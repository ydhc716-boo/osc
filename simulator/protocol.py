"""
SCO Communication Protocol — shared between simulator, web_ui, and firmware.

Binary packet format:
  [SYNC 0xAA] [SYNC 0x55] [CMD 1B] [LEN_H 1B] [LEN_L 1B] [DATA N bytes] [CRC16 2B LE]

CRC16-CCITT over CMD + LEN_H + LEN_L + DATA.
"""

import struct
import logging

logger = logging.getLogger(__name__)

# ── Constants ─────────────────────────────────────────────────────────────────
SYNC1 = 0xAA
SYNC2 = 0x55

# PC → MCU Commands
CMD_SET_WAVEFORM    = 0x01
CMD_SET_FREQ        = 0x02
CMD_SET_AMPLITUDE   = 0x03
CMD_SET_SAMPLE_RATE = 0x04
CMD_SET_TRIGGER     = 0x05
CMD_START_SG        = 0x06
CMD_STOP_SG         = 0x07
CMD_START_SCOPE     = 0x08
CMD_STOP_SCOPE      = 0x09
CMD_GET_STATUS      = 0x0A

# MCU → PC Responses
RSP_STATUS     = 0x80
RSP_SCOPE_DATA = 0x81
RSP_ERROR      = 0x82

# Waveform types
WAVEFORM_SINE     = 0
WAVEFORM_SQUARE   = 1
WAVEFORM_TRIANGLE = 2
WAVEFORM_SAWTOOTH = 3

# Trigger modes
TRIG_AUTO   = 0
TRIG_RISING = 1
TRIG_FALLING = 2

# Error codes
ERR_NONE        = 0x00
ERR_BAD_CMD     = 0x01
ERR_BAD_PARAM   = 0x02
ERR_BUSY        = 0x03
ERR_CRC         = 0x04
ERR_OVERRUN     = 0x05

# ── CRC16 ─────────────────────────────────────────────────────────────────────
_CRC16_TABLE = None

def _make_crc16_table():
    global _CRC16_TABLE
    if _CRC16_TABLE is not None:
        return _CRC16_TABLE
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
        table.append(crc)
    _CRC16_TABLE = table
    return table

def crc16(data: bytes) -> int:
    """Compute CRC16-CCITT over bytes."""
    table = _make_crc16_table()
    crc = 0xFFFF
    for b in data:
        idx = ((crc >> 8) ^ b) & 0xFF
        crc = ((crc << 8) ^ table[idx]) & 0xFFFF
    return crc

# ── Packet helpers ────────────────────────────────────────────────────────────

def pack_command(cmd: int, data: bytes = b'') -> bytes:
    """Encode a PC→MCU command packet."""
    length = len(data)
    # header: cmd(1B) + len(2B LE)
    header = struct.pack('<BH', cmd, length)
    payload = header + data
    crc = crc16(payload)
    return bytes([SYNC1, SYNC2]) + payload + struct.pack('<H', crc)

def pack_set_waveform(wave_type: int) -> bytes:
    return pack_command(CMD_SET_WAVEFORM, bytes([wave_type]))

def pack_set_freq(freq_hz: float) -> bytes:
    return pack_command(CMD_SET_FREQ, struct.pack('<I', int(freq_hz)))

def pack_set_amplitude(mv: int) -> bytes:
    return pack_command(CMD_SET_AMPLITUDE, struct.pack('<H', mv))

def pack_set_sample_rate(rate_hz: int) -> bytes:
    return pack_command(CMD_SET_SAMPLE_RATE, struct.pack('<I', rate_hz))

def pack_set_trigger(mode: int, level_mv: int) -> bytes:
    return pack_command(CMD_SET_TRIGGER, struct.pack('<BH', mode, level_mv))

def pack_start_sg() -> bytes:
    return pack_command(CMD_START_SG)

def pack_stop_sg() -> bytes:
    return pack_command(CMD_STOP_SG)

def pack_start_scope(samples_per_packet: int = 512) -> bytes:
    return pack_command(CMD_START_SCOPE, struct.pack('<H', samples_per_packet))

def pack_stop_scope() -> bytes:
    return pack_command(CMD_STOP_SCOPE)

def pack_get_status() -> bytes:
    return pack_command(CMD_GET_STATUS)

# ── Parser ────────────────────────────────────────────────────────────────────

class ProtocolParser:
    """Stateful parser for binary protocol stream."""

    def __init__(self):
        self._buf = bytearray()
        self._sync = False

    def feed(self, data: bytes) -> list:
        """Feed raw bytes; returns list of parsed (cmd, data) tuples."""
        self._buf.extend(data)
        packets = []
        while len(self._buf) >= 6:  # min packet: sync2 + cmd + len2 + crc2
            # Find sync
            if not self._sync:
                idx = self._buf.find(bytes([SYNC1, SYNC2]))
                if idx < 0:
                    # Keep last byte in case SYNC1 is at the end
                    keep = min(1, len(self._buf))
                    self._buf = self._buf[-keep:] if keep else bytearray()
                    break
                self._buf = self._buf[idx + 2:]  # strip sync
                self._sync = True

            if len(self._buf) < 4:  # cmd + len_h + len_l + crc16 minimum
                break

            cmd = self._buf[0]
            data_len = self._buf[1] | (self._buf[2] << 8)  # uint16 LE
            total = 3 + data_len + 2  # header(3) + data + crc(2)

            if len(self._buf) < total:
                break

            # Verify CRC
            payload = self._buf[:3 + data_len]
            crc_received = (self._buf[3 + data_len + 1] << 8) | self._buf[3 + data_len]
            crc_calc = crc16(bytes(payload))
            if crc_received != crc_calc:
                logger.warning(f"CRC mismatch: got 0x{crc_received:04X}, calc 0x{crc_calc:04X}")
                self._sync = False
                self._buf = self._buf[1:]  # resync
                continue

            data = bytes(self._buf[3:3 + data_len])
            packets.append((cmd, data))
            self._buf = self._buf[total:]
            self._sync = False  # ready for next packet

        return packets
