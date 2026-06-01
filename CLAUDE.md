# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SCO is a USB oscilloscope + signal generator based on STM32G474VET6. It has three layers: firmware (C, STM32 HAL), a Python simulator (TCP server for development without hardware), and a web UI (Flask + Socket.IO + Canvas). All three share a binary protocol (`simulator/protocol.py` ↔ `firmware/Core/Src/protocol.c`).

## Development Workflow (No Hardware Required)

```bash
# 1. Install Python dependencies
pip install -r requirements.txt

# 2. Start the MCU simulator (TCP on 127.0.0.1:9876)
python -m simulator.main --loopback

# 3. Start the web server (another terminal)
python web_ui/server.py

# 4. Open browser → http://127.0.0.1:5000 → Connect → TCP mode → Start SG + Scope
```

**Loopback mode** (`--loopback`) connects the DDS signal generator output directly to the ADC oscilloscope input internally, so changing waveforms is immediately visible.

## Running Tests

```bash
# Quick simulator protocol smoke test
python test_simulator.py

# End-to-end test: start DDS → verify sine → stop DDS → verify flat line
python test_e2e.py
```

Tests require the simulator running on port 9876 first.

## Architecture

### Communication Protocol (`simulator/protocol.py`)

All components speak the same binary protocol. This is the **single source of truth** for packet format, command codes, and CRC16. The web UI and firmware both import/implement it.

- Packets: `[0xAA 0x55] [CMD 1B] [LEN 2B LE] [DATA N] [CRC16 2B LE]`
- PC→MCU commands: `0x01`–`0x0A` (set waveform, freq, amplitude, sample rate, trigger, start/stop SG, start/stop scope, get status)
- MCU→PC responses: `0x80` (STATUS), `0x81` (SCOPE_DATA), `0x82` (ERROR)
- `ProtocolParser` is a stateful streaming parser — it accumulates bytes via `.feed(data)` and returns complete `(cmd, data)` tuples

### Simulator (`simulator/`)

- `main.py` — TCP server, accepts client connections, spawns per-client handler threads
- `dds_sim.py` — 32-bit phase accumulator, 256-point sine LUT, 1 MSPS update rate; square/triangle/sawtooth computed analytically; amplitude scaling around midpoint
- `adc_sim.py` — configurable sample rate (1k–1M SPS), optional DDS loopback source, or standalone test signal with noise
- `MCUSimulator` (in main.py) orchestrates DDS + ADC, processes commands, returns status/scope-data/error responses

### Web UI (`web_ui/`)

- `server.py` — Flask + Socket.IO backend. REST API endpoints for control, WebSocket for real-time data streaming. Uses a global `DeviceHandler` (`serial_handler.py`)
- `serial_handler.py` — Abstract `DeviceConnection` → `SerialConnection` (real hardware via pyserial) and `TCPConnection` (simulator). `DeviceHandler` runs a background reader thread that feeds `ProtocolParser` and fires callbacks. **Re-exports protocol packers** from `simulator.protocol` as convenience
- Frontend: `templates/index.html` served by Flask; `static/js/app.js` initializes socket, waveform renderer, and controls. Load order: app.js → websocket.js → waveform.js → controls.js
- `static/js/waveform.js` — Canvas-based oscilloscope rendering with grid/scales
- `static/js/controls.js` — Signal generator and scope control panel logic
- `static/js/websocket.js` — Socket.IO event handling, scope data buffering

### Firmware (`firmware/`)

- Chip: STM32G474VET6 (Cortex-M4, 170 MHz)
- `main.c` — Full system init (clock, GPIO, DAC, ADC, TIM6, TIM2, DMA, USB CDC), main loop processing USB received commands and streaming scope data
- `dds.c` — DDS signal generator: phase accumulator updated in TIM6 ISR, DAC output via DMA. 256-point sine LUT, real-time square/triangle/sawtooth computation
- `scope.c` — ADC + DMA double-buffering (1024 points per half). TIM2 triggers ADC. Half-complete interrupt triggers data packaging and USB transmission. Trigger modes: auto/rising/falling
- `protocol.c` — CRC16-CCITT, byte-by-byte parser state machine, packet builders
- `usbd_cdc_if.c` — USB CDC ACM receive callback, buffers data for main loop
- `stm32g4xx_it.c` — Interrupt handlers (SysTick, DMA, TIM6)
- `Makefile` — arm-none-eabi-gcc toolchain: `make` builds, `make flash` via st-flash, `make flash-ocd` via OpenOCD
- `CUBEMX_SETUP.md` — Step-by-step CubeMX configuration guide for regenerating HAL boilerplate

### Key Design Details

- **Protocol sync/CRC handling**: The parser skips bytes until it finds `0xAA 0x55`, then validates CRC16-CCITT. CRC mismatch triggers resync (discard one byte, hunt for sync again). This is resilient to partial/corrupt data.
- **Scope data streaming**: Not request-response — once started, scope data is pushed from MCU/simulator to host as fast as it's acquired. The web UI uses Socket.IO to stream samples to the browser.
- **Status response**: Every configuration command returns a full STATUS packet (16 bytes) with all device state. The web UI caches this in `_device_state` and emits via WebSocket to all clients.
- **The protocol constants (command codes, waveform types, trigger modes, error codes) must stay in sync** across `simulator/protocol.py`, `firmware/Core/Inc/protocol.h`, and `firmware/Core/Src/protocol.c`. When adding a new command, update all three files plus the command handlers in `simulator/main.py` and `firmware/Core/Src/main.c`.
