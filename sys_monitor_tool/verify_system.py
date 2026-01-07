import socket
import json
import time

def verify():
    print("Testing connection to C Engine...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5)
        s.connect(('127.0.0.1', 5000))
        print("Connected.")
        
        data = s.recv(1024).decode('utf-8')
        print(f"Received raw data: {data.strip()}")
        
        for line in data.split('\n'):
            if not line: continue
            try:
                metrics = json.loads(line)
                print("Parsed JSON successfully:")
                print(json.dumps(metrics, indent=2))
                
                assert "cpu" in metrics
                assert "ram_percent" in metrics
                
                print("\n[OK] VERIFICATION PASSED: C Engine is streaming valid system metrics.")
                return
            except json.JSONDecodeError:
                continue
                
        print("[FAIL] VERIFICATION FAILED: Could not decode JSON.")
        
    except Exception as e:
        print(f"[FAIL] VERIFICATION FAILED: {e}")
    finally:
        s.close()

if __name__ == "__main__":
    verify()
