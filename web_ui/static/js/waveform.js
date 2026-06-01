/**
 * SCO Web UI — Waveform Renderer
 *
 * High-performance Canvas 2D waveform display.
 * Renders scope data with grid, labels, and waveform trace.
 */

// Dependencies: App (from app.js)

const Waveform = {
    canvas: null,
    ctx: null,
    width: 0,
    height: 0,
    dpr: 1, // device pixel ratio

    // Display settings
    gridColor: '#1a2a3a',
    gridColorMajor: '#253545',
    textColor: '#6e7681',
    waveformColor: '#58a6ff',
    waveformGlow: 'rgba(88, 166, 255, 0.3)',
    triggerColor: '#d2991d',

    // View state
    timePerDiv: 500,     // µs per horizontal division
    voltsPerDiv: 500,    // mV per vertical division
    numHorizDivs: 10,
    numVertDivs: 8,

    vRef: 3300,          // mV reference
    vMid: 1650,          // mV midpoint

    init() {
        this.canvas = document.getElementById('scope-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.dpr = window.devicePixelRatio || 1;
        this.resize();
        window.addEventListener('resize', () => this.resize());
    },

    resize() {
        const container = this.canvas.parentElement;
        if (!container) return;
        const rect = container.getBoundingClientRect();
        this.width = rect.width;
        this.height = rect.height;
        this.canvas.width = this.width * this.dpr;
        this.canvas.height = this.height * this.dpr;
        this.canvas.style.width = this.width + 'px';
        this.canvas.style.height = this.height + 'px';
        this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    },

    show() {
        this.canvas.style.display = 'block';
        const placeholder = document.getElementById('scope-placeholder');
        if (placeholder) placeholder.style.display = 'none';
    },

    hide() {
        this.canvas.style.display = 'none';
        const placeholder = document.getElementById('scope-placeholder');
        if (placeholder) placeholder.style.display = 'flex';
    },

    /**
     * Render the waveform from the buffer data.
     * @param {number[]} buffer - Array of voltage values in mV
     * @param {number} sampleRate - Sample rate in SPS
     */
    render(buffer, sampleRate) {
        if (!buffer || buffer.length === 0) {
            this.hide();
            return;
        }
        this.show();
        this.resize(); // ensure correct size

        const ctx = this.ctx;
        const w = this.width;
        const h = this.height;
        ctx.clearRect(0, 0, w, h);

        // Draw grid
        this.drawGrid(ctx, w, h);

        // Calculate view
        const timeWindow = this.timePerDiv * this.numHorizDivs; // µs
        const samplesPerWindow = Math.floor((timeWindow / 1_000_000) * sampleRate);

        // Show the most recent data that fits the window
        let displaySamples;
        if (buffer.length <= samplesPerWindow) {
            displaySamples = buffer;
        } else {
            displaySamples = buffer.slice(buffer.length - samplesPerWindow);
        }

        const numPoints = displaySamples.length;
        if (numPoints < 2) return;

        // Scale factors
        const xScale = w / numPoints;
        const yMid = h / 2;
        const yScale = h / (this.voltsPerDiv * this.numVertDivs); // pixels per mV

        // Map voltage to y: y = yMid - (mv - vMid) * yScale
        // vMid = 1650mV, voltsPerDiv * numVertDivs = total mV span
        const totalSpan = this.voltsPerDiv * this.numVertDivs; // mV total vertical

        // Draw waveform path
        ctx.beginPath();
        ctx.strokeStyle = this.waveformColor;
        ctx.lineWidth = 1.5;
        ctx.lineJoin = 'round';

        const firstY = yMid - (displaySamples[0] - this.vMid) / totalSpan * h;
        ctx.moveTo(0, firstY);

        // For performance with large buffers, decimate to screen width
        const maxPoints = w * 2; // 2 points per pixel is enough
        let step = 1;
        if (numPoints > maxPoints) {
            step = Math.floor(numPoints / maxPoints);
        }

        for (let i = 1; i < numPoints; i += step) {
            const x = i * xScale;
            const y = yMid - (displaySamples[i] - this.vMid) / totalSpan * h;
            ctx.lineTo(x, y);
        }

        // Always connect to the last point
        const lastX = (numPoints - 1) * xScale;
        const lastY = yMid - (displaySamples[numPoints - 1] - this.vMid) / totalSpan * h;
        ctx.lineTo(lastX, lastY);

        // Optional: glow effect
        ctx.shadowColor = this.waveformGlow;
        ctx.shadowBlur = 4;
        ctx.stroke();
        ctx.shadowBlur = 0;

        // Draw trigger level indicator
        const trigLevel = App.deviceState.trigger_level_mv || this.vMid;
        const trigY = yMid - (trigLevel - this.vMid) / totalSpan * h;
        ctx.beginPath();
        ctx.strokeStyle = this.triggerColor;
        ctx.lineWidth = 0.5;
        ctx.setLineDash([4, 4]);
        ctx.moveTo(0, trigY);
        ctx.lineTo(w, trigY);
        ctx.stroke();
        ctx.setLineDash([]);

        // Draw trigger label
        ctx.fillStyle = this.triggerColor;
        ctx.font = '10px monospace';
        ctx.fillText(`T:${Math.round(trigLevel)}mV`, 4, trigY - 4);
    },

    drawGrid(ctx, w, h) {
        const numH = this.numHorizDivs;
        const numV = this.numVertDivs;
        const dx = w / numH;
        const dy = h / numV;

        // Minor grid
        ctx.strokeStyle = this.gridColor;
        ctx.lineWidth = 0.5;

        // Vertical lines (subdivisions every 1/5 div)
        for (let i = 0; i <= numH * 5; i++) {
            const x = (i / 5) * dx;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            if (i % 5 === 0) {
                ctx.strokeStyle = this.gridColorMajor;
            } else {
                ctx.strokeStyle = this.gridColor;
            }
            ctx.stroke();
        }

        // Horizontal lines
        for (let i = 0; i <= numV * 5; i++) {
            const y = (i / 5) * dy;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            if (i % 5 === 0) {
                ctx.strokeStyle = this.gridColorMajor;
            } else {
                ctx.strokeStyle = this.gridColor;
            }
            ctx.stroke();
        }

        // Labels
        ctx.fillStyle = this.textColor;
        ctx.font = '10px monospace';
        ctx.textAlign = 'center';

        // Time labels
        for (let i = 0; i <= numH; i++) {
            const x = i * dx;
            const timeVal = (i - numH / 2) * this.timePerDiv;
            let label;
            if (Math.abs(timeVal) >= 1000) {
                label = (timeVal / 1000).toFixed(1) + 'ms';
            } else {
                label = timeVal.toFixed(0) + 'µs';
            }
            ctx.fillText(label, x, h - 4);
        }

        // Voltage labels
        ctx.textAlign = 'right';
        const totalSpan = this.voltsPerDiv * this.numVertDivs; // mV
        for (let i = 0; i <= numV; i++) {
            const y = i * dy;
            const mv = this.vMid + totalSpan / 2 - (i / numV) * totalSpan;
            const label = (mv / 1000).toFixed(2) + 'V';
            ctx.fillText(label, w - 4, y + 3);
        }
    },
};

// Hook into App
App.initWaveform = function () {
    Waveform.init();
};

// Global function called by App
function renderWaveform(buffer, sampleRate) {
    if (!Waveform.ctx) {
        Waveform.init();
    }
    Waveform.render(buffer, sampleRate);
}

// ── Measurements ────────────────────────────────────────────────────────

function updateMeasurements(buffer, droppedPackets, totalPackets) {
    if (!buffer || buffer.length < 2) {
        document.getElementById('meas-vpp').textContent = '--';
        document.getElementById('meas-freq').textContent = '--';
        document.getElementById('meas-avg').textContent = '--';
        document.getElementById('meas-min').textContent = '--';
        document.getElementById('meas-max').textContent = '--';
        document.getElementById('meas-drop').textContent = '0%';
        return;
    }

    // Take the last ~10k samples for measurements
    const n = Math.min(buffer.length, 10000);
    const slice = buffer.slice(-n);

    let min = slice[0], max = slice[0], sum = 0;
    for (let i = 0; i < slice.length; i++) {
        const v = slice[i];
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
    }
    const avg = sum / slice.length;
    const vpp = max - min;

    // Simple frequency estimation (zero-crossing)
    let crossings = 0;
    for (let i = 1; i < slice.length; i++) {
        if ((slice[i - 1] < avg && slice[i] >= avg) ||
            (slice[i - 1] >= avg && slice[i] < avg)) {
            crossings++;
        }
    }
    const estFreq = '--'; // Can't know without sample rate context in slice

    document.getElementById('meas-vpp').textContent = (vpp / 1000).toFixed(3) + ' V';
    document.getElementById('meas-avg').textContent = (avg / 1000).toFixed(3) + ' V';
    document.getElementById('meas-min').textContent = (min / 1000).toFixed(3) + ' V';
    document.getElementById('meas-max').textContent = (max / 1000).toFixed(3) + ' V';

    // Frequency estimation using full buffer with known sample rate
    if (App.deviceState.sample_rate) {
        const fullSlice = buffer.slice(-Math.min(buffer.length, 20000));
        let fullCrossings = 0;
        for (let i = 1; i < fullSlice.length; i++) {
            if ((fullSlice[i - 1] < avg && fullSlice[i] >= avg) ||
                (fullSlice[i] >= avg && fullSlice[i - 1] < avg)) {
                fullCrossings++;
            }
        }
        const duration = fullSlice.length / App.deviceState.sample_rate;
        const freq = fullCrossings / (2 * duration);
        document.getElementById('meas-freq').textContent =
            freq >= 1000 ? (freq / 1000).toFixed(2) + ' kHz' : freq.toFixed(1) + ' Hz';
    }

    // Drop rate
    if (totalPackets > 0) {
        const dropRate = (droppedPackets / (totalPackets + droppedPackets)) * 100;
        document.getElementById('meas-drop').textContent = dropRate.toFixed(1) + '%';
    }
}
