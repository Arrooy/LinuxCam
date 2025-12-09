/**
 * Reconnection & Error Recovery Module
 * Handles automatic reconnection with exponential backoff
 */

import { state, ConnectionState } from './state.js';

class ReconnectionManager {
    constructor() {
        this.maxAttempts = 5;
        this.baseDelay = 1000; // 1 second
        this.maxDelay = 30000; // 30 seconds
        this.reconnectTimer = null;
        this.isReconnecting = false;
    }

    /**
     * Calculate backoff delay using exponential backoff with jitter
     */
    calculateBackoff(attempt) {
        const exponentialDelay = Math.min(
            this.baseDelay * Math.pow(2, attempt),
            this.maxDelay
        );

        // Add random jitter (±20%) to prevent thundering herd
        const jitter = exponentialDelay * 0.2 * (Math.random() - 0.5);
        return Math.floor(exponentialDelay + jitter);
    }

    /**
     * Start reconnection process
     */
    async startReconnection(connectCallback) {
        if (this.isReconnecting) {
            console.log('[Reconnect] Already in progress');
            return;
        }

        this.isReconnecting = true;
        const attempts = state.get('reconnectAttempts');

        if (attempts >= this.maxAttempts) {
            console.error('[Reconnect] Max attempts reached');
            state.setState({
                connectionState: ConnectionState.ERROR,
                lastError: 'Max reconnection attempts exceeded. Please restart manually.',
                isReconnecting: false
            });
            this.isReconnecting = false;
            return false;
        }

        const delay = this.calculateBackoff(attempts);
        console.log(`[Reconnect] Attempting in ${(delay / 1000).toFixed(1)}s (attempt ${attempts + 1}/${this.maxAttempts})`);

        state.setState({
            connectionState: ConnectionState.RECONNECTING,
            reconnectAttempts: attempts + 1
        });

        return new Promise((resolve) => {
            this.reconnectTimer = setTimeout(async () => {
                console.log(`[Reconnect] Attempting ${attempts + 1}/${this.maxAttempts}...`);

                try {
                    await connectCallback();

                    // Success - reset attempts
                    console.log('[Reconnect] Successful');
                    state.setState({
                        reconnectAttempts: 0,
                        connectionState: ConnectionState.CONNECTED,
                        lastError: null
                    });
                    this.isReconnecting = false;
                    resolve(true);

                } catch (error) {
                    console.error('[Reconnect] Failed:', error);
                    state.setState({
                        lastError: error.message
                    });
                    this.isReconnecting = false;

                    // Try again recursively
                    const success = await this.startReconnection(connectCallback);
                    resolve(success);
                }
            }, delay);
        });
    }

    /**
     * Cancel ongoing reconnection
     */
    cancel() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        this.isReconnecting = false;
        state.setState({
            connectionState: ConnectionState.DISCONNECTED,
            reconnectAttempts: 0
        });
        console.log('[Reconnect] Cancelled');
    }

    /**
     * Reset reconnection state
     */
    reset() {
        this.cancel();
        state.setState({
            reconnectAttempts: 0,
            lastError: null
        });
    }
}

export const reconnectionManager = new ReconnectionManager();
