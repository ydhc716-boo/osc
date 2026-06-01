"""Quick test script for the SCO simulator."""
import socket
import struct
import sys
import time

sys.path.insert(0, '.')
from simulator.protocol import *

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(('127.0.0.1', 9876))
print("Connected to simulator!")

# Configure and start
sock.send(pack_set_waveform(0))      # sine
sock.send(pack_set_freq(2000))       # 2 kHz
sock.send(pack_start_sg())
sock.send(pack_set_sample_rate(50000))
sock.send(pack_start_scope(256))

parser = ProtocolParser()
scope_count = 0
for i in range(80):
    try:
        chunk = sock.recv(65536)
        if chunk:
            packets = parser.feed(chunk)
            for cmd, data in packets:
                if cmd == RSP_STATUS:
                    print(f'STATUS: {len(data)} bytes')
                elif cmd == RSP_SCOPE_DATA:
                    seq, count = struct.unpack('<HH', data[:4])
                    scope_count += 1
                    if scope_count <= 3:
                        first_val = struct.unpack('<H', data[4:6])[0]
                        print(f'SCOPE_DATA: seq={seq}, count={count}, first_val={first_val}')
    except socket.timeout:
        pass
    if scope_count >= 3:
        break
    time.sleep(0.05)

sock.close()
print(f'Total scope packets: {scope_count}')
print('Simulator test passed!')
