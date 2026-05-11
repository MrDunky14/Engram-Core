"""Slice ConceptNet assertions.gz into a normalized CSV of SVO triples for benchmarks."""

import gzip
import csv
import json
import os

def extract_conceptnet(input_file, output_file, max_facts=10000):
    print(f"Extracting {max_facts} facts from {input_file}...")
    
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.")
        return

    facts = set()
    
    # We want a diverse set of relations
    valid_relations = {'/r/IsA', '/r/HasA', '/r/CapableOf', '/r/UsedFor', '/r/AtLocation', '/r/PartOf', '/r/MadeOf', '/r/Causes'}

    with gzip.open(input_file, 'rt', encoding='utf-8') as f:
        reader = csv.reader(f, delimiter='\t')
        
        for row in reader:
            if len(row) < 5:
                continue
                
            _, rel, start, end, metadata = row
            
            # We only care about English terms and specific relations
            if not start.startswith('/c/en/') or not end.startswith('/c/en/'):
                continue
                
            if rel not in valid_relations:
                continue
                
            try:
                meta = json.loads(metadata)
                weight = meta.get('weight', 0.0)
            except:
                weight = 0.0
                
            if weight < 1.0:
                continue
                
            # Extract the actual words (e.g., /c/en/dog -> DOG)
            subj = start.split('/')[3].upper()
            obj = end.split('/')[3].upper()
            relation = rel.split('/')[2].upper()
            
            # Clean up multi-word phrases for our tokenizer
            subj = subj.replace("_", "")
            obj = obj.replace("_", "")
            
            # Filter out overly complex/long nodes
            if len(subj) > 15 or len(obj) > 15:
                continue
                
            # Add to facts
            facts.add((subj, relation, obj))
            
            if len(facts) % 1000 == 0:
                print(f"  Extracted {len(facts)} facts...", end='\r')
                
            if len(facts) >= max_facts:
                break

    facts_list = list(facts)
    print(f"\nWriting {len(facts_list)} clean facts to {output_file}...")
    
    with open(output_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        for fact in facts_list:
            writer.writerow(fact)
            
    print("Extraction complete.")

if __name__ == "__main__":
    input_path = "data/conceptnet-assertions-5.7.0.csv.gz"
    output_path = "data/conceptnet_10k_real.csv"
    extract_conceptnet(input_path, output_path, 10000)
