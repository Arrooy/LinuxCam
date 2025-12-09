/**
 * State Management Module
 * Centralized state with reactive updates
 */

export const ConnectionState = {
    DISCONNECTED: 'disconnected',
    CONNECTING: 'connecting',
    CONNECTED: 'connected',
    STREAMING: 'streaming',
    ERROR: 'error',
    RECONNECTING: 'reconnecting'
};

export const ConnectionMode = {
    JPEG: 'jpeg',
    WEBRTC: 'webrtc'
};

class StateManager {
    constructor() {
        this.state = {
            // Connection state
            connectionState: ConnectionState.DISCONNECTED,
            connectionMode: ConnectionMode.WEBRTC,
            lastError: null,
            reconnectAttempts: 0,

            // Camera state
            cameraActive: false,
            currentFacingMode: 'user',
            localStream: null,

            // WebRTC state
            peerConnection: null,
            dataChannel: null,
            webrtcConnected: false,
            offerRequestPending: false,

            // Encoder state
            videoEncoder: null,
            encoderConfigured: false,
            encodingFrameId: 0,

            // WebSocket state
            ws: null,

            // Streaming state
            isStreaming: false,
            frameInterval: null,

            // Statistics
            framesSent: 0,
            dataSent: 0,
            framesReceived: 0,
            receivedFrameTimestamps: [],
            lastServerResponseTime: Date.now(),

            // Settings
            settings: {
                connectionMode: 'webrtc',
                jpegQuality: 85,
                resolution: '960x540',
                deviceId: 'default'
            }
        };

        this.listeners = new Map();
    }

    /**
     * Get current state
     */
    getState() {
        return { ...this.state };
    }

    /**
     * Get specific state value
     */
    get(key) {
        return this.state[key];
    }

    /**
     * Update state and notify listeners
     */
    setState(updates) {
        const oldState = { ...this.state };
        this.state = { ...this.state, ...updates };

        // Notify listeners of changed keys
        Object.keys(updates).forEach(key => {
            if (this.listeners.has(key)) {
                this.listeners.get(key).forEach(callback => {
                    callback(this.state[key], oldState[key]);
                });
            }
        });

        // Notify global listeners
        if (this.listeners.has('*')) {
            this.listeners.get('*').forEach(callback => {
                callback(this.state, oldState);
            });
        }
    }

    /**
     * Subscribe to state changes
     */
    subscribe(key, callback) {
        if (!this.listeners.has(key)) {
            this.listeners.set(key, new Set());
        }
        this.listeners.get(key).add(callback);

        // Return unsubscribe function
        return () => {
            const callbacks = this.listeners.get(key);
            if (callbacks) {
                callbacks.delete(callback);
            }
        };
    }

    /**
     * Reset state to initial values
     */
    reset() {
        const settings = this.state.settings;
        this.setState({
            connectionState: ConnectionState.DISCONNECTED,
            lastError: null,
            reconnectAttempts: 0,
            cameraActive: false,
            localStream: null,
            peerConnection: null,
            dataChannel: null,
            webrtcConnected: false,
            offerRequestPending: false,
            videoEncoder: null,
            encoderConfigured: false,
            encodingFrameId: 0,
            ws: null,
            isStreaming: false,
            frameInterval: null,
            framesSent: 0,
            dataSent: 0,
            framesReceived: 0,
            receivedFrameTimestamps: [],
            settings
        });
    }

    /**
     * Load settings from localStorage
     */
    loadSettings() {
        const saved = localStorage.getItem('linuxface_settings');
        if (saved) {
            try {
                const parsed = JSON.parse(saved);

                // Migrate old format
                if (parsed.serverQuality !== undefined) {
                    parsed.jpegQuality = parsed.serverQuality;
                    delete parsed.serverQuality;
                }

                this.setState({
                    settings: { ...this.state.settings, ...parsed }
                });

                console.log('[State] Loaded settings:', this.state.settings);
            } catch (e) {
                console.error('[State] Failed to load settings:', e);
            }
        }
    }

    /**
     * Save settings to localStorage
     */
    saveSettings() {
        localStorage.setItem('linuxface_settings', JSON.stringify(this.state.settings));
        console.log('💾 Settings saved');
    }

    /**
     * Update settings
     */
    updateSettings(updates) {
        this.setState({
            settings: { ...this.state.settings, ...updates }
        });
        this.saveSettings();
    }
}

// Export singleton instance
export const state = new StateManager();
