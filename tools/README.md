# Optional Python tooling (Engram Core / FP-SAN)

Optional **Python** helpers for **`data/`** datasets (not required for R6). **Engine:** FP-SAN v17.0 · release v1.0.0.

Install deps only when you run a given script (suggested venv from repo root):

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install numpy scikit-learn   # gen_mnist, generate_benchmark
pip install wikipedia-api        # build_rag (+ scikit-learn)
pip install opencv-python mss pyautogui   # screen/webcam/teacher sensors, as needed
```

## Scripts

| Script | Inputs | Outputs | Notes |
|--------|--------|---------|--------|
| [`gen_mnist.py`](gen_mnist.py) | OpenML MNIST | `data/mnist_stream.csv`, `data/mnist_labels.csv` | Matches paths expected by `src/benchmark/fpsan_benchmark*.cpp`. |
| [`generate_benchmark.py`](generate_benchmark.py) | sklearn 20 Newsgroups | `data/benchmark.csv` | Optional TF-IDF vectors; not wired into R6. |
| [`build_rag.py`](build_rag.py) | English Wikipedia summaries | `data/rag_vectors.csv`, `data/rag_metadata.txt`, `data/rag_vocab.txt` | Sets a proper **User-Agent**; do not crawl aggressively. |
| [`fetch_conceptnet.py`](fetch_conceptnet.py) | (none — synthetic) | `data/conceptnet_1000.csv` | Name is historical — **no network**; used with `fpsan_knowledge_test.cpp`. |
| [`extract_conceptnet.py`](extract_conceptnet.py) | `data/conceptnet-assertions-5.7.0.csv.gz` | `data/conceptnet_10k_real.csv` | Real ConceptNet slice if you supply the gz. |
| [`humaneval_ast_ingestor.py`](humaneval_ast_ingestor.py) | `data/HumanEval.jsonl.gz` | `data/humaneval_ast_triples.csv`, `data/humaneval_prompts.csv` | AST triple export for `fpsan_humaneval_test` and experiments. |
| [`screen_sensor.py`](screen_sensor.py) | Display, UDP | UDP `127.0.0.1:5005` | Demo grabber; optional `cv2` + `mss`. |
| [`webcam_sensor.py`](webcam_sensor.py) | Camera or synthetic | UDP `127.0.0.1:5005` | Falls back if OpenCV missing. |
| [`teacher_sensor.py`](teacher_sensor.py) | stdin, Notepad | UDP `127.0.0.1:5006` | Cross-modal typing demo with a running `engram.exe`. |

## Architecture docs

- [`SOVEREIGN_RESEARCHER.md`](../SOVEREIGN_RESEARCHER.md) — optional web research + distiller (C++).
- [`INFORMATION_DISTILLER.md`](../INFORMATION_DISTILLER.md) — distiller pipeline.
- [`FP-SAN Architecture.md`](../FP-SAN%20Architecture.md) — cognitive architecture overview.
