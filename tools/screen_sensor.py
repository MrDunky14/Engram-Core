import socket
import time
import argparse
import sys
import numpy as np

try:
    import cv2
    from mss import MSS
    HAS_DEPS = True
except ImportError:
    HAS_DEPS = False

UDP_IP = "127.0.0.1"
UDP_PORT = 5005
FRAME_SIZE = 784 # 28x28

def run_screen_sensor(fps=30, monitor_idx=1):
    print("[Sensor] Engram Core demo — screen region grabber")
    print(f"[Sensor] Streaming to UDP {UDP_IP}:{UDP_PORT} at {fps} FPS")
    print("[Sensor] Move your Notepad window to the TOP LEFT of your primary monitor.")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Capture a 400x400 region at the top-left of the screen
    # This is the "Nursery Sandbox" where Notepad should be placed
    monitor = {"top": 0, "left": 0, "width": 400, "height": 400}
    
    sct = MSS()
    frame_count = 0
    print("[Sensor] Press 'q' in the preview window to exit.")
    
    try:
        while True:
            # 1. Grab screen
            img = np.array(sct.grab(monitor))
            
            # 2. Grayscale
            gray = cv2.cvtColor(img, cv2.COLOR_BGRA2GRAY)
            
            # 3. Resize to 28x28
            resized = cv2.resize(gray, (28, 28), interpolation=cv2.INTER_AREA)
            
            # 4. Binarize
            # Notepad has white background, black text. 
            # We want text to be '1' (spikes) and background '0'.
            # So we invert: pixels < 128 become 1 (text), pixels > 128 become 0 (bg).
            _, binarized = cv2.threshold(resized, 128, 1, cv2.THRESH_BINARY_INV)
            
            # 5. Send over UDP
            flat = binarized.flatten()
            byte_frame = bytearray(flat)
            sock.sendto(byte_frame, (UDP_IP, UDP_PORT))
            
            # Visualization
            viz = cv2.resize(binarized * 255, (280, 280), interpolation=cv2.INTER_NEAREST)
            cv2.imshow("Nursery Sensor View (What the AI Sees)", viz)
            
            if cv2.waitKey(int(1000/fps)) & 0xFF == ord('q'):
                break
                
            frame_count += 1
            if frame_count % 100 == 0:
                print(f"\r[Sensor] Sent {frame_count} frames...", end="", flush=True)
                
    except KeyboardInterrupt:
        pass
    finally:
        print("\n[Sensor] Stopped.")
        cv2.destroyAllWindows()

if __name__ == "__main__":
    if not HAS_DEPS:
        print("ERROR: Missing dependencies. Please run:")
        print("pip install mss opencv-python numpy")
        sys.exit(1)
        
    parser = argparse.ArgumentParser(description="FP-SAN Nursery Screen Sensor")
    parser.add_argument("--fps", type=int, default=10, help="Target frames per second")
    args = parser.parse_args()

    run_screen_sensor(args.fps)
