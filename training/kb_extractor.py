#!/usr/bin/env python3
"""
Engram Core / FP-SAN v17 — Phase 5A: knowledge mass extractor
==========================================
Run on Kaggle (2×T4 GPU, 30 h/week) or Google Colab.

Reads:
  - Simple English Wikipedia XML dump  (simplewiki-latest-pages-articles.xml.bz2)
  - ConceptNet CSV dump                (conceptnet-assertions-5.7.0.csv)
  - ATOMIC2020 CSV                     (atomic2020_data-feb2021.zip)

Outputs:
  - knowledge_mass.bin  (binary, loadable by fpsan_knowledge_mass.h)

Binary format per record (22 bytes, little-endian):
  char  subject[64]   — null-terminated UTF-8 string
  char  object[64]    — null-terminated UTF-8 string
  uint8 relation      — EdgeType enum value (matches cluster_graph.h)
  float weight        — 0.0-1.0 confidence / co-occurrence score
  uint8 provenance    — EdgeProvenance enum value

Install:
  pip install transformers accelerate bz2file tqdm lxml

Usage on Kaggle:
  !python kb_extractor.py --wiki simplewiki-latest-pages-articles.xml.bz2 \
                           --cn   conceptnet-assertions-5.7.0.csv \
                           --out  knowledge_mass.bin \
                           --max  200000
"""

import argparse, bz2, csv, os, re, struct, sys
from pathlib import Path
from tqdm import tqdm

# ── EdgeType mirrors cluster_graph.h ──
EDGE_TEMPORAL      = 0
EDGE_IS_A          = 1
EDGE_HAS_A         = 2
EDGE_CAN_DO        = 3
EDGE_CAUSES        = 4
EDGE_SEQUENCE      = 5
EDGE_RELATED       = 10
EDGE_ANTONYM       = 18

# ── EdgeProvenance ──
PROV_UNKNOWN    = 0
PROV_USER       = 1
PROV_WIKIPEDIA  = 2
PROV_CONCEPTNET = 3
PROV_INFERRED   = 4

# ConceptNet relation → EdgeType mapping
CN_REL_MAP = {
    "/r/IsA":           EDGE_IS_A,
    "/r/PartOf":        EDGE_HAS_A,
    "/r/HasA":          EDGE_HAS_A,
    "/r/CapableOf":     EDGE_CAN_DO,
    "/r/Causes":        EDGE_CAUSES,
    "/r/Antonym":       EDGE_ANTONYM,
    "/r/RelatedTo":     EDGE_RELATED,
    "/r/UsedFor":       EDGE_CAN_DO,
    "/r/HasProperty":   EDGE_HAS_A,
    "/r/InstanceOf":    EDGE_IS_A,
    "/r/CausesDesire":  EDGE_CAUSES,
    "/r/MadeOf":        EDGE_HAS_A,
    "/r/DefinedAs":     EDGE_IS_A,
    "/r/SymbolOf":      EDGE_RELATED,
    "/r/LocatedNear":   EDGE_RELATED,
    "/r/HasSubevent":   EDGE_SEQUENCE,
    "/r/HasFirstSubevent": EDGE_SEQUENCE,
    "/r/HasLastSubevent":  EDGE_SEQUENCE,
    "/r/HasPrerequisite":  EDGE_CAUSES,
    "/r/MotivatedByGoal":  EDGE_CAUSES,
    "/r/Desires":       EDGE_CAN_DO,
    "/r/NotDesires":    EDGE_ANTONYM,
}

RECORD_FMT  = "64s64sBfB"   # subject, object, relation, weight, provenance
RECORD_SIZE = struct.calcsize(RECORD_FMT)  # should be 134

def encode(s: str, length: int = 64) -> bytes:
    b = s.encode("utf-8", errors="replace")[:length-1]
    return b.ljust(length, b"\x00")

def slug_to_word(uri: str) -> str:
    """Convert /c/en/water_bottle to 'water bottle'."""
    parts = uri.split("/")
    if len(parts) >= 4:
        return parts[3].replace("_", " ").lower()
    return uri.replace("_", " ").lower()

def clean(word: str) -> str:
    word = re.sub(r"[^a-z0-9 '\-]", "", word.lower()).strip()
    return word[:62] if word else ""

# ────────────────────────────────────────────────────────────
# ConceptNet extraction
# ────────────────────────────────────────────────────────────
def extract_conceptnet(cn_path: str, out, written: list, max_triples: int):
    print(f"[ConceptNet] Reading {cn_path} …")
    with open(cn_path, encoding="utf-8", newline="") as f:
        reader = csv.reader(f, delimiter="\t")
        for row in tqdm(reader, desc="ConceptNet"):
            if written[0] >= max_triples:
                break
            if len(row) < 5:
                continue
            relation = row[1]
            subj_uri = row[2]
            obj_uri  = row[3]
            weight_s = row[4]

            if not subj_uri.startswith("/c/en/") or not obj_uri.startswith("/c/en/"):
                continue

            edge_type = CN_REL_MAP.get(relation)
            if edge_type is None:
                continue

            subj = clean(slug_to_word(subj_uri))
            obj  = clean(slug_to_word(obj_uri))
            if not subj or not obj or subj == obj:
                continue

            # Parse weight from JSON blob or default
            try:
                import json
                meta = json.loads(weight_s)
                w = float(meta.get("weight", 1.0))
            except Exception:
                w = 1.0
            w = min(max(w / 10.0, 0.05), 1.0)

            rec = struct.pack(RECORD_FMT,
                              encode(subj), encode(obj),
                              edge_type, w, PROV_CONCEPTNET)
            out.write(rec)
            written[0] += 1

# ────────────────────────────────────────────────────────────
# Simple English Wikipedia sentence-triple extraction
# (no LLM needed — rule-based IS_A / HAS_A from first sentence)
# ────────────────────────────────────────────────────────────
IS_A_RE  = re.compile(r"^([A-Z][a-z]+(?:\s[a-z]+)?)\s+is\s+(?:a|an|the)\s+([a-z][a-z\s]{1,40})", re.M)
HAS_A_RE = re.compile(r"\b([a-z]+(?:\s[a-z]+)?)\s+has\s+(?:a|an)\s+([a-z][a-z\s]{1,30})", re.M)
CAN_DO_RE= re.compile(r"\b([a-z]+)\s+can\s+([a-z]+(?:\s[a-z]+){0,3})", re.M)

def extract_wikipedia(wiki_path: str, out, written: list, max_triples: int):
    print(f"[Wikipedia] Reading {wiki_path} …")
    try:
        import lxml.etree as ET
    except ImportError:
        print("  lxml not installed — skipping Wikipedia extraction. pip install lxml")
        return

    opener = bz2.open if wiki_path.endswith(".bz2") else open
    with opener(wiki_path, "rb") as f:
        context = ET.iterparse(f, events=("end",), tag="{http://www.mediawiki.org/xml/DTD/MediaWiki}text")
        for _, elem in tqdm(context, desc="Wikipedia"):
            if written[0] >= max_triples:
                break
            text = elem.text or ""
            if not text or len(text) < 50:
                elem.clear()
                continue

            for m in IS_A_RE.finditer(text):
                if written[0] >= max_triples: break
                subj = clean(m.group(1))
                obj  = clean(m.group(2).split(".")[0].strip())
                if subj and obj and len(subj) > 2 and len(obj) > 2:
                    rec = struct.pack(RECORD_FMT,
                                      encode(subj), encode(obj),
                                      EDGE_IS_A, 0.6, PROV_WIKIPEDIA)
                    out.write(rec)
                    written[0] += 1

            for m in HAS_A_RE.finditer(text):
                if written[0] >= max_triples: break
                subj = clean(m.group(1))
                obj  = clean(m.group(2).split(".")[0].strip())
                if subj and obj and len(subj) > 2 and len(obj) > 2:
                    rec = struct.pack(RECORD_FMT,
                                      encode(subj), encode(obj),
                                      EDGE_HAS_A, 0.5, PROV_WIKIPEDIA)
                    out.write(rec)
                    written[0] += 1

            for m in CAN_DO_RE.finditer(text):
                if written[0] >= max_triples: break
                subj = clean(m.group(1))
                obj  = clean(m.group(2).strip())
                if subj and obj and len(subj) > 2 and len(obj) > 2:
                    rec = struct.pack(RECORD_FMT,
                                      encode(subj), encode(obj),
                                      EDGE_CAN_DO, 0.5, PROV_WIKIPEDIA)
                    out.write(rec)
                    written[0] += 1

            elem.clear()

# ────────────────────────────────────────────────────────────
# LLM-enhanced extraction (optional — uses GPU if available)
# Reads a plain-text sentence file and extracts triples via
# Mistral-7B-Instruct-Q4 or Phi-3.5-mini for higher precision.
# ────────────────────────────────────────────────────────────
def extract_llm(sentences_path: str, out, written: list, max_triples: int):
    try:
        from transformers import AutoTokenizer, AutoModelForCausalLM
        import torch
    except ImportError:
        print("[LLM] transformers not available — skipping LLM extraction.")
        return

    model_id = "microsoft/Phi-3.5-mini-instruct"
    print(f"[LLM] Loading {model_id} …")
    tok = AutoTokenizer.from_pretrained(model_id, trust_remote_code=True)
    mdl = AutoModelForCausalLM.from_pretrained(
        model_id, torch_dtype=torch.float16,
        device_map="auto", trust_remote_code=True)
    mdl.eval()

    PROMPT_TMPL = (
        "Extract SUBJECT RELATION OBJECT triples from this sentence. "
        "Use only: IS_A, HAS_A, CAN_DO, CAUSES, ANTONYM, RELATED. "
        "Output one triple per line as: subject|relation|object\n\n"
        "Sentence: {sent}\n\nTriples:"
    )

    REL_LUT = {"IS_A": EDGE_IS_A, "HAS_A": EDGE_HAS_A, "CAN_DO": EDGE_CAN_DO,
               "CAUSES": EDGE_CAUSES, "ANTONYM": EDGE_ANTONYM, "RELATED": EDGE_RELATED}

    with open(sentences_path, encoding="utf-8") as f:
        sentences = [l.strip() for l in f if len(l.strip()) > 20]

    for sent in tqdm(sentences, desc="LLM triples"):
        if written[0] >= max_triples:
            break
        prompt = PROMPT_TMPL.format(sent=sent[:200])
        inputs = tok(prompt, return_tensors="pt").to(mdl.device)
        with torch.no_grad():
            out_ids = mdl.generate(**inputs, max_new_tokens=80, do_sample=False)
        decoded = tok.decode(out_ids[0][inputs["input_ids"].shape[1]:], skip_special_tokens=True)
        for line in decoded.splitlines():
            parts = line.strip().split("|")
            if len(parts) != 3:
                continue
            subj = clean(parts[0])
            rel  = parts[1].strip().upper()
            obj  = clean(parts[2])
            etype = REL_LUT.get(rel)
            if not subj or not obj or etype is None:
                continue
            rec = struct.pack(RECORD_FMT,
                              encode(subj), encode(obj),
                              etype, 0.7, PROV_WIKIPEDIA)
            out.write(rec)
            written[0] += 1

# ────────────────────────────────────────────────────────────
# Entry point
# ────────────────────────────────────────────────────────────
def write_phase12_artifacts(base_dir="."):
    """Emit skeletal Phase-12 artefacts (templates / bigrams / intent prototypes).

    Canonical layout on Kaggle/Colab:
      templates.bin   — MAGIC 'TPL1' + serialized intent-id + short UTF-8 pattern
      bigrams.bin     — MAGIC 'BIG1' + uint32 hashed predecessor/successor pairs
      intent_proto.bin — MAGIC 'IPR1' + float activation fingerprint blobs (stub)
    """
    base = Path(base_dir)

    tmpl = base / "templates.bin"
    with open(tmpl, "wb") as f:
        f.write(b"TPL1\x00\x02")  # version 2 records
        f.write(struct.pack("<I", 2))
        patterns = [(0, "[SUBJECT] is a [OBJECT]"),
                    (1, "[OBJECT] relates [SUBJECT]")]
        for intent_id, text in patterns:
            raw = text.encode("utf-8")[:119]
            f.write(struct.pack("<B", intent_id))
            f.write(raw + b"\x00" * (120 - len(raw)))

    bigrams = base / "bigrams.bin"
    with open(bigrams, "wb") as f:
        f.write(b"BIG1\x01")
        pairs = [("sun", "is"), ("is", "a")]
        f.write(struct.pack("<I", len(pairs)))

        def w32(s):
            h = sum(ord(c) * (i + 1) for i, c in enumerate(s[:63]))
            return h & 0xFFFFFFFF

        for a, b in pairs:
            f.write(struct.pack("<II", w32(a), w32(b)))

    intents = base / "intent_prototypes.bin"
    with open(intents, "wb") as f:
        # Placeholder fingerprints — consumed by future cxx loader.
        f.write(b"IPR1\x01")
        f.write(struct.pack("<I", 6500))


def main():
    ap = argparse.ArgumentParser(description="Engram Core / FP-SAN v17 — knowledge mass extractor")
    ap.add_argument("--wiki",  default="", help="Simple English Wikipedia .xml.bz2 path")
    ap.add_argument("--cn",    default="", help="ConceptNet CSV path")
    ap.add_argument("--llm",   default="", help="Optional plain-text sentence file for LLM extraction")
    ap.add_argument("--out",   default="knowledge_mass.bin", help="Output binary path")
    ap.add_argument("--max",   type=int, default=200_000, help="Max triples to write")
    ap.add_argument("--emit_phase12_artifacts",
                    default="", help="Optional directory to drop templates/bigrams/intent stubs")
    args = ap.parse_args()

    if args.emit_phase12_artifacts:
        write_phase12_artifacts(args.emit_phase12_artifacts)
        print(f"[Phase12] Wrote artefacts into {args.emit_phase12_artifacts}")

    written = [0]
    with open(args.out, "wb") as out:
        # Write magic header: "FPSANKM" + version byte
        out.write(b"FPSANKM\x01")

        if args.cn:
            extract_conceptnet(args.cn, out, written, args.max)
        if args.wiki:
            extract_wikipedia(args.wiki, out, written, args.max - written[0])
        if args.llm:
            extract_llm(args.llm, out, written, args.max - written[0])

    sz_mb = os.path.getsize(args.out) / 1e6
    print(f"\n[Done] Wrote {written[0]:,} triples → {args.out} ({sz_mb:.1f} MB)")
    print(f"       Record size = {RECORD_SIZE} bytes")
    print(f"       Load with:  !load_mass {args.out}")

if __name__ == "__main__":
    main()
