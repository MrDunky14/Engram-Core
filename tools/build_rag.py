"""Optional Wikipedia → TF-IDF vectors for local RAG experiments (requires PyPI deps)."""

import os
import wikipediaapi
from sklearn.feature_extraction.text import TfidfVectorizer
import numpy as np

wiki_wiki = wikipediaapi.Wikipedia(
    user_agent="EngramCore/1.0.0 (FP-SAN v17; research tooling — set your public repo URL if needed)",
    language="en",
)

# The 4 distinct domains the AI must learn to separate unsupervised
topics = [
    "Quantum_mechanics", "Schrödinger_equation", "Quantum_entanglement", "String_theory", "Black_hole",
    "Ancient_Rome", "Julius_Caesar", "Roman_Empire", "Augustus", "Colosseum",
    "Neuroscience", "Human_brain", "Neuron", "Synapse", "Cerebral_cortex",
    "Space_exploration", "Mars", "Apollo_11", "International_Space_Station", "Hubble_Space_Telescope"
]

print("[Python] Downloading Wikipedia articles...")
documents = []
titles = []

for topic in topics:
    page = wiki_wiki.page(topic)
    if page.exists():
        # Grab the summary of the page to keep vectors dense and focused
        documents.append(page.summary)
        titles.append(page.title)
        print(f"  -> Downloaded: {page.title}")

print(f"\n[Python] Vectorizing {len(documents)} articles into 784 dimensions...")
# Restrict to 784 words, stripping out common stop words ("the", "and")
vectorizer = TfidfVectorizer(max_features=784, stop_words='english')
vectors = vectorizer.fit_transform(documents)
dense_vectors = vectors.toarray()

os.makedirs("data", exist_ok=True)
vec_path = os.path.join("data", "rag_vectors.csv")
meta_path = os.path.join("data", "rag_metadata.txt")
vocab_path = os.path.join("data", "rag_vocab.txt")
np.savetxt(vec_path, dense_vectors, delimiter=",", fmt="%.6f")

with open(meta_path, "w", encoding="utf-8") as f:
    for title in titles:
        f.write(title + "\n")

# Save the vocabulary so we can vectorize user search queries later
vocab = vectorizer.get_feature_names_out()
with open(vocab_path, "w", encoding="utf-8") as f:
    for word in vocab:
        f.write(word + "\n")

print("\n[Python] Success. Knowledge base written under data/.")
print(f"Files: {vec_path}, {meta_path}, {vocab_path}")