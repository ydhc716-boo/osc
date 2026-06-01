/**
 * SCO Web UI — WebSocket Communication
 *
 * Manages Socket.IO connection to the Flask backend.
 * Handles real-time scope data and status updates.
 */

// Dependencies: App (from app.js), io (from socket.io CDN)

let socket = null;

App.initSocket = function () {
    socket = io();

    socket.on('connect', () => {
        console.log('WebSocket connected:', socket.id);
        socket.emit('request_status');
    });

    socket.on('disconnect', () => {
        console.log('WebSocket disconnected');
        App.setConnected(false);
    });

    socket.on('connect_error', (err) => {
        console.error('WebSocket connect error:', err);
    });

    // Receive device status updates
    socket.on('status', (state) => {
        console.log('Status update:', state);
        App.updateDeviceState(state);
        // Update UI controls to reflect device state
        if (typeof syncControlsFromState === 'function') {
            syncControlsFromState(state);
        }
    });

    // Receive scope data
    socket.on('scope_data', (data) => {
        App.addScopeData(data.seq, data.samples);
    });

    // Device error
    socket.on('device_error', (data) => {
        console.warn('Device error:', data.code);
    });

    // Device disconnected unexpectedly
    socket.on('device_disconnected', () => {
        console.warn('Device disconnected unexpectedly');
        App.setConnected(false);
    });

    // Connection info
    socket.on('connected_info', (data) => {
        // Server confirms connection state
    });
};

// ── API Helper ──────────────────────────────────────────────────────────

async function apiPost(url, body = {}) {
    try {
        const resp = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        return await resp.json();
    } catch (err) {
        console.error(`API error ${url}:`, err);
        return { success: false, error: err.message };
    }
}

async function apiGet(url) {
    try {
        const resp = await fetch(url);
        return await resp.json();
    } catch (err) {
        console.error(`API error ${url}:`, err);
        return { success: false, error: err.message };
    }
}
