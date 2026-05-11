# `data/` — local datasets

Small fixture files in this directory may be committed. **Oversized or regenerable** inputs are **gitignored** so the repo stays within GitHub limits.

| File | In Git? | How to obtain |
|------|---------|----------------|
| `conceptnet-assertions-5.7.0.csv.gz` | No (~475 MB) | Download from [ConceptNet](https://github.com/commonsense/conceptnet/wiki/Downloads) (assertions 5.7.0). Place here, then run `python tools/extract_conceptnet.py`. |
| `mnist_stream.csv` | No (~52 MB) | `pip install scikit-learn` then `python tools/gen_mnist.py` |
| `conceptnet_1000.csv` | Yes (optional) | Or run `python tools/fetch_conceptnet.py` for synthetic triples. |
| `HumanEval.jsonl.gz` | Yes (small) | Or download from [openai_humaneval](https://github.com/openai/human-eval). |

`tools/README.md` lists other generators (`benchmark.csv`, `rag_vectors.csv`, etc.).
