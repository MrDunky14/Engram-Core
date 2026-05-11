import subprocess
import tkinter as tk
import random

# --- 1. The Pre-Frontal Cortex (NLP Router) ---
STOP_WORDS = {"who", "what", "where", "when", "why", "how", "is", "was", "a", "the", "he", "she", "it", "of", "do", "does"}

def clean_query(user_input):
    words = user_input.lower().replace("?", "").replace(".", "").split()
    keywords = [w for w in words if w not in STOP_WORDS]
    return " ".join(keywords)

# --- 2. The Hippocampus Link (C++ Subprocess) ---
def query_cpp_engine(keyword_string):
    # Simulates passing the dense keyword string to your v4_apex binary
    if "augustus" in keyword_string or "empire" in keyword_string:
        return "Augustus"
    elif "brain" in keyword_string or "nervous" in keyword_string:
        return "Neuroscience"
    else:
        return "MEMORY_NOT_FOUND"

# --- 3. The Unified Visual Cortex & UI ---
class SpikingVisualizer:
    def __init__(self, root):
        self.root = root
        self.root.title("Engram Core — FP-SAN v17 | cognitive ecosystem")
        self.root.configure(bg="#0a0a12")
        
        # The Neural Canvas
        self.canvas = tk.Canvas(root, width=800, height=450, bg="#0a0a12", highlightthickness=0)
        self.canvas.pack(pady=10)
        
        # The System Response Terminal
        self.response_label = tk.Label(root, text="[System] Awaiting input...", bg="#0a0a12", fg="#555577", font=("Consolas", 12))
        self.response_label.pack(pady=5)

        # The Cyberpunk Input Bar
        self.chat_frame = tk.Frame(root, bg="#0a0a12")
        self.chat_frame.pack(fill=tk.X, padx=20, pady=10)
        
        self.entry = tk.Entry(self.chat_frame, bg="#1e1e32", fg="#00ffff", font=("Consolas", 14), insertbackground="#00ffff", relief=tk.FLAT)
        self.entry.pack(side=tk.LEFT, fill=tk.X, expand=True, ipady=8)
        self.entry.bind("<Return>", self.handle_query)
        self.entry.focus()
        
        # Initialize Particles
        self.particles = []
        for _ in range(300):
            x = random.randint(50, 750)
            y = random.randint(50, 400)
            vx = random.uniform(-0.3, 0.3)
            vy = random.uniform(-0.3, 0.3)
            domain = "text" if x < 400 else "vision"
            node = self.canvas.create_oval(x, y, x+3, y+3, fill="#1e1e32", outline="")
            self.particles.append({"id": node, "x": x, "y": y, "vx": vx, "vy": vy, "domain": domain, "active": 0})

        self.animate()

    def animate(self):
        for p in self.particles:
            # Drift physics
            p["x"] += p["vx"]
            p["y"] += p["vy"]
            
            # Smooth edge bouncing
            if p["x"] < 10 or p["x"] > 790: p["vx"] *= -1
            if p["y"] < 10 or p["y"] > 440: p["vy"] *= -1
            
            # Heat decay for spiking neurons
            if p["active"] > 0:
                p["active"] -= 1
                color = "#00ffff" if p["domain"] == "text" else "#ff00ff"
                if p["active"] < 10: color = "#1e1e32" 
            else:
                color = "#1e1e32"
                
            self.canvas.coords(p["id"], p["x"], p["y"], p["x"]+4, p["y"]+4)
            self.canvas.itemconfig(p["id"], fill=color)

        # Loop the animation completely independently of user typing
        self.root.after(30, self.animate)

    def trigger_spike(self, domain_type):
        for p in self.particles:
            if p["domain"] == domain_type and random.random() < 0.3:
                p["active"] = 30 # Light up for 30 frames

    def handle_query(self, event):
        query = self.entry.get()
        if not query: return
        
        self.entry.delete(0, tk.END)
        self.response_label.config(text=f"[User] {query}", fg="#aaaaaa")
        
        # 1. NLP Filtering
        cleaned_query = clean_query(query)
        
        # 2. Visual Cortex Activation
        self.trigger_spike("text")
        
        # 3. Simulate C++ Engine Latency & Retrieval
        self.root.after(400, self.display_result, cleaned_query)

    def display_result(self, cleaned_query):
        result = query_cpp_engine(cleaned_query)
        if result != "MEMORY_NOT_FOUND":
            self.response_label.config(text=f"[FP-SAN] Retrieval Success -> {result}", fg="#00ffff")
        else:
            self.response_label.config(text="[FP-SAN] Memory corrupted or not found.", fg="#ff4444")

# --- 4. Boot Sequence ---
if __name__ == "__main__":
    root = tk.Tk()
    # Force window to top so you don't have to hunt for it
    root.attributes('-topmost', True)
    root.update()
    root.attributes('-topmost', False)
    
    app = SpikingVisualizer(root)
    root.mainloop()
