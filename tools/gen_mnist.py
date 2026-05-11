"""Generate `data/mnist_stream.csv` + `data/mnist_labels.csv` for legacy FP-SAN benchmarks."""

import os
import numpy as np
from sklearn.datasets import fetch_openml
from sklearn.utils import shuffle

print("[Python] Downloading MNIST dataset from OpenML (this may take a minute)...")
# Fetch the official 784-dimensional handwritten digit dataset
mnist = fetch_openml('mnist_784', version=1, parser='auto')
X = mnist.data.to_numpy()
y = mnist.target.to_numpy()

print("[Python] Dataset loaded. Normalizing electrical spikes...")
# Convert pixel intensities (0-255) to analog voltage signals (0.0 - 1.0)
X = X / 255.0

# Shuffle the dataset to ensure the AI doesn't just memorize in order
X, y = shuffle(X, y, random_state=42)

# We will stream 10,000 images for the benchmark. 
# This is enough to prove stability without making you wait 30 minutes.
sample_size = 10000
X_subset = X[:sample_size]
y_subset = y[:sample_size]

print(f"[Python] Exporting {sample_size} continuous sensory frames...")
os.makedirs("data", exist_ok=True)
stream_path = os.path.join("data", "mnist_stream.csv")
labels_path = os.path.join("data", "mnist_labels.csv")
np.savetxt(stream_path, X_subset, delimiter=",", fmt="%.4f")
np.savetxt(labels_path, y_subset, delimiter=",", fmt="%s")

print(f"[Python] Success. Wrote {stream_path} and {labels_path} (Engram Core / FP-SAN benchmarks).")