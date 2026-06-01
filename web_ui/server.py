"""
SCO Web UI Server — Flask + Flask-SocketIO backend.

Provides:
  - REST API for device control (signal generator, oscilloscope)
  - WebSocket for real-time scope data streaming
  - Serial port listing

Usage:
    python web_ui/server.py [--port 5000] [--debug]
"""

import sys
import os
import struct
import logging
import argparse

# Ensure project root is importable
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO, emit

from simulator.protocol import (
    RSP_STATUS, RSP_SCOPE_DATA, RSP_ERROR,
    WAVEFORM_SINE, WAVEFORM_SQUARE, WAVEFORM_TRIANGLE, WAVEFORM_SAWTOOTH,
)
from web_ui.serial_handler import (
    DeviceHandler, list_serial_ports,
    pack_set_waveform, pack_set_freq, pack_set_amplitude,
    pack_set_sample_rate, pack_set_trigger,
    pack_start_sg, pack_stop_sg, pack_start_scope, pack_stop_scope,
    pack_get_status,
)

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
)
logger = logging.getLogger('web_ui')

# ── App Setup ─────────────────────────────────────────────────────────────

app = Flask(__name__)
app.config['SECRET_KEY'] = 'sco-secret-key-change-in-production'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading',
                    ping_timeout=10, ping_interval=5)

# global device handler
dev = DeviceHandler()

# Track latest device state for polling
_device_state = {
    'sg_on': False, 'scope_on': False,
    'waveform': 0, 'freq_hz': 1000, 'amp_mv': 3300,
    'sample_rate': 100000, 'trigger_mode': 0, 'trigger_level_mv': 1650,
}


# ── Packet Callback ────────────────────────────────────────────────────────

def on_device_packet(cmd: int, data: bytes):
    """Called from device reader thread when a packet arrives."""
    if cmd == RSP_STATUS and len(data) >= 13:
        try:
            sg_on    = data[0] != 0
            scope_on = data[1] != 0
            waveform = data[2]
            freq_hz  = struct.unpack('<I', data[3:7])[0]
            amp_mv   = struct.unpack('<H', data[7:9])[0]
            trig_mode = data[9]
            trig_lvl  = struct.unpack('<H', data[10:12])[0]
            samp_rate = struct.unpack('<I', data[12:16])[0] if len(data) >= 16 else 0

            _device_state.update({
                'sg_on': sg_on, 'scope_on': scope_on,
                'waveform': waveform, 'freq_hz': freq_hz, 'amp_mv': amp_mv,
                'sample_rate': samp_rate or _device_state['sample_rate'],
                'trigger_mode': trig_mode, 'trigger_level_mv': trig_lvl,
            })
            socketio.emit('status', _device_state)
        except Exception as e:
            logger.error(f"Status parse error: {e}")

    elif cmd == RSP_SCOPE_DATA and len(data) >= 4:
        try:
            seq   = struct.unpack('<H', data[0:2])[0]
            count = struct.unpack('<H', data[2:4])[0]
            samples = []
            for i in range(count):
                offset = 4 + i * 2
                if offset + 1 < len(data):
                    val = struct.unpack('<H', data[offset:offset+2])[0]
                    samples.append(val)
            if samples:
                socketio.emit('scope_data', {
                    'seq': seq,
                    'samples': samples,
                })
        except Exception as e:
            logger.error(f"Scope data parse error: {e}")

    elif cmd == RSP_ERROR and len(data) >= 1:
        logger.warning(f"Device error: 0x{data[0]:02X}")
        socketio.emit('device_error', {'code': data[0]})


def on_device_disconnect():
    """Called when device disconnects unexpectedly."""
    _device_state['sg_on'] = False
    _device_state['scope_on'] = False
    socketio.emit('status', _device_state)
    socketio.emit('device_disconnected', {})


dev.set_callbacks(on_device_packet, on_device_disconnect)


# ── Routes ─────────────────────────────────────────────────────────────────

@app.route('/')
def index():
    """Serve the main page."""
    return render_template('index.html')


# ── Connection API ────────────────────────────────────────────────────────

@app.route('/api/ports', methods=['GET'])
def api_ports():
    """List available serial ports."""
    ports = list_serial_ports()
    # Add simulator option
    result = {
        'serial': ports,
        'simulator_available': True,
    }
    return jsonify(result)


@app.route('/api/connect', methods=['POST'])
def api_connect():
    """Connect to device. Body: {mode: 'serial'|'tcp', port?: str, host?: str, tcp_port?: int}"""
    body = request.get_json() or {}
    mode = body.get('mode', 'tcp')

    if dev.connected:
        dev.disconnect()

    if mode == 'serial':
        port = body.get('port', '')
        if not port:
            return jsonify({'success': False, 'error': 'No serial port specified'})
        ok = dev.connect_serial(port)
    else:
        host = body.get('host', '127.0.0.1')
        tcp_port = body.get('tcp_port', 9876)
        ok = dev.connect_tcp(host, tcp_port)

    if ok:
        # Request initial status
        dev.send(pack_get_status())
        return jsonify({'success': True, 'mode': mode})
    return jsonify({'success': False, 'error': 'Connection failed'})


@app.route('/api/disconnect', methods=['POST'])
def api_disconnect():
    """Disconnect from device."""
    dev.disconnect()
    return jsonify({'success': True})


@app.route('/api/status', methods=['GET'])
def api_status():
    """Get current cached device state."""
    return jsonify({
        'connected': dev.connected,
        'state': _device_state,
    })


# ── Signal Generator API ──────────────────────────────────────────────────

@app.route('/api/signal/set_waveform', methods=['POST'])
def api_set_waveform():
    body = request.get_json() or {}
    wtype = body.get('type', 0)
    dev.send(pack_set_waveform(wtype))
    return jsonify({'success': True})


@app.route('/api/signal/set_frequency', methods=['POST'])
def api_set_frequency():
    body = request.get_json() or {}
    freq = body.get('freq', 1000)
    dev.send(pack_set_freq(int(freq)))
    return jsonify({'success': True})


@app.route('/api/signal/set_amplitude', methods=['POST'])
def api_set_amplitude():
    body = request.get_json() or {}
    amp = body.get('amp', 3300)
    dev.send(pack_set_amplitude(int(amp)))
    return jsonify({'success': True})


@app.route('/api/signal/start', methods=['POST'])
def api_signal_start():
    dev.send(pack_start_sg())
    return jsonify({'success': True})


@app.route('/api/signal/stop', methods=['POST'])
def api_signal_stop():
    dev.send(pack_stop_sg())
    return jsonify({'success': True})


# ── Oscilloscope API ──────────────────────────────────────────────────────

@app.route('/api/scope/set_sample_rate', methods=['POST'])
def api_set_sample_rate():
    body = request.get_json() or {}
    rate = body.get('rate', 100000)
    dev.send(pack_set_sample_rate(int(rate)))
    return jsonify({'success': True})


@app.route('/api/scope/set_trigger', methods=['POST'])
def api_set_trigger():
    body = request.get_json() or {}
    mode = body.get('mode', 0)
    level = body.get('level', 1650)
    dev.send(pack_set_trigger(int(mode), int(level)))
    return jsonify({'success': True})


@app.route('/api/scope/start', methods=['POST'])
def api_scope_start():
    dev.send(pack_start_scope(512))
    return jsonify({'success': True})


@app.route('/api/scope/stop', methods=['POST'])
def api_scope_stop():
    dev.send(pack_stop_scope())
    return jsonify({'success': True})


# ── WebSocket Events ──────────────────────────────────────────────────────

@socketio.on('connect')
def on_ws_connect():
    """Client connected via WebSocket."""
    logger.info("WebSocket client connected")
    emit('status', _device_state)
    emit('connected_info', {'connected': dev.connected})


@socketio.on('disconnect')
def on_ws_disconnect():
    logger.info("WebSocket client disconnected")


@socketio.on('request_status')
def on_request_status():
    """Client requests fresh status."""
    if dev.connected:
        dev.send(pack_get_status())
    emit('status', _device_state)


# ── Main ──────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='SCO Web UI Server')
    parser.add_argument('--port', type=int, default=5000, help='HTTP port (default: 5000)')
    parser.add_argument('--host', type=str, default='127.0.0.1', help='Bind address')
    parser.add_argument('--debug', action='store_true', help='Debug mode')
    args = parser.parse_args()

    logger.info("═" * 50)
    logger.info(f"SCO Web UI Server starting on http://{args.host}:{args.port}")
    logger.info(f"Open your browser to view the interface")
    logger.info(f"═" * 50)

    socketio.run(app, host=args.host, port=args.port, debug=args.debug,
                 allow_unsafe_werkzeug=True)


if __name__ == '__main__':
    main()
