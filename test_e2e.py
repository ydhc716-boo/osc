"""End-to-end test: start DDS+scope → verify sine → stop DDS → verify flat line."""
import sys, os, time, socket, struct
sys.path.insert(0, '.')
from simulator.protocol import *

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(('127.0.0.1', 9876))
print("Connected")

# Start signal generator (sine, 2kHz)
sock.send(pack_set_waveform(0))
time.sleep(0.05)
sock.send(pack_start_sg())
time.sleep(0.05)

# Start oscilloscope (50kSPS)
sock.send(pack_set_sample_rate(50000))
time.sleep(0.05)
sock.send(pack_start_scope(256))
time.sleep(0.05)

# Read scope data for ~0.5s — should be sine wave
parser = ProtocolParser()
buf = b''
vpp_values = []
for _ in range(40):
    try:
        buf += sock.recv(65536)
    except socket.timeout:
        pass
    time.sleep(0.015)

packets = parser.feed(buf)
for cmd, data in packets:
    if cmd == RSP_SCOPE_DATA:
        _, count = struct.unpack('<HH', data[:4])
        vals = [struct.unpack('<H', data[4+i*2:6+i*2])[0] for i in range(min(count, 10))]
        vpp = max(vals) - min(vals)
        vpp_values.append(vpp)

if vpp_values:
    avg_vpp = sum(vpp_values) / len(vpp_values)
    print(f"Before STOP_SG: avg Vpp = {avg_vpp:.0f} ADC codes (should be >> 0 for sine)")
else:
    print("No scope data received before STOP_SG")

# Now stop the signal generator (scope keeps running)
sock.send(pack_stop_sg())
print("Sent STOP_SG")
time.sleep(0.1)

# Read scope data for ~0.5s — should be flat (all mid-scale ~2047)
buf = b''
for _ in range(40):
    try:
        buf += sock.recv(65536)
    except socket.timeout:
        pass
    time.sleep(0.015)

flat_vpp = []
parser2 = ProtocolParser()
packets = parser2.feed(buf)
for cmd, data in packets:
    if cmd == RSP_SCOPE_DATA:
        _, count = struct.unpack('<HH', data[:4])
        vals = [struct.unpack('<H', data[4+i*2:6+i*2])[0] for i in range(min(count, 10))]
        vpp = max(vals) - min(vals)
        flat_vpp.append(vpp)

if flat_vpp:
    avg_flat = sum(flat_vpp) / len(flat_vpp)
    print(f"After  STOP_SG: avg Vpp = {avg_flat:.0f} ADC codes (should be ~0 for flat)")
    if avg_flat < 5:
        print("PASS: Flat line after SG stop")
    else:
        print(f"FAIL: Still getting varying waveform (Vpp={avg_flat:.0f})")
else:
    print("No scope data received after STOP_SG")

sock.close()
