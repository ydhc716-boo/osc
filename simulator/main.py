"""
STM32G474VET6 MCU Simulator — TCP Server.

Listens on a TCP port and responds to the SCO binary protocol exactly
as the real firmware would. Use this for development and testing the
web UI without hardware.

Usage:
    python -m simulator.main [--port 9876] [--loopback]
"""

import sys
import os
import argparse
import struct
import socket
import logging
import threading
import time

# Allow running as python -m simulator.main or python simulator/main.py
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from simulator.protocol import (
    SYNC1, SYNC2,
    CMD_SET_WAVEFORM, CMD_SET_FREQ, CMD_SET_AMPLITUDE,
    CMD_SET_SAMPLE_RATE, CMD_SET_TRIGGER,
    CMD_START_SG, CMD_STOP_SG, CMD_START_SCOPE, CMD_STOP_SCOPE,
    CMD_GET_STATUS,
    RSP_STATUS, RSP_SCOPE_DATA, RSP_ERROR,
    ERR_BAD_CMD, ERR_BAD_PARAM,
    ProtocolParser, crc16,
)
from simulator.dds_sim import DDSSignalGenerator
from simulator.adc_sim import ADCScope

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
)
logger = logging.getLogger('simulator')


class MCUSimulator:
    """Complete MCU simulator: DDS signal gen + ADC scope + protocol handler."""

    def __init__(self, loopback: bool = False):
        self.dds = DDSSignalGenerator()
        self.adc = ADCScope()
        self.parser = ProtocolParser()
        self.loopback = loopback

        if loopback:
            self.adc.set_dds_source(self.dds)
            logger.info("Loopback mode: DDS output connected to ADC input")

    def handle_packet(self, cmd: int, data: bytes) -> bytes:
        """Process a command packet, return response bytes or None."""

        try:
            # ── Signal Generator Commands ──
            if cmd == CMD_SET_WAVEFORM:
                if len(data) < 1:
                    return self._error(ERR_BAD_PARAM)
                wtype = data[0]
                if wtype > 3:
                    return self._error(ERR_BAD_PARAM)
                self.dds.configure(waveform=wtype)
                logger.info(f"SG: waveform={['sine','square','triangle','sawtooth'][wtype]}")
                return self._status()

            elif cmd == CMD_SET_FREQ:
                if len(data) < 4:
                    return self._error(ERR_BAD_PARAM)
                freq = struct.unpack('<I', data[:4])[0]
                if freq < 1 or freq > 100_000:
                    return self._error(ERR_BAD_PARAM)
                self.dds.configure(freq_hz=freq)
                logger.info(f"SG: freq={freq} Hz")
                return self._status()

            elif cmd == CMD_SET_AMPLITUDE:
                if len(data) < 2:
                    return self._error(ERR_BAD_PARAM)
                amp = struct.unpack('<H', data[:2])[0]
                if amp > 3300:
                    return self._error(ERR_BAD_PARAM)
                self.dds.configure(amplitude_mv=amp)
                logger.info(f"SG: amplitude={amp} mV")
                return self._status()

            elif cmd == CMD_START_SG:
                self.dds.start()
                logger.info("SG: started")
                return self._status()

            elif cmd == CMD_STOP_SG:
                self.dds.stop()
                logger.info("SG: stopped")
                return self._status()

            # ── Scope Commands ──
            elif cmd == CMD_SET_SAMPLE_RATE:
                if len(data) < 4:
                    return self._error(ERR_BAD_PARAM)
                rate = struct.unpack('<I', data[:4])[0]
                self.adc.configure(sample_rate=rate)
                logger.info(f"Scope: sample_rate={rate} SPS")
                return self._status()

            elif cmd == CMD_SET_TRIGGER:
                if len(data) < 3:
                    return self._error(ERR_BAD_PARAM)
                mode = data[0]
                level = struct.unpack('<H', data[1:3])[0]
                if mode > 2:
                    return self._error(ERR_BAD_PARAM)
                self.adc.configure(trigger_mode=mode, trigger_level_mv=level)
                logger.info(f"Scope: trigger mode={mode}, level={level} mV")
                return self._status()

            elif cmd == CMD_START_SCOPE:
                self.adc.start()
                logger.info("Scope: started")
                return self._status()

            elif cmd == CMD_STOP_SCOPE:
                self.adc.stop()
                logger.info("Scope: stopped")
                return self._status()

            # ── Status ──
            elif cmd == CMD_GET_STATUS:
                return self._status()

            else:
                logger.warning(f"Unknown command: 0x{cmd:02X}")
                return self._error(ERR_BAD_CMD)

        except Exception as e:
            logger.error(f"Error handling cmd 0x{cmd:02X}: {e}")
            return self._error(ERR_BAD_PARAM)

    def get_scope_data(self) -> bytes:
        """Get a scope data packet (call periodically to stream)."""
        if not self.adc.running:
            return None
        seq, samples = self.adc.acquire(512)
        return self._build_scope_data(seq, samples)

    def _status(self) -> bytes:
        dds_status = self.dds.get_status()
        adc_status = self.adc.get_status()
        # Format: sg_on(B) scope_on(B) waveform(B) freq(I) amp(H) trig_mode(B) trig_level(H) sample_rate(I)
        #         1B      1B         1B         4B      2B     1B            2B             4B = 16 bytes
        data = struct.pack(
            '<BBB I H B H I',
            int(dds_status['sg_on']),
            int(adc_status['scope_on']),
            int(dds_status['waveform']),
            int(dds_status['freq_hz']),
            int(dds_status['amp_mv']),
            int(adc_status['trigger_mode']),
            int(adc_status['trigger_level_mv']),
            int(adc_status['sample_rate']),
        )
        payload = bytes([RSP_STATUS]) + struct.pack('<H', len(data)) + data
        c = crc16(payload)
        return bytes([SYNC1, SYNC2]) + payload + struct.pack('<H', c)

    def _build_scope_data(self, seq: int, samples) -> bytes:
        count = len(samples)
        # Data: seq(2) + count(2) + samples(count*2)
        data = struct.pack('<HH', seq, count)
        for s in samples:
            data += struct.pack('<H', int(s))
        payload = bytes([RSP_SCOPE_DATA]) + struct.pack('<H', len(data)) + data
        c = crc16(payload)
        return bytes([SYNC1, SYNC2]) + payload + struct.pack('<H', c)

    def _error(self, code: int) -> bytes:
        payload = bytes([RSP_ERROR]) + struct.pack('<H', 1) + bytes([code])
        c = crc16(payload)
        return bytes([SYNC1, SYNC2]) + payload + struct.pack('<H', c)


def handle_client(conn: socket.socket, addr: tuple, simulator: MCUSimulator):
    """Handle one TCP client connection."""
    logger.info(f"Client connected: {addr}")
    parser = ProtocolParser()
    # Each client gets its own parser; simulator state is shared

    try:
        conn.settimeout(0.05)  # 50ms timeout for scope streaming loop
        buf = bytearray()
        scope_stream_active = False

        while True:
            try:
                data = conn.recv(4096)
                if not data:
                    break
                buf.extend(data)
            except socket.timeout:
                pass  # continue to stream scope data

            # Parse incoming commands
            if len(buf) > 0:
                packets = parser.feed(bytes(buf))
                buf = bytearray()  # parser keeps internal state for partial packets
                # Actually, feed modifies internal buf. We need to handle leftover.
                # Let's process packet by packet differently.
                # For simplicity, feed one byte at a time.
                pass

            # Stream scope data if running
            if simulator.adc.running:
                pkt = simulator.get_scope_data()
                if pkt:
                    try:
                        conn.sendall(pkt)
                        scope_stream_active = True
                    except (BrokenPipeError, ConnectionResetError):
                        break

            # Small sleep to avoid busy-wait when nothing to do
            if not simulator.adc.running and len(buf) == 0:
                time.sleep(0.01)

    except (ConnectionResetError, BrokenPipeError, OSError) as e:
        logger.info(f"Client {addr} disconnected: {e}")
    finally:
        conn.close()
        logger.info(f"Client {addr} connection closed")


def handle_client_simple(conn: socket.socket, addr: tuple, simulator: MCUSimulator):
    """
    Handle one TCP client — simplified version.
    Processes commands and streams scope data.
    """
    logger.info(f"Client connected: {addr}")
    conn.settimeout(0.02)
    parser = ProtocolParser()
    rx_buf = b''

    try:
        while True:
            # Read available data
            try:
                chunk = conn.recv(65536)
                if not chunk:
                    break
                rx_buf += chunk
            except socket.timeout:
                pass

            # Parse all complete packets
            if rx_buf:
                packets = parser.feed(rx_buf)
                rx_buf = b''  # parser keeps internal leftover, but feed consumes what it can
                # Actually, feed returns parsed packets and keeps leftover internally.
                # Let's handle the response sending.
                for cmd, data in packets:
                    resp = simulator.handle_packet(cmd, data)
                    if resp:
                        try:
                            conn.sendall(resp)
                        except (BrokenPipeError, ConnectionResetError):
                            return

            # Stream scope data
            if simulator.adc.running:
                pkt = simulator.get_scope_data()
                if pkt:
                    try:
                        conn.sendall(pkt)
                    except (BrokenPipeError, ConnectionResetError):
                        return

    except (ConnectionResetError, BrokenPipeError, OSError):
        pass
    finally:
        try:
            conn.close()
        except Exception:
            pass
        logger.info(f"Client {addr} disconnected")


def main():
    parser = argparse.ArgumentParser(description='SCO MCU Simulator')
    parser.add_argument('--port', type=int, default=9876, help='TCP port (default: 9876)')
    parser.add_argument('--host', type=str, default='127.0.0.1', help='Bind address')
    parser.add_argument('--loopback', action='store_true',
                        help='Connect DDS output to ADC input internally')
    args = parser.parse_args()

    simulator = MCUSimulator(loopback=args.loopback)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(5)

    logger.info(f"═" * 50)
    logger.info(f"SCO MCU Simulator running on {args.host}:{args.port}")
    if args.loopback:
        logger.info(f"Loopback mode: DDS output → ADC input")
    logger.info(f"Connect the Web UI to TCP mode at this address")
    logger.info(f"═" * 50)

    try:
        while True:
            conn, addr = server.accept()
            t = threading.Thread(target=handle_client_simple,
                                 args=(conn, addr, simulator), daemon=True)
            t.start()
    except KeyboardInterrupt:
        logger.info("Shutting down...")
    finally:
        server.close()


if __name__ == '__main__':
    main()
