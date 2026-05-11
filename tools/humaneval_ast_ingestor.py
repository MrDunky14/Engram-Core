"""
HumanEval JSONL.gz → AST triple CSV for optional neuromorphic ingest experiments.

Output paths are relative to repo root (`data/`).
Requires `data/HumanEval.jsonl.gz` (download separately).

(Formerly `jarvis_ast_ingestor.py`.)
"""
import gzip
import json
import csv
import ast
import os

class FullASTVisitor(ast.NodeVisitor):
    def __init__(self, problem_id):
        self.triples = set()
        self.problem_id = problem_id
        # We assign a unique ID per AST node instance so the graph is a true tree,
        # but the node's label is semantic so the network learns the grammar.
        # Wait, if we use unique IDs, the LanguageCortex won't generalize across problems!
        # If we use purely semantic labels (like 'Return', 'Add', 'x'), the graph will merge them.
        # The user wants an unsimplified AST. Merging them is correct for a Neuromorphic graph.
        
    def get_semantic_label(self, node):
        if isinstance(node, ast.Name): return f"Var_{node.id}"
        if isinstance(node, ast.arg): return f"Arg_{node.arg}"
        if isinstance(node, ast.FunctionDef): return f"Func_{node.name}"
        if isinstance(node, ast.ClassDef): return f"Class_{node.name}"
        if isinstance(node, ast.Constant): 
            val = str(node.value).replace('\n', ' ').replace(',', '')
            if len(val) > 20: val = val[:20] + "..."
            return f"Const_{val}"
        return type(node).__name__

    def visit_and_link(self, parent_label, relation, node):
        if isinstance(node, ast.AST):
            child_label = self.get_semantic_label(node)
            # Add triple
            self.triples.add((parent_label, relation.upper(), child_label))
            # Continue traversal
            self.generic_visit(node)
        elif node is not None:
            # Primitives (int, str, bool)
            val = str(node).replace('\n', ' ').replace(',', '')
            if len(val) > 20: val = val[:20] + "..."
            child_label = f"Val_{val}"
            self.triples.add((parent_label, relation.upper(), child_label))

    def generic_visit(self, node):
        parent_label = self.get_semantic_label(node)
        
        for field, value in ast.iter_fields(node):
            if isinstance(value, list):
                for item in value:
                    self.visit_and_link(parent_label, field, item)
            else:
                self.visit_and_link(parent_label, field, value)

def ingest_humaneval():
    input_file = "data/HumanEval.jsonl.gz"
    output_file = "data/humaneval_ast_triples.csv"
    docstrings_file = "data/humaneval_prompts.csv"
    
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.")
        return

    all_triples = set()
    prompts = []

    with gzip.open(input_file, 'rt', encoding='utf-8') as f:
        for line in f:
            data = json.loads(line)
            task_id = data['task_id'].replace('/', '_')
            prompt = data['prompt']
            canonical_solution = data['canonical_solution']
            
            # The full code is the prompt + solution
            full_code = prompt + canonical_solution
            
            # Extract just the docstring as the "Query" for the benchmark
            # The prompt contains the def and the docstring.
            docstring = prompt.split('"""')[1].strip().replace('\n', ' ') if '"""' in prompt else "solve " + task_id
            if len(docstring) > 50: docstring = docstring[:50]
            
            prompts.append((task_id, docstring))
            
            try:
                tree = ast.parse(full_code)
                visitor = FullASTVisitor(task_id)
                visitor.visit(tree)
                
                # Link TaskID to the root Module
                all_triples.add((task_id, "HAS_DOCSTRING", "Doc_" + docstring.replace(' ', '_')[:20]))
                
                # Link Docstring to the root function
                for node in tree.body:
                    if isinstance(node, ast.FunctionDef):
                        func_label = visitor.get_semantic_label(node)
                        all_triples.add(("Doc_" + docstring.replace(' ', '_')[:20], "IMPLEMENTED_BY", func_label))
                
                all_triples.update(visitor.triples)
            except SyntaxError as e:
                print(f"Syntax error parsing {task_id}: {e}")
                continue

    print(f"Writing {len(all_triples)} AST triples to {output_file}...")
    with open(output_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        for t in all_triples:
            writer.writerow(t)
            
    print(f"Writing {len(prompts)} prompts to {docstrings_file}...")
    with open(docstrings_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        for p in prompts:
            writer.writerow(p)

    print("Ingestion complete.")

if __name__ == "__main__":
    ingest_humaneval()
