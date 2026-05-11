"""Optional: 20 Newsgroups → dense TF-IDF CSV for experiments (not used by R6 gates)."""

import os
from sklearn.datasets import fetch_20newsgroups
from sklearn.feature_extraction.text import TfidfVectorizer
import numpy as np

print("[Python] Downloading 20 Newsgroups dataset (PC Hardware vs. Baseball)...")
# We select two completely unrelated topics to see if the AI can separate them
categories = ['comp.sys.ibm.pc.hardware', 'rec.sport.baseball']
dataset = fetch_20newsgroups(subset='train', categories=categories, shuffle=True, random_state=42)

print(f"[Python] Loaded {len(dataset.data)} articles.")
print("[Python] Vectorizing text into exactly 784 dimensions...")

# Convert the English text into mathematical vectors. 
# We restrict the vocabulary to the top 784 words so it perfectly fits your INPUT_DIM.
vectorizer = TfidfVectorizer(max_features=784, stop_words='english')
vectors = vectorizer.fit_transform(dataset.data)

# Convert the sparse matrix into a dense array
dense_vectors = vectors.toarray()

os.makedirs("data", exist_ok=True)
out_path = os.path.join("data", "benchmark.csv")
print(f"[Python] Saving to {out_path}...")
np.savetxt(out_path, dense_vectors, delimiter=",", fmt="%.6f")

print("[Python] Success (Engram Core optional benchmarking helper).")