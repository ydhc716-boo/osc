/**
 * SCO Web UI — Main Application
 *
 * Global state and initialization.
 * Dependencies: socket.io (CDN), websocket.js, waveform.js, controls.js
 * Load order: app.js → websocket.js → waveform.js → controls.js
 */

const App = {
    // ── State ──────────────────────────────────────────────────────────
    connected: false,
    deviceState: {
        sg_on: false,
        scope_on: false,
        waveform: 0,
        freq_hz: 1000,
        amp_mv: 3300,
        sample_rate: 100000,
        trigger_mode: 0,
        trigger_level_mv: 1650,
    },

    // Scope data buffer
    scopeBuffer: [],
    scopeSeq: -1,
    droppedPackets: 0,
    totalPackets: 0,

    // Active scope streaming
    scopeActive: false,

    // ── Init ──────────────────────────────────────────────────────────
    init() {
        console.log('SCO App initializing...');

        const steps = [
            ['initUI',       () => this.initUI()],
            ['initSocket',   () => this.initSocket()],
            ['initWaveform', () => this.initWaveform()],
            ['initControls', () => this.initControls()],
        ];

        for (const [name, fn] of steps) {
            try {
                fn();
                console.log(`  ${name}: OK`);
            } catch (e) {
                console.error(`  ${name}: FAILED —`, e.message || e);
            }
        }

        console.log('SCO App ready.');
    },

    initUI() {
        // DOM references
        this.$connStatus = document.getElementById('conn-status');
        this.$btnConnect = document.getElementById('btn-connect');
        this.$btnDisconnect = document.getElementById('btn-disconnect');
        this.$connectModal = document.getElementById('connect-modal');
        this.$connMode = document.getElementById('conn-mode');
        this.$tcpSettings = document.getElementById('tcp-settings');
        this.$serialSettings = document.getElementById('serial-settings');
    },

    // ── Connection Status ─────────────────────────────────────────────
    setConnected(connected, mode) {
        this.connected = connected;
        if (connected) {
            this.$connStatus.textContent = `● 已连接 (${mode || 'TCP'})`;
            this.$connStatus.className = 'conn-badge connected';
            this.$btnConnect.style.display = 'none';
            this.$btnDisconnect.style.display = 'inline-block';
        } else {
            this.$connStatus.textContent = '● 未连接';
            this.$connStatus.className = 'conn-badge disconnected';
            this.$btnConnect.style.display = 'inline-block';
            this.$btnDisconnect.style.display = 'none';
            this.scopeActive = false;
            this.scopeBuffer = [];
            this.updateWaveformDisplay();
        }
    },

    updateDeviceState(state) {
        // Detect signal generator stop: clear buffer so residual waveform disappears
        if (state.sg_on === false && this.deviceState.sg_on === true) {
            this.scopeBuffer = [];
        }

        Object.assign(this.deviceState, state);

        // If scope just started, clear buffer
        if (state.scope_on && !this.scopeActive) {
            this.scopeActive = true;
            this.scopeBuffer = [];
            this.scopeSeq = -1;
            this.droppedPackets = 0;
            this.totalPackets = 0;
        }
        if (state.scope_on === false) {
            this.scopeActive = false;
        }
    },

    addScopeData(seq, samples) {
        if (!this.scopeActive) return;
        this.totalPackets++;

        // Check for dropped packets
        if (this.scopeSeq >= 0) {
            const expected = (this.scopeSeq + 1) & 0xFFFF;
            if (seq !== expected) {
                const missed = (seq - expected) & 0xFFFF;
                this.droppedPackets += missed;
            }
        }
        this.scopeSeq = seq;

        // Convert ADC codes to voltage (mV) and append
        const vRef = 3300; // mV
        const adcMax = 4095;
        for (let i = 0; i < samples.length; i++) {
            const mv = (samples[i] / adcMax) * vRef;
            this.scopeBuffer.push(mv);
        }

        // Keep buffer size reasonable (~2 seconds of data at 100kSPS = 200k points)
        const maxBuffer = 200000;
        if (this.scopeBuffer.length > maxBuffer) {
            this.scopeBuffer = this.scopeBuffer.slice(-maxBuffer);
        }

        // Update waveform display
        this.updateWaveformDisplay();
    },

    updateWaveformDisplay() {
        // Called by waveform.js after it's initialized
        if (typeof renderWaveform === 'function') {
            renderWaveform(this.scopeBuffer, this.deviceState.sample_rate);
        }
        // Update measurements
        if (typeof updateMeasurements === 'function') {
            updateMeasurements(this.scopeBuffer, this.droppedPackets, this.totalPackets);
        }
    },
};

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', () => App.init());
