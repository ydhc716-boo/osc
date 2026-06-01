"""
Serial/TCP communication handler for SCO Web UI.

Supports two modes:
  - Serial: Connect to real STM32 hardware via COM port (USB CDC)
  - TCP:    Connect to the simulator via TCP socket

Both modes share the same binary protocol.
"""

import sys
import os
import time
import socket
import threading
import logging
from abc import ABC, abstractmethod

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulator.protocol import ProtocolParser, crc16, SYNC1, SYNC2

logger = logging.getLogger(__name__)


class DeviceConnection(ABC):
    """Abstract device connection."""

    @abstractmethod
    def connect(self) -> bool:
        """Establish connection. Returns True on success."""
        ...

    @abstractmethod
    def disconnect(self):
        """Close connection."""
        ...

    @abstractmethod
    def send(self, data: bytes) -> bool:
        """Send raw bytes. Returns True on success."""
        ...

    @abstractmethod
    def recv(self, timeout: float = 0.1) -> bytes:
        """Receive available bytes."""
        ...

    @abstractmethod
    def is_connected(self) -> bool:
        ...


class SerialConnection(DeviceConnection):
    """Serial port connection for real hardware."""

    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self._ser = None

    def connect(self) -> bool:
        try:
            import serial
            self._ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            logger.info(f"Serial connected: {self.port} @ {self.baudrate}")
            return True
        except Exception as e:
            logger.error(f"Serial connect failed: {e}")
            return False

    def disconnect(self):
        if self._ser and self._ser.is_open:
            self._ser.close()
            logger.info("Serial disconnected")

    def send(self, data: bytes) -> bool:
        if not self._ser or not self._ser.is_open:
            return False
        try:
            self._ser.write(data)
            return True
        except Exception as e:
            logger.error(f"Serial send error: {e}")
            return False

    def recv(self, timeout: float = 0.1) -> bytes:
        if not self._ser or not self._ser.is_open:
            return b''
        try:
            self._ser.timeout = timeout
            data = self._ser.read(self._ser.in_waiting or 1)
            return data if data else b''
        except Exception:
            return b''

    def is_connected(self) -> bool:
        return self._ser is not None and self._ser.is_open


class TCPConnection(DeviceConnection):
    """TCP connection for simulator."""

    def __init__(self, host: str = '127.0.0.1', port: int = 9876):
        self.host = host
        self.port = port
        self._sock: socket.socket | None = None

    def connect(self) -> bool:
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(2.0)
            self._sock.connect((self.host, self.port))
            self._sock.settimeout(0.1)
            logger.info(f"TCP connected: {self.host}:{self.port}")
            return True
        except Exception as e:
            logger.error(f"TCP connect failed: {e}")
            return False

    def disconnect(self):
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None
            logger.info("TCP disconnected")

    def send(self, data: bytes) -> bool:
        if not self._sock:
            return False
        try:
            self._sock.sendall(data)
            return True
        except Exception as e:
            logger.error(f"TCP send error: {e}")
            return False

    def recv(self, timeout: float = 0.1) -> bytes:
        if not self._sock:
            return b''
        try:
            self._sock.settimeout(timeout)
            data = self._sock.recv(65536)
            return data
        except socket.timeout:
            return b''
        except Exception:
            return b''

    def is_connected(self) -> bool:
        return self._sock is not None


class DeviceHandler:
    """
    Manages connection to device (serial or TCP) and protocol parsing.
    Runs a background reader thread that pushes parsed packets to a callback.
    """

    def __init__(self):
        self._conn: DeviceConnection | None = None
        self._parser = ProtocolParser()
        self._thread: threading.Thread | None = None
        self._running = False
        self._on_packet = None    # callback(cmd, data)
        self._on_disconnect = None
        self._lock = threading.RLock()

    @property
    def connected(self) -> bool:
        return self._conn is not None and self._conn.is_connected()

    def set_callbacks(self, on_packet, on_disconnect=None):
        """Set callbacks: on_packet(cmd, data), on_disconnect()."""
        self._on_packet = on_packet
        self._on_disconnect = on_disconnect

    def connect_serial(self, port: str, baudrate: int = 115200) -> bool:
        """Connect via serial port."""
        with self._lock:
            self._disconnect_locked()
            self._conn = SerialConnection(port, baudrate)
            if not self._conn.connect():
                self._conn = None
                return False
            self._start_reader_locked()
            return True

    def connect_tcp(self, host: str = '127.0.0.1', port: int = 9876) -> bool:
        """Connect via TCP (simulator)."""
        with self._lock:
            self._disconnect_locked()
            self._conn = TCPConnection(host, port)
            if not self._conn.connect():
                self._conn = None
                return False
            self._start_reader_locked()
            return True

    def disconnect(self):
        """Disconnect and stop reader thread."""
        with self._lock:
            self._disconnect_locked()

    def send(self, data: bytes) -> bool:
        """Send raw packet bytes."""
        with self._lock:
            if not self._conn or not self._conn.is_connected():
                return False
            return self._conn.send(data)

    def _disconnect_locked(self):
        """Internal: disconnect without acquiring lock (caller holds lock)."""
        self._running = False
        if self._conn:
            self._conn.disconnect()
            self._conn = None
        self._parser = ProtocolParser()

    def _start_reader_locked(self):
        """Internal: start reader thread (caller holds lock)."""
        if self._thread and self._thread.is_alive():
            # Release lock while joining old thread to avoid deadlock
            self._lock.release()
            try:
                self._thread.join(timeout=1.0)
            finally:
                self._lock.acquire()
        self._running = True
        self._thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._thread.start()

    def _reader_loop(self):
        """Background thread: read from device, parse packets, invoke callback."""
        while self._running:
            with self._lock:
                conn = self._conn
            if conn is None or not conn.is_connected():
                time.sleep(0.01)
                continue
            try:
                data = conn.recv(timeout=0.05)
            except Exception:
                data = b''
            if data:
                packets = self._parser.feed(data)
                for cmd, data_bytes in packets:
                    if self._on_packet:
                        try:
                            self._on_packet(cmd, data_bytes)
                        except Exception as e:
                            logger.error(f"Packet callback error: {e}")
            else:
                time.sleep(0.005)  # 5ms idle

        # If we exited because connection dropped
        with self._lock:
            still_connected = self._conn and self._conn.is_connected()
        if self._on_disconnect and not still_connected:
            self._on_disconnect()


def list_serial_ports() -> list:
    """List available serial ports."""
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        return [{'device': p.device, 'description': p.description} for p in ports]
    except ImportError:
        return []
    except Exception:
        return []


# ── Convenience: packet builders (re-export from protocol) ──

from simulator.protocol import (
    pack_set_waveform, pack_set_freq, pack_set_amplitude,
    pack_set_sample_rate, pack_set_trigger,
    pack_start_sg, pack_stop_sg, pack_start_scope, pack_stop_scope,
    pack_get_status,
    RSP_STATUS, RSP_SCOPE_DATA, RSP_ERROR,
    WAVEFORM_SINE, WAVEFORM_SQUARE, WAVEFORM_TRIANGLE, WAVEFORM_SAWTOOTH,
    TRIG_AUTO, TRIG_RISING, TRIG_FALLING,
)
