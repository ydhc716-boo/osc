/**
 * SCO Web UI — Control Handlers
 *
 * UI event bindings for connection, signal generator, and oscilloscope controls.
 */

// Dependencies: App (app.js), apiPost/apiGet (websocket.js)

App.initControls = function () {
    // ── Connection ─────────────────────────────────────────────────────
    const $btnConnect = document.getElementById('btn-connect');
    const $btnDisconnect = document.getElementById('btn-disconnect');
    const $connectModal = document.getElementById('connect-modal');
    const $btnConfirm = document.getElementById('btn-connect-confirm');
    const $btnCancel = document.getElementById('btn-connect-cancel');
    const $connMode = document.getElementById('conn-mode');
    const $tcpSettings = document.getElementById('tcp-settings');
    const $serialSettings = document.getElementById('serial-settings');

    $btnConnect.addEventListener('click', () => {
        $connectModal.style.display = 'flex';
        loadSerialPorts();
        updateModeUI();
    });

    $btnCancel.addEventListener('click', () => {
        $connectModal.style.display = 'none';
    });

    $connMode.addEventListener('change', updateModeUI);

    function updateModeUI() {
        const mode = $connMode.value;
        $tcpSettings.style.display = mode === 'tcp' ? 'block' : 'none';
        $serialSettings.style.display = mode === 'serial' ? 'block' : 'none';
    }

    $btnConfirm.addEventListener('click', async () => {
        const mode = $connMode.value;
        let body = { mode };
        if (mode === 'tcp') {
            body.host = document.getElementById('tcp-host').value;
            body.tcp_port = parseInt(document.getElementById('tcp-port').value) || 9876;
        } else {
            body.port = document.getElementById('serial-port-list').value;
            body.baudrate = parseInt(document.getElementById('serial-baud').value) || 115200;
        }
        const result = await apiPost('/api/connect', body);
        if (result.success) {
            App.setConnected(true, mode);
            $connectModal.style.display = 'none';
            // Request fresh status
            await apiPost('/api/status');
        } else {
            alert('连接失败: ' + (result.error || '未知错误'));
        }
    });

    $btnDisconnect.addEventListener('click', async () => {
        await apiPost('/api/disconnect');
        App.setConnected(false);
    });

    // ── Signal Generator ───────────────────────────────────────────────
    const $waveBtns = document.querySelectorAll('.wave-btn');
    $waveBtns.forEach(btn => {
        btn.addEventListener('click', async () => {
            $waveBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            const waveType = parseInt(btn.dataset.wave);
            await apiPost('/api/signal/set_waveform', { type: waveType });
        });
    });

    const $sgFreq = document.getElementById('sg-freq');
    const $sgFreqNum = document.getElementById('sg-freq-num');

    function updateFreqDisplay(freq) {
        document.getElementById('freq-display').textContent =
            freq >= 1000 ? (freq / 1000).toFixed(freq % 1000 === 0 ? 0 : 1) + ' kHz'
                         : freq + ' Hz';
    }

    function setFreq(freq, source) {
        freq = Math.max(1, Math.min(100000, parseInt(freq) || 1000));
        if (source !== 'slider') { $sgFreq.value = freq; }
        if (source !== 'num')     { $sgFreqNum.value = freq; }
        updateFreqDisplay(freq);
    }

    $sgFreq.addEventListener('input', () => setFreq(parseInt($sgFreq.value), 'slider'));
    $sgFreq.addEventListener('change', async () => {
        const freq = parseInt($sgFreq.value);
        await apiPost('/api/signal/set_frequency', { freq });
    });

    $sgFreqNum.addEventListener('input', () => setFreq(parseInt($sgFreqNum.value), 'num'));
    $sgFreqNum.addEventListener('change', async () => {
        const freq = parseInt($sgFreqNum.value);
        setFreq(freq, 'num');
        await apiPost('/api/signal/set_frequency', { freq });
    });
    $sgFreqNum.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { $sgFreqNum.blur(); }
    });

    // ── Amplitude ──────────────────────────────────────────────────────
    const $sgAmp = document.getElementById('sg-amp');
    const $sgAmpNum = document.getElementById('sg-amp-num');

    function updateAmpDisplay(amp) {
        document.getElementById('amp-display').textContent = amp.toLocaleString() + ' mV';
    }

    function setAmp(amp, source) {
        amp = Math.max(0, Math.min(3300, parseInt(amp) || 0));
        amp = Math.round(amp / 10) * 10; // snap to 10mV steps
        if (source !== 'slider') { $sgAmp.value = amp; }
        if (source !== 'num')    { $sgAmpNum.value = amp; }
        updateAmpDisplay(amp);
    }

    $sgAmp.addEventListener('input', () => setAmp(parseInt($sgAmp.value), 'slider'));
    $sgAmp.addEventListener('change', async () => {
        const amp = parseInt($sgAmp.value);
        await apiPost('/api/signal/set_amplitude', { amp });
    });

    $sgAmpNum.addEventListener('input', () => setAmp(parseInt($sgAmpNum.value), 'num'));
    $sgAmpNum.addEventListener('change', async () => {
        const amp = parseInt($sgAmpNum.value);
        setAmp(amp, 'num');
        await apiPost('/api/signal/set_amplitude', { amp });
    });
    $sgAmpNum.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { $sgAmpNum.blur(); }
    });

    const $btnSgStart = document.getElementById('btn-sg-start');
    const $btnSgStop = document.getElementById('btn-sg-stop');
    $btnSgStart.addEventListener('click', async () => {
        await apiPost('/api/signal/start');
        $btnSgStart.style.display = 'none';
        $btnSgStop.style.display = 'inline-block';
    });
    $btnSgStop.addEventListener('click', async () => {
        await apiPost('/api/signal/stop');
        $btnSgStart.style.display = 'inline-block';
        $btnSgStop.style.display = 'none';
    });

    // ── Oscilloscope ───────────────────────────────────────────────────
    const $scopeRate = document.getElementById('scope-rate');
    $scopeRate.addEventListener('change', async () => {
        const rate = parseInt($scopeRate.value);
        document.getElementById('rate-display').textContent = rate.toLocaleString() + ' SPS';
        await apiPost('/api/scope/set_sample_rate', { rate });
    });

    const $scopeTrigger = document.getElementById('scope-trigger');
    $scopeTrigger.addEventListener('change', async () => {
        const mode = parseInt($scopeTrigger.value);
        const level = parseInt(document.getElementById('scope-trig-level').value);
        await apiPost('/api/scope/set_trigger', { mode, level });
    });

    const $scopeTrigLevel = document.getElementById('scope-trig-level');
    $scopeTrigLevel.addEventListener('input', () => {
        const level = parseInt($scopeTrigLevel.value);
        document.getElementById('trig-display').textContent = level.toLocaleString() + ' mV';
    });
    $scopeTrigLevel.addEventListener('change', async () => {
        const mode = parseInt($scopeTrigger.value);
        const level = parseInt($scopeTrigLevel.value);
        await apiPost('/api/scope/set_trigger', { mode, level });
    });

    const $btnScopeStart = document.getElementById('btn-scope-start');
    const $btnScopeStop = document.getElementById('btn-scope-stop');
    $btnScopeStart.addEventListener('click', async () => {
        await apiPost('/api/scope/start');
        $btnScopeStart.style.display = 'none';
        $btnScopeStop.style.display = 'inline-block';
    });
    $btnScopeStop.addEventListener('click', async () => {
        await apiPost('/api/scope/stop');
        $btnScopeStart.style.display = 'inline-block';
        $btnScopeStop.style.display = 'none';
    });

    // ── Time/Div & Volt/Div (client-side only, no device command) ────
    const $scopeTimeDiv = document.getElementById('scope-time-div');
    $scopeTimeDiv.addEventListener('change', () => {
        const val = parseInt($scopeTimeDiv.value);
        const label = val >= 1000 ? (val / 1000) + ' ms/div' : val + ' µs/div';
        document.getElementById('time-div-display').textContent = label;
    });

    const $scopeVoltDiv = document.getElementById('scope-volt-div');
    $scopeVoltDiv.addEventListener('change', () => {
        const val = parseInt($scopeVoltDiv.value);
        const label = val >= 1000 ? (val / 1000).toFixed(1) + ' V/div' : val + ' mV/div';
        document.getElementById('volt-div-display').textContent = label;
    });

    // ── Keyboard shortcuts ─────────────────────────────────────────────
    document.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
        switch (e.key.toLowerCase()) {
            case 's':
                if (e.ctrlKey) {
                    e.preventDefault();
                    document.getElementById('btn-sg-start').click();
                }
                break;
            case 't':
                if (e.ctrlKey) {
                    e.preventDefault();
                    document.getElementById('btn-sg-stop').click();
                }
                break;
            case 'o':
                if (e.ctrlKey) {
                    e.preventDefault();
                    document.getElementById('btn-scope-start').click();
                }
                break;
            case 'p':
                if (e.ctrlKey) {
                    e.preventDefault();
                    document.getElementById('btn-scope-stop').click();
                }
                break;
        }
    });
};

// ── Sync UI from device state ──────────────────────────────────────────

function syncControlsFromState(state) {
    // Signal generator
    if (state.sg_on !== undefined) {
        const $btnSgStart = document.getElementById('btn-sg-start');
        const $btnSgStop = document.getElementById('btn-sg-stop');
        if (state.sg_on) {
            if ($btnSgStart) $btnSgStart.style.display = 'none';
            if ($btnSgStop) $btnSgStop.style.display = 'inline-block';
        } else {
            if ($btnSgStart) $btnSgStart.style.display = 'inline-block';
            if ($btnSgStop) $btnSgStop.style.display = 'none';
        }
    }

    if (state.waveform !== undefined) {
        document.querySelectorAll('.wave-btn').forEach(b => {
            b.classList.toggle('active', parseInt(b.dataset.wave) === state.waveform);
        });
    }

    if (state.freq_hz !== undefined) {
        document.getElementById('sg-freq').value = state.freq_hz;
        document.getElementById('sg-freq-num').value = state.freq_hz;
        document.getElementById('freq-display').textContent =
            state.freq_hz >= 1000
                ? (state.freq_hz / 1000).toFixed(state.freq_hz % 1000 === 0 ? 0 : 1) + ' kHz'
                : state.freq_hz + ' Hz';
    }

    if (state.amp_mv !== undefined) {
        document.getElementById('sg-amp').value = state.amp_mv;
        document.getElementById('sg-amp-num').value = state.amp_mv;
        document.getElementById('amp-display').textContent =
            state.amp_mv.toLocaleString() + ' mV';
    }

    // Oscilloscope
    if (state.scope_on !== undefined) {
        const $btnScopeStart = document.getElementById('btn-scope-start');
        const $btnScopeStop = document.getElementById('btn-scope-stop');
        if (state.scope_on) {
            if ($btnScopeStart) $btnScopeStart.style.display = 'none';
            if ($btnScopeStop) $btnScopeStop.style.display = 'inline-block';
        } else {
            if ($btnScopeStart) $btnScopeStart.style.display = 'inline-block';
            if ($btnScopeStop) $btnScopeStop.style.display = 'none';
        }
    }

    if (state.sample_rate !== undefined) {
        document.getElementById('scope-rate').value = state.sample_rate;
        document.getElementById('rate-display').textContent =
            state.sample_rate.toLocaleString() + ' SPS';
    }
}

// ── Serial port loading ────────────────────────────────────────────────

async function loadSerialPorts() {
    try {
        const resp = await apiGet('/api/ports');
        const select = document.getElementById('serial-port-list');
        select.innerHTML = '';
        if (resp.serial && resp.serial.length > 0) {
            resp.serial.forEach(p => {
                const opt = document.createElement('option');
                opt.value = p.device;
                opt.textContent = `${p.device} — ${p.description}`;
                select.appendChild(opt);
            });
        } else {
            const opt = document.createElement('option');
            opt.value = '';
            opt.textContent = '未检测到串口';
            select.appendChild(opt);
        }
    } catch (err) {
        console.error('Failed to load serial ports:', err);
    }
}
