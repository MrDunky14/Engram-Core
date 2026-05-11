import socket
import time
import argparse
import sys

# Try to import cv2, fallback to synthetic data if missing
try:
    import cv2
    import numpy as np
    HAS_CV2 = True
except ImportError:
    HAS_CV2 = False

UDP_IP = "127.0.0.1"
UDP_PORT = 5005
FRAME_SIZE = 784 # 28x28

def run_synthetic_sensor(sock, fps=30):
    print("[Sensor] OpenCV not found — synthetic 28x28 stream (Engram Core demo).")
    print("[Sensor] Streaming to UDP %s:%s at %d FPS" % (UDP_IP, UDP_PORT, fps))
    
    # We'll create a simple moving "blob" on a 28x28 grid
    x, y = 14, 14
    dx, dy = 1, 1
    
    frame_count = 0
    try:
        while True:
            # Create empty 28x28
            frame = [0] * FRAME_SIZE
            
            # Draw a 5x5 blob at (x, y)
            for i in range(-2, 3):
                for j in range(-2, 3):
                    px, py = x + i, y + j
                    if 0 <= px < 28 and 0 <= py < 28:
                        frame[py * 28 + px] = 1
            
            # Update blob position
            x += dx
            y += dy
            if x <= 2 or x >= 25: dx *= -1
            if y <= 2 or y >= 25: dy *= -1
            
            # Pack to bytes
            byte_frame = bytearray(frame)
            
            # Send
            sock.sendto(byte_frame, (UDP_IP, UDP_PORT))
            
            frame_count += 1
            if frame_count % 100 == 0:
                print(f"\r[Sensor] Sent {frame_count} frames...", end="", flush=True)
                
            time.sleep(1.0 / fps)
    except KeyboardInterrupt:
        print("\n[Sensor] Stopped.")

def run_webcam_sensor(sock, fps=30):
    print("[Sensor] OpenCV found! Starting LIVE WEBCAM sensor.")
    print("[Sensor] Streaming to UDP %s:%s" % (UDP_IP, UDP_PORT))
    
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("[Sensor] ERROR: Cannot open webcam. Fallback to synthetic.")
        run_synthetic_sensor(sock, fps)
        return

    frame_count = 0
    print("[Sensor] Press 'q' in the preview window to exit.")
    
    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                continue
                
            # 1. Grayscale
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            
            # 2. Resize to 28x28
            resized = cv2.resize(gray, (28, 28), interpolation=cv2.INTER_AREA)
            
            # 3. Binarize (threshold at 128)
            _, binarized = cv2.threshold(resized, 128, 1, cv2.THRESH_BINARY)
            
            # 4. Flatten and convert to bytes
            flat = binarized.flatten()
            byte_frame = bytearray(flat)
            
            # Send
            sock.sendto(byte_frame, (UDP_IP, UDP_PORT))
            
            # Visualization: show what the AI sees (scaled up)
            viz = cv2.resize(binarized * 255, (280, 280), interpolation=cv2.INTER_NEAREST)
            cv2.imshow("FP-SAN AI Vision", viz)
            
            if cv2.waitKey(int(1000/fps)) & 0xFF == ord('q'):
                break
                
            frame_count += 1
            if frame_count % 100 == 0:
                print(f"\r[Sensor] Sent {frame_count} frames...", end="", flush=True)
                
    except KeyboardInterrupt:
        pass
    finally:
        print("\n[Sensor] Stopped.")
        cap.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="FP-SAN UDP Sensor Bus Publisher")
    parser.add_argument("--fps", type=int, default=30, help="Target frames per second")
    parser.add_argument("--force-synth", action="store_true", help="Force synthetic data even if OpenCV is present")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    if HAS_CV2 and not args.force_synth:
        run_webcam_sensor(sock, args.fps)
    else:
        run_synthetic_sensor(sock, args.fps)
