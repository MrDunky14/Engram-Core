"""
Synthetic ConceptNet-style SVO triples for local tests.

The filename is historical: this script does not call the ConceptNet API.
It writes `data/conceptnet_1000.csv`, consumed by `src/benchmark/fpsan_knowledge_test.cpp`
(and similar) when present.
"""
import csv
import random
import os

def generate_messy_ontology(num_facts=1000):
    # Base ontologies to mix
    animals = ["DOG", "CAT", "BIRD", "FISH", "SHARK", "WHALE", "EAGLE", "SPARROW", "TIGER", "LION"]
    traits = ["ANIMAL", "PET", "PREDATOR", "MAMMAL", "CARNIVORE", "FLYING", "SWIMMING", "WILD"]
    actions = ["BARK", "MEOW", "FLY", "SWIM", "HUNT", "RUN", "JUMP", "SLEEP", "BITE"]
    habitats = ["HOUSE", "OCEAN", "FOREST", "SKY", "JUNGLE", "RIVER", "CITY"]
    
    # We want a lot of overlap (e.g. many things are MAMMALS, many things HUNT)
    # We'll programmatically generate 1000 unique messy facts by combining random English words
    # to simulate the semantic density of ConceptNet.
    
    prefixes = ["RED", "BLUE", "BIG", "SMALL", "FAST", "SLOW", "OLD", "NEW", "WILD", "DOMESTIC"]
    nouns = ["CAR", "TREE", "HOUSE", "CITY", "RIVER", "MOUNTAIN", "CLOUD", "STAR", "BOOK", "PEN"]
    verbs = ["IS_A", "HAS_A", "CAPABLE_OF", "USED_FOR", "LOCATED_IN", "PART_OF", "MADE_OF"]
    
    facts = set()
    
    # 1. Core logical facts (for 2-hop testing)
    facts.add(("SPARROW", "IS_A", "BIRD"))
    facts.add(("BIRD", "CAPABLE_OF", "FLY"))
    facts.add(("DOG", "IS_A", "MAMMAL"))
    facts.add(("MAMMAL", "HAS_A", "HAIR"))
    facts.add(("FISH", "LOCATED_IN", "WATER"))
    facts.add(("SHARK", "IS_A", "FISH"))
    
    # 2. Fill the rest with messy overlapping semantics
    while len(facts) < num_facts:
        rel = random.choice(verbs)
        
        # Sometimes pick from animals, sometimes from generic nouns to create diverse clusters
        if random.random() < 0.3:
            subj = random.choice(animals)
            if rel == "IS_A":
                obj = random.choice(traits)
            elif rel == "CAPABLE_OF":
                if subj == "SPARROW": continue # Force 2-hop inference via BIRD
                obj = random.choice(actions)
            elif rel == "LOCATED_IN":
                obj = random.choice(habitats)
            else:
                obj = random.choice(nouns)
        else:
            subj = f"{random.choice(prefixes)}_{random.choice(nouns)}"
            if rel == "IS_A":
                obj = random.choice(nouns)
            else:
                obj = f"{random.choice(prefixes)}_{random.choice(nouns)}"
                
        facts.add((subj, rel, obj))

    return list(facts)[:num_facts]

if __name__ == "__main__":
    facts = generate_messy_ontology(1000)
    
    os.makedirs("data", exist_ok=True)
    with open("data/conceptnet_1000.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        for fact in facts:
            writer.writerow(fact)
            
    print(f"Generated {len(facts)} messy semantic facts into data/conceptnet_1000.csv")
    print("Example facts:")
    for f in facts[:5]:
        print(f"  {f[0]} -> {f[1]} -> {f[2]}")
