"""
SCO Hardware Debug Tool — Direct serial communication test.
Tests if the STM32 firmware responds to protocol commands via USB CDC.
"""
import sys, os, struct, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simulator.protocol import (
    pack_set_waveform, pack_set_freq, pack_set_amplitude,
    pack_start_sg, pack_stop_sg, pack_start_scope, pack_stop_scope,
    pack_get_status, ProtocolParser,
    CMD_SET_WAVEFORM, CMD_SET_FREQ, CMD_SET_AMPLITUDE,
    CMD_START_SG, RSP_STATUS, RSP_SCOPE_DATA, RSP_ERROR,
)

PORT = input("Enter COM port (e.g. COM7): ").strip()

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

print(f"Opening {PORT}...")
ser = serial.Serial(PORT, 115200, timeout=0.5)
print(f"Connected! Sending GET_STATUS...")

# Test 1: GET_STATUS
packet = pack_get_status()
print(f"  Sending {len(packet)} bytes: {packet.hex()}")
ser.write(packet)
time.sleep(0.3)

# Read response
parser = ProtocolParser()
raw = ser.read(2048)
print(f"  Received {len(raw)} bytes: {raw.hex() if raw else '(empty)'}")

if raw:
    packets = parser.feed(raw)
    for cmd, data in packets:
        if cmd == RSP_STATUS:
            print(f"  ✅ STATUS received! {len(data)} bytes")
            sg_on = data[0]; scope_on = data[1]; wave = data[2]
            freq = struct.unpack('<I', data[3:7])[0]
            amp = struct.unpack('<H', data[7:9])[0]
            print(f"     SG:{sg_on} Scope:{scope_on} Wave:{wave} Freq:{freq}Hz Amp:{amp}mV")
            trig = data[9]; trig_lvl = struct.unpack('<H', data[10:12])[0]
            sr = struct.unpack('<I', data[12:16])[0]
            print(f"     Trig:{trig} TrigLvl:{trig_lvl}mV SampleRate:{sr}SPS")
        elif cmd == RSP_ERROR:
            print(f"  ❌ Error code: 0x{data[0]:02X}")
        else:
            print(f"  ? Unknown cmd: 0x{cmd:02X}, data: {data.hex()}")
else:
    print("  ❌ NO RESPONSE — trying again with longer timeout...")
    ser.timeout = 2.0
    ser.write(pack_get_status())
    time.sleep(0.5)
    raw = ser.read(2048)
    if raw:
        print(f"  Got {len(raw)} bytes delayed: {raw.hex()}")
        packets = parser.feed(raw)
        for cmd, data in packets:
            if cmd == RSP_STATUS:
                print("  ✅ STATUS received on retry!")
            else:
                print(f"  cmd=0x{cmd:02X} data={data.hex()}")
    else:
        print("  ❌ STILL NO RESPONSE")

# Test 2: START SG if status worked
print("\nTest 2: START SG...")
ser.timeout = 0.5
ser.write(pack_start_sg())
time.sleep(0.2)
ser.write(pack_get_status())
time.sleep(0.3)
raw = ser.read(2048)
if raw:
    parser2 = ProtocolParser()
    packets = parser2.feed(raw)
    for cmd, data in packets:
        if cmd == RSP_STATUS:
            print(f"  ✅ SG status: running={data[0]}")
else:
    print("  ❌ No response to START_SG")

# Test 3: START SCOPE
print("\nTest 3: START SCOPE...")
ser.write(pack_start_scope(512))
time.sleep(0.5)
raw = ser.read(4096)
if raw:
    parser3 = ProtocolParser()
    packets = parser3.feed(raw)
    scope_count = 0
    for cmd, data in packets:
        if cmd == RSP_STATUS:
            print(f"  ✅ STATUS: scope_on={data[1]}")
        elif cmd == RSP_SCOPE_DATA:
            scope_count += 1
            if scope_count <= 2:
                seq = struct.unpack('<H', data[0:2])[0]
                cnt = struct.unpack('<H', data[2:4])[0]
                print(f"  📊 SCOPE DATA seq={seq} count={cnt}")
    if scope_count > 0:
        print(f"  ✅ Got {scope_count} scope data packets!")
    else:
        print("  ❌ No scope data received")
else:
    print("  ❌ No data received")

# Cleanup
ser.write(pack_stop_sg())
ser.write(pack_stop_scope())
time.sleep(0.1)
ser.close()
print("\n✅ Test complete.")
