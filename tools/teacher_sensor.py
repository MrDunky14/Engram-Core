import socket
import time
import argparse
import sys

try:
    import pyautogui
    HAS_DEPS = True
except ImportError:
    HAS_DEPS = False

UDP_IP = "127.0.0.1"
UDP_PORT_LANG = 5006 # New port specifically for Language Spikes

def run_teacher_sensor():
    print("================================================================")
    print(" Engram Core — teacher protocol (cross-modal UDP demo)")
    print("================================================================")
    print(f"[Teacher] UDP Language Port: {UDP_PORT_LANG}")
    print("[Teacher] Make sure Notepad is open and the Screen Sensor is running.")
    print("[Teacher] Also make sure Engram Core is running (build\\engram.exe).")
    print("\nInstructions:")
    print("1. Click inside your Notepad window so it has focus.")
    print("2. Type a word below and press Enter.")
    print("3. The Teacher will automatically type the word into Notepad (Vision).")
    print("4. At the same time, it sends the word over UDP (Language).")
    print("5. The cognitive graph links vision + language spikes from this session.")
    print("\nType 'quit' to exit.\n")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    while True:
        word = input("Teacher> ")
        if word.lower() == 'quit':
            break
        if not word.strip():
            continue
            
        word = word.upper()
        
        # 1. Send Language Spikes over UDP
        # We just send the ASCII bytes. The C++ LanguageCortex will decode and tokenize.
        byte_frame = bytearray(word, 'ascii')
        sock.sendto(byte_frame, (UDP_IP, UDP_PORT_LANG))
        
        # 2. Provide Vision stimulus
        # Click the mouse to ensure focus (assuming user left mouse in Notepad)
        # Actually, let's just type it out with pyautogui
        print(f"[Teacher] Typing '{word}' into Notepad... (Vision Stimulus)")
        
        # Clear notepad first (Ctrl+A, Backspace)
        pyautogui.hotkey('ctrl', 'a')
        pyautogui.press('backspace')
        time.sleep(0.1)
        
        # Type the new word slowly so the sensor catches it
        pyautogui.write(word, interval=0.05)
        
        print(f"[Teacher] '{word}' sent to both Vision and Language Cortices.\n")

if __name__ == "__main__":
    if not HAS_DEPS:
        print("ERROR: Missing dependencies. Please run:")
        print("pip install pyautogui")
        sys.exit(1)

    run_teacher_sensor()
