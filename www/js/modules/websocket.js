/**
 * WebSocket Module
 * Manages WebSocket connection and messaging
 * 
 * Safari WebSocket Mitigation:
 * Safari occasionally launches the initial WebSocket on a stale or half-initialized
 * network path, causing the connection to hang indefinitely in CONNECTING state.
 * The standard fix (used by streaming/LLM apps) is to detect timeout and auto-retry.
 * The second connection uses a clean network path and succeeds reliably.
 * 
 * Key implementation details:
 * - Abandon stuck sockets by nulling handlers (calling close() can break Safari further)
 * - Brief stabilization delay after open (Safari needs time before first message)
 */

import { state, ConnectionState } from './state.js';

export class WebSocketManager {
    constructor() {
        this.serverTimeoutInterval = null;
        this.SERVER_TIMEOUT_MS = 30000;
        this.onMessage = null;
        this.onClose = null;
        this.isIntentionalClose = false;
        this.pendingWs = null;
        this.connectionTimeout = null;
        this.safariWorkaroundApplied = false;
    }

    /**
     * Build WebSocket URL from current page location
     */
    _buildWsUrl() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const port = window.location.port && window.location.port !== ''
            ? window.location.port
            : (window.location.protocol === 'https:' ? '8443' : '8080');
        return `${protocol}//${window.location.hostname}:${port}/ws/video`;
    }

    /**
     * Detect if browser is Safari (iOS or macOS)
     */
    _isSafari() {
        const ua = navigator.userAgent;
        return /Safari/i.test(ua) && !/Chrome|Chromium|CriOS|FxiOS/i.test(ua);
    }

    /**
     * Connect to WebSocket server
     * Safari browsers get automatic retry on first connection timeout
     */
    async connect(onMessage, onClose) {
        if (onMessage) this.onMessage = onMessage;
        if (onClose) this.onClose = onClose;

        const needsWorkaround = this._isSafari() && !this.safariWorkaroundApplied;

        if (needsWorkaround) {
            try {
                return await this._connectInternal();
            } catch (error) {
                if (error.message.includes('timeout')) {
                    console.log('[WebSocket] Safari first-connection timeout, retrying...');
                    this._abandonPendingConnection();
                    this.safariWorkaroundApplied = true;
                    await new Promise(resolve => setTimeout(resolve, 200));
                    return await this._connectInternal();
                }
                throw error;
            }
        }

        return await this._connectInternal();
    }

    /**
     * Abandon a stuck pending connection without calling close()
     * Calling close() on a stuck Safari WebSocket can break subsequent connections
     */
    _abandonPendingConnection() {
        if (this.pendingWs) {
            this.pendingWs.onopen = null;
            this.pendingWs.onclose = null;
            this.pendingWs.onerror = null;
            this.pendingWs.onmessage = null;
            this.pendingWs = null;
        }
    }

    /**
     * Internal connect implementation
     */
    async _connectInternal() {
        const wsUrl = this._buildWsUrl();
        const isSafari = this._isSafari();
        // Safari's first connection always hangs - use short timeout to fail fast
        // Retry and other browsers get standard timeout
        const timeoutMs = isSafari && !this.safariWorkaroundApplied ? 2000 : 5000;

        return new Promise((resolve, reject) => {
            console.log('[WebSocket] Connecting to:', wsUrl);

            if (this.connectionTimeout) {
                clearTimeout(this.connectionTimeout);
                this.connectionTimeout = null;
            }

            this.connectionTimeout = setTimeout(() => {
                console.error('[WebSocket] Connection timeout');
                this.connectionTimeout = null;
                reject(new Error('WebSocket connection timeout'));
            }, timeoutMs);

            let ws;
            try {
                ws = new WebSocket(wsUrl);
                ws.binaryType = 'arraybuffer';
                this.pendingWs = ws;
            } catch (error) {
                clearTimeout(this.connectionTimeout);
                this.connectionTimeout = null;
                console.error('[WebSocket] Failed to create WebSocket:', error);
                reject(error);
                return;
            }

            ws.onopen = () => {
                clearTimeout(this.connectionTimeout);
                this.connectionTimeout = null;
                console.log('[WebSocket] Connected');
                this.pendingWs = null;
                state.setState({ ws });

                // Safari needs brief delay before WebSocket is fully ready
                const stabilizationDelay = this._isSafari() ? 200 : 0;

                setTimeout(() => {
                    state.setState({
                        connectionState: ConnectionState.CONNECTED,
                        lastServerResponseTime: Date.now()
                    });
                    this.startServerTimeoutMonitoring();
                    resolve(ws);
                }, stabilizationDelay);
            };

            ws.onmessage = (event) => {
                state.setState({ lastServerResponseTime: Date.now() });
                if (this.onMessage) {
                    this.onMessage(event);
                }
            };

            ws.onerror = (error) => {
                clearTimeout(this.connectionTimeout);
                this.connectionTimeout = null;
                if (!this.isIntentionalClose) {
                    console.error('[WebSocket] Connection error:', error);
                    reject(error);
                }
            };

            ws.onclose = (event) => {
                if (!this.isIntentionalClose) {
                    console.log('[WebSocket] Connection closed unexpectedly:', event.code);
                }

                this.stopServerTimeoutMonitoring();
                state.setState({
                    ws: null,
                    connectionState: ConnectionState.DISCONNECTED
                });

                if (!this.isIntentionalClose && this.onClose) {
                    this.onClose();
                }

                this.isIntentionalClose = false;
            };
        });
    }

    /**
     * Send message via WebSocket
     */
    send(data) {
        const ws = state.get('ws');
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(data);
            return true;
        }
        return false;
    }

    /**
     * Close WebSocket connection
     */
    close() {
        if (this.connectionTimeout) {
            clearTimeout(this.connectionTimeout);
            this.connectionTimeout = null;
        }

        const ws = state.get('ws');
        if (ws) {
            this.isIntentionalClose = true;
            ws.close(1000, 'Client stopping');
            state.setState({ ws: null });
        }

        // Abandon pending connection without calling close() (Safari workaround)
        this._abandonPendingConnection();
        this.stopServerTimeoutMonitoring();
    }

    /**
     * Start monitoring server timeout
     */
    startServerTimeoutMonitoring() {
        this.stopServerTimeoutMonitoring();

        this.serverTimeoutInterval = setInterval(() => {
            const lastResponse = state.get('lastServerResponseTime');
            const timeSince = Date.now() - lastResponse;

            if (timeSince > this.SERVER_TIMEOUT_MS) {
                console.warn('[WebSocket] Server timeout -', timeSince, 'ms since last response');
                state.setState({
                    connectionState: ConnectionState.ERROR,
                    lastError: 'Server timeout'
                });
                // Close will trigger onClose callback which handles reconnection logic
                this.close();
            }
        }, 1000);
    }

    /**
     * Stop server timeout monitoring
     */
    stopServerTimeoutMonitoring() {
        if (this.serverTimeoutInterval) {
            clearInterval(this.serverTimeoutInterval);
            this.serverTimeoutInterval = null;
        }
    }
}

export const websocketManager = new WebSocketManager();
