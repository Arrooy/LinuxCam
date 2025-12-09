/**
 * LinuxFace Main Application
 * Modular Architecture with State Management and Error Recovery
 */

import { state, ConnectionState } from './modules/state.js';
import { reconnectionManager } from './modules/reconnect.js';
import { cameraManager } from './modules/camera.js';
import { websocketManager } from './modules/websocket.js';
import { webrtcManager } from './modules/webrtc.js';

class LinuxFaceApp {
    constructor() {
        this.elements = {};
        this.eventListeners = [];
    }

    /**
     * Initialize application
     */
    async init() {
        console.log('[LinuxFace] Initializing...');

        // Setup debug console FIRST to capture all logs
        this.setupDebugConsole();

        // Load settings from localStorage
        state.loadSettings();

        // Get DOM elements
        this.initializeElements();

        // Initialize modules
        cameraManager.initialize(
            this.elements.localVideo,
            this.elements.captureCanvas
        );

        // Setup UI event listeners
        this.setupEventListeners();

        // Setup state subscriptions for reactive UI updates
        this.setupStateSubscriptions();

        console.log('[LinuxFace] Initialized');
    }

    /**
     * Get all DOM element references
     */
    initializeElements() {
        this.elements = {
            // Video elements
            localVideo: document.getElementById('localVideo'),
            localPreview: document.getElementById('localPreview'),
            captureCanvas: document.getElementById('captureCanvas'),
            receivedCanvas: document.getElementById('receivedCanvas'),
            placeholderText: document.getElementById('placeholderText'),

            // Controls
            startBtn: document.getElementById('startBtn'),
            settingsBtn: document.getElementById('settingsBtn'),
            targetPhotoBtn: document.getElementById('targetPhotoBtn'),

            // Status displays
            cameraStatus: document.getElementById('cameraStatus'),
            cameraStatusDot: document.getElementById('cameraStatusDot'),
            qualityIndicator: document.querySelector('.quality-indicator'),
            qualityText: document.getElementById('qualityText'),
            fpsDisplay: document.getElementById('fpsDisplay'),
            framesSent: document.getElementById('framesSent'),
            dataSent: document.getElementById('dataSent'),
            framesReceived: document.getElementById('framesReceived'),

            // Modals
            settingsModal: document.getElementById('settingsModal'),
            photoSourceModal: document.getElementById('photoSourceModal'),

            // Settings
            jpegQualitySlider: document.getElementById('jpegQualitySlider'),
            jpegQualityValue: document.getElementById('jpegQualityValue'),
            deviceSelect: document.getElementById('deviceSelect'),
            resolutionSelect: document.getElementById('resolutionSelect'),
            deviceTestBtn: document.getElementById('deviceTestBtn'),
            modeJpeg: document.getElementById('modeJpeg'),
            modeWebRTC: document.getElementById('modeWebRTC')
        };
    }

    /**
     * Setup debug console to capture all console logs
     */
    setupDebugConsole() {
        const debugToggle = document.getElementById('debugToggle');
        const debugConsole = document.getElementById('debugConsole');
        const debugOutput = document.getElementById('debugOutput');
        const debugClear = document.getElementById('debugClear');
        const debugClose = document.getElementById('debugClose');

        // Store original console methods
        const originalConsole = {
            log: console.log.bind(console),
            warn: console.warn.bind(console),
            error: console.error.bind(console),
            info: console.info.bind(console)
        };

        // Helper to add entry to debug console
        const addDebugEntry = (type, args) => {
            const timestamp = new Date().toLocaleTimeString('en-US', { 
                hour12: false, 
                hour: '2-digit', 
                minute: '2-digit', 
                second: '2-digit',
                fractionalSecondDigits: 3
            });

            const message = args.map(arg => {
                if (typeof arg === 'object') {
                    try {
                        return JSON.stringify(arg, null, 2);
                    } catch (e) {
                        return String(arg);
                    }
                }
                return String(arg);
            }).join(' ');

            // Extract tag from [Tag] format
            let tag = '';
            const tagMatch = message.match(/^\[([^\]]+)\]/);
            if (tagMatch) {
                tag = tagMatch[1];
            }

            const entry = document.createElement('div');
            entry.className = `debug-entry ${type}`;
            entry.innerHTML = `
                <span class="debug-timestamp">${timestamp}</span>
                ${tag ? `<span class="debug-tag">[${tag}]</span>` : ''}
                <span>${message}</span>
            `;

            debugOutput.appendChild(entry);

            // Auto-scroll to bottom
            debugOutput.scrollTop = debugOutput.scrollHeight;

            // Limit to last 200 entries to prevent memory issues
            while (debugOutput.children.length > 200) {
                debugOutput.removeChild(debugOutput.firstChild);
            }
        };

        // Override console methods
        console.log = function(...args) {
            originalConsole.log(...args);
            addDebugEntry('log', args);
        };

        console.warn = function(...args) {
            originalConsole.warn(...args);
            addDebugEntry('warn', args);
        };

        console.error = function(...args) {
            originalConsole.error(...args);
            addDebugEntry('error', args);
        };

        console.info = function(...args) {
            originalConsole.info(...args);
            addDebugEntry('info', args);
        };

        // Capture uncaught errors (filter out expected WebSocket closure errors)
        window.addEventListener('error', (event) => {
            // Ignore WebSocket errors during intentional close
            const isWebSocketError = event.message && 
                                    (event.message.includes('WebSocket') || 
                                     event.target instanceof WebSocket);
            
            if (isWebSocketError && event.isTrusted) {
                // This is likely an expected shutdown error, log at debug level
                addDebugEntry('log', [`[Expected] WebSocket event during shutdown`]);
            } else {
                addDebugEntry('error', [`Uncaught Error: ${event.message}`, `at ${event.filename}:${event.lineno}:${event.colno}`]);
            }
        });

        // Capture unhandled promise rejections
        window.addEventListener('unhandledrejection', (event) => {
            // Check if this is a WebSocket-related rejection during shutdown
            const reason = String(event.reason);
            if (reason.includes('WebSocket') || reason.includes('connection')) {
                addDebugEntry('warn', [`Promise rejection (may be during shutdown): ${reason}`]);
            } else {
                addDebugEntry('error', [`Unhandled Promise Rejection: ${reason}`]);
            }
        });

        // Toggle debug console
        debugToggle.addEventListener('click', () => {
            debugConsole.classList.toggle('hidden');
        });

        // Clear button
        debugClear.addEventListener('click', () => {
            debugOutput.innerHTML = '';
            originalConsole.log('[Debug] Console cleared');
        });

        // Close button
        debugClose.addEventListener('click', () => {
            debugConsole.classList.add('hidden');
        });

        console.log('[Debug] Debug console initialized - tap icon in top-left to toggle');
    }

    /**
     * Setup UI event listeners with tracking for cleanup
     */
    setupEventListeners() {
        // Start/Stop button
        this.addTrackedListener(this.elements.startBtn, 'click', async () => {
            if (!state.get('cameraActive')) {
                await this.startCamera();
            } else {
                await this.stopCamera();
            }
        });

        // Settings button
        this.addTrackedListener(this.elements.settingsBtn, 'click', () => {
            this.openSettings();
        });

        // Settings modal - Save button
        this.addTrackedListener(document.getElementById('settingsSaveBtn'), 'click', async () => {
            await this.saveSettings();
        });

        // Device test button
        this.addTrackedListener(this.elements.deviceTestBtn, 'click', async () => {
            await this.handleDeviceTest();
        });

        // Settings modal - Cancel button
        this.addTrackedListener(document.getElementById('settingsCancelBtn'), 'click', () => {
            this.closeSettings();
        });

        // Settings modal - Close on background click
        this.addTrackedListener(this.elements.settingsModal, 'click', (e) => {
            if (e.target === this.elements.settingsModal) {
                this.closeSettings();
            }
        });

        // Connection mode selection
        this.addTrackedListener(this.elements.modeJpeg, 'click', () => {
            this.selectConnectionMode('jpeg');
        });

        this.addTrackedListener(this.elements.modeWebRTC, 'click', () => {
            this.selectConnectionMode('webrtc');
        });

        // JPEG quality slider
        this.addTrackedListener(this.elements.jpegQualitySlider, 'input', (e) => {
            this.elements.jpegQualityValue.textContent = e.target.value + '%';
        });

        // Double-tap to switch camera
        let lastTapTime = 0;
        const handleDoubleTap = (e) => {
            const now = Date.now();
            if (now - lastTapTime < 300 && state.get('cameraActive')) {
                e.preventDefault();
                cameraManager.switchCamera().catch(err => {
                    console.error('Camera switch failed:', err);
                    this.showError('Could not switch camera');
                });
            }
            lastTapTime = now;
        };

        this.addTrackedListener(this.elements.localPreview, 'click', handleDoubleTap);
        this.addTrackedListener(document.getElementById('mainView'), 'click', handleDoubleTap);

        // Cleanup on page unload
        window.addEventListener('beforeunload', () => {
            this.cleanup();
        });
    }

    /**
     * Setup reactive state subscriptions
     */
    setupStateSubscriptions() {
        // Update UI when connection state changes
        state.subscribe('connectionState', (newState) => {
            this.updateConnectionUI(newState);
        });

        // Update UI when camera state changes
        state.subscribe('cameraActive', (active) => {
            this.updateCameraUI(active);
        });

        // Update stats displays
        state.subscribe('framesSent', (count) => {
            this.elements.framesSent.textContent = count;
        });

        state.subscribe('framesReceived', (count) => {
            this.elements.framesReceived.textContent = count;
            if (count === 1) {
                this.elements.placeholderText.style.display = 'none';
            }
        });

        state.subscribe('dataSent', (kb) => {
            this.elements.dataSent.textContent = kb.toFixed(1);
        });
    }

    /**
     * Start camera and connect
     */
    async startCamera() {
        try {
            console.log('[Camera] Starting...');

            // Start camera
            await cameraManager.startCamera();

            console.log('[Camera] Camera ready, connecting to server...');

            // Connect to server with automatic fallback
            await this.connect();

        } catch (error) {
            console.error('[Camera] Failed to start:', error);
            // Provide more actionable guidance depending on error type
            const msg = this.humanizeCameraError(error);
            this.showError(msg);
            state.setState({
                connectionState: ConnectionState.ERROR,
                lastError: error.message
            });
        }
    }

    humanizeCameraError(error) {
        const text = String(error?.message || error?.name || 'Unknown error');
        if (/NotReadableError/.test(text) || /Could not start video source/.test(text)) {
            return 'Could not start the selected camera. It may be in use by another app, blocked by the OS, or incompatible with the chosen resolution. Try closing other camera apps, selecting a different camera in Settings, or reducing the capture resolution.';
        }
        if (/NotAllowedError|PermissionDeniedError/.test(text)) {
            return 'Camera permission denied. Please allow camera access in your browser and try again.';
        }
        if (/OverconstrainedError/.test(text)) {
            return 'The selected constraints (resolution or device) are not supported by the camera. Try a lower resolution or a different camera.';
        }
        return text;
    }

    /**
     * Stop camera and disconnect
     */
    async stopCamera() {
        console.log('[Camera] Stopping...');

        // Cancel any ongoing reconnection
        reconnectionManager.cancel();

        // Stop streaming interval to prevent frame counting
        const frameInterval = state.get('frameInterval');
        if (frameInterval) {
            clearInterval(frameInterval);
            console.log('[Streaming] Frame interval stopped');
        }

        // Stop camera
        cameraManager.stopCamera();

        // Close WebRTC if active
        webrtcManager.close();

        // Close WebSocket
        websocketManager.close();

        // Reset state
        state.reset();

        // Reset UI
        this.resetUI();

    }

    /**
     * Connect with automatic WebRTC→JPEG fallback
     */
    async connect() {
        const settings = state.get('settings');
        if (settings.connectionMode === 'webrtc') {
            await this.connectWebRTC();
        } else {
            await this.connectJPEG();
        }
    }

    /**
     * Connect using WebRTC mode
     */
    async connectWebRTC() {
        console.log('[WebRTC] Starting connection sequence...');

        console.log('[WebRTC] Phase 1: Establishing WebSocket for signaling...');
        // First establish WebSocket for signaling
        await websocketManager.connect(
            (event) => this.handleWebSocketMessage(event),
            () => this.handleWebSocketClose()
        );
        console.log('[WebRTC] Phase 1 complete: WebSocket connected');

        console.log('[WebRTC] Phase 2: Initializing WebCodecs H.264 encoder/decoder...');
        // Initialize WebRTC with video element for received stream
        const webrtcInitialized = await webrtcManager.initialize(this.elements.receivedCanvas);
        if (!webrtcInitialized) {
            console.error('[WebRTC] Initialization failed - cannot proceed with WebRTC connection');
            throw new Error('WebRTC initialization failed - H.264 codecs not available');
        }
        console.log('[WebRTC] Phase 2 complete: WebCodecs initialized');

        console.log('[WebRTC] Phase 3: Creating WebRTC peer connection and exchanging offer/answer...');
        // Start WebRTC connection (creates offer, exchanges via WebSocket)
        await webrtcManager.connect();
        console.log('[WebRTC] Phase 3 complete: Peer connection established');

        console.log('[WebRTC] ✓ All phases complete - WebRTC connected successfully');
    }

    /**
     * Connect using JPEG mode
     */
    async connectJPEG() {
        console.log('[JPEG] Starting...');

        await websocketManager.connect(
            (event) => this.handleWebSocketMessage(event),
            () => this.handleWebSocketClose()
        );

        // Send initial quality setting
        const settings = state.get('settings');
        websocketManager.send(`QUALITY:${settings.jpegQuality}`);

        // Start JPEG streaming
        this.startJPEGStreaming();
    }

    /**
     * Start JPEG frame streaming with adaptive frame rate
     */
    startJPEGStreaming() {
        // Use similar adaptive approach as WebRTC mode for consistency
        const isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
        const targetFrameRate = isMobile ? 24 : 30;
        const frameIntervalMs = 1000 / targetFrameRate;

        console.log(`[JPEG] Starting adaptive frame streaming: ${targetFrameRate}fps`);

        // Use requestAnimationFrame for better performance than setInterval
        let lastCaptureTime = 0;

        const captureLoop = (currentTime) => {
            // Adaptive frame rate timing
            if (currentTime - lastCaptureTime < frameIntervalMs) {
                if (state.get('isStreaming')) {
                    requestAnimationFrame(captureLoop);
                }
                return;
            }

            // Check WebSocket state before processing
            const ws = state.get('ws');
            if (!ws || ws.readyState !== WebSocket.OPEN) {
                console.warn('[JPEG] WebSocket not ready, stopping streaming');
                state.setState({ isStreaming: false });
                return;
            }

            this.captureAndSendJPEGFrame();
            lastCaptureTime = currentTime;

            // Continue loop if still streaming
            if (state.get('isStreaming')) {
                requestAnimationFrame(captureLoop);
            }
        };

        // Start the adaptive streaming loop
        requestAnimationFrame(captureLoop);

        state.setState({
            isStreaming: true,
            connectionState: ConnectionState.STREAMING
        });
    }

    /**
     * Capture and send JPEG frame
     */
    async captureAndSendJPEGFrame() {
        const ws = state.get('ws');
        if (!ws || ws.readyState !== WebSocket.OPEN) return;

        const canvas = cameraManager.captureCanvas;

        // Validate canvas dimensions before capturing
        if (!canvas || canvas.width === 0 || canvas.height === 0) {
            console.warn('[JPEG] Canvas not ready, skipping frame');
            return;
        }

        cameraManager.captureFrame();

        const settings = state.get('settings');

        // Ensure quality is in valid range (0.0 - 1.0)
        const quality = Math.max(0.1, Math.min(1.0, settings.jpegQuality / 100));

        canvas.toBlob(async (blob) => {
            if (!blob) {
                console.error('[JPEG] Failed to create JPEG blob');
                return;
            }

            if (ws.readyState !== WebSocket.OPEN) {
                console.warn('[JPEG] WebSocket closed during frame encoding');
                return;
            }

            const arrayBuffer = await blob.arrayBuffer();

            // Verify JPEG data is valid (should have JPEG magic bytes: FF D8)
            if (arrayBuffer.byteLength < 2) {
                console.error('[JPEG] Data too small:', arrayBuffer.byteLength, 'bytes');
                return;
            }

            const bytes = new Uint8Array(arrayBuffer);
            if (bytes[0] !== 0xFF || bytes[1] !== 0xD8) {
                console.error('[JPEG] Invalid magic bytes:', bytes[0].toString(16), bytes[1].toString(16));
                return;
            }

            ws.send(arrayBuffer);

            const framesSent = state.get('framesSent') + 1;
            const dataSent = state.get('dataSent') + (arrayBuffer.byteLength / 1024);

            state.setState({ framesSent, dataSent });

            // Log first frame for verification
            if (framesSent === 1) {
                console.log('[JPEG] First frame sent:', arrayBuffer.byteLength, 'bytes, quality:', quality);
            }
        }, 'image/jpeg', quality);
    }

    /**
     * Handle WebSocket messages
     */
    handleWebSocketMessage(event) {
        if (typeof event.data === 'string') {
            if (event.data.startsWith('WEBRTC:')) {
                // Handle WebRTC signaling responses
                try {
                    const message = JSON.parse(event.data.substring(7)); // Remove 'WEBRTC:' prefix
                    console.log('[WebSocket] WebRTC signaling received:', {
                        type: message.type,
                        success: message.success,
                        error: message.error,
                        peerId: message.peerId
                    });

                    if (message.type === 'server_answer' && message.success) {
                        console.log('[WebRTC] Processing server answer...');
                        webrtcManager.handleAnswer(message.sdp);
                    } else if (message.type === 'ice_candidate_response' && message.success) {
                        console.log('[WebRTC] ICE candidate acknowledged by server');
                    } else if (message.error) {
                        console.error('[WebRTC] Server error:', message.error);
                        // Show user-friendly error
                        this.showWebRTCError('Server Error', message.error);
                    } else {
                        console.warn('[WebRTC] Unexpected server response:', message);
                    }
                } catch (error) {
                    console.error('[WebSocket] Failed to parse WebRTC signaling:', error);
                }
            } else {
                console.log('[WebSocket] Server message:', event.data);
            }
        } else {
            // Binary data - processed frame (JPEG mode only)
            this.displayReceivedFrame(event.data);
        }
    }

    /**
     * Show WebRTC connection error
     */
    showWebRTCError(title, message) {
        // Create modal overlay
        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.8);
            z-index: 10000;
            display: flex;
            align-items: center;
            justify-content: center;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
        `;

        // Create modal dialog
        const modal = document.createElement('div');
        modal.style.cssText = `
            background: white;
            border-radius: 12px;
            padding: 24px;
            max-width: 500px;
            margin: 20px;
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.3);
        `;

        modal.innerHTML = `
            <div style="display: flex; align-items: center; margin-bottom: 16px;">
                <div style="width: 24px; height: 24px; background: #ff4444; border-radius: 50%; margin-right: 12px; display: flex; align-items: center; justify-content: center; color: white; font-weight: bold;">!</div>
                <h2 style="margin: 0; color: #333; font-size: 18px;">${title}</h2>
            </div>
            <p style="color: #666; line-height: 1.5; margin-bottom: 20px;">${message}</p>
            <div style="text-align: right;">
                <button id="webrtcConnectionErrorClose" style="
                    background: #007bff;
                    color: white;
                    border: none;
                    border-radius: 6px;
                    padding: 10px 20px;
                    cursor: pointer;
                    font-size: 14px;
                    margin-right: 8px;
                ">Try Again</button>
                <button id="webrtcConnectionErrorSwitch" style="
                    background: #6c757d;
                    color: white;
                    border: none;
                    border-radius: 6px;
                    padding: 10px 20px;
                    cursor: pointer;
                    font-size: 14px;
                ">Switch to JPEG</button>
            </div>
        `;

        overlay.appendChild(modal);
        document.body.appendChild(overlay);

        // Close modal handlers
        const closeModal = () => {
            document.body.removeChild(overlay);
        };

        document.getElementById('webrtcConnectionErrorClose').onclick = async () => {
            closeModal();
            // Try reconnecting
            await this.stopCamera();
            await this.startCamera();
        };

        document.getElementById('webrtcConnectionErrorSwitch').onclick = () => {
            closeModal();
            // Switch to JPEG mode
            this.selectConnectionMode('jpeg');
        };

        overlay.onclick = (e) => {
            if (e.target === overlay) closeModal();
        };

        console.error(`[WebRTC] Connection Error: ${title} - ${message}`);
    }

    /**
     * Show simple connection lost notification
     */
    showConnectionLostNotification() {
        // Create toast notification
        const toast = document.createElement('div');
        toast.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: #ff4444;
            color: white;
            padding: 16px 24px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            z-index: 10000;
            display: flex;
            align-items: center;
            gap: 12px;
            animation: slideIn 0.3s ease-out;
        `;

        toast.innerHTML = `
            <div style="font-size: 20px;">⚠️</div>
            <div>
                <div style="font-weight: bold; margin-bottom: 4px;">Connection Lost</div>
                <div style="font-size: 14px; opacity: 0.9;">Camera stopped. Click Start to reconnect.</div>
            </div>
        `;

        // Add animation if not already present
        if (!document.getElementById('toastAnimationStyle')) {
            const style = document.createElement('style');
            style.id = 'toastAnimationStyle';
            style.textContent = `
                @keyframes slideIn {
                    from {
                        transform: translateX(400px);
                        opacity: 0;
                    }
                    to {
                        transform: translateX(0);
                        opacity: 1;
                    }
                }
            `;
            document.head.appendChild(style);
        }

        document.body.appendChild(toast);

        // Auto-remove after 5 seconds
        setTimeout(() => {
            toast.style.transition = 'opacity 0.3s ease-out, transform 0.3s ease-out';
            toast.style.opacity = '0';
            toast.style.transform = 'translateX(400px)';
            setTimeout(() => {
                if (toast.parentNode) {
                    document.body.removeChild(toast);
                }
            }, 300);
        }, 5000);

        console.log('[UI] Connection lost notification shown');
    }

    /**
     * Handle WebSocket close
     */
    handleWebSocketClose() {
        console.log('[WebSocket] Connection closed');

        const settings = state.get('settings');
        
        if (settings.connectionMode === 'jpeg') {
            // JPEG mode: Auto-reconnect (WebSocket is the only connection)
            if (!reconnectionManager.isReconnecting) {
                console.log('[Reconnect] Starting reconnection process for JPEG mode...');
                reconnectionManager.startReconnection(async () => {
                    await this.connect();
                });
            } else {
                console.log('[Reconnect] Already attempting reconnection, skipping');
            }
        } else {
            // WebRTC mode: Stop camera, let user restart manually
            // WebRTC has complex state (PeerConnection, encoders, decoders) that requires full reset
            console.log('[WebRTC] Connection lost - stopping camera. Please restart manually.');
            this.stopCamera();
            
            // Show user-friendly notification
            this.showConnectionLostNotification();
        }
    }

    /**
     * Display received frame on canvas
     */
    displayReceivedFrame(data) {
        const blob = new Blob([data], { type: 'image/jpeg' });
        const img = new Image();

        img.onload = () => {
            const canvas = this.elements.receivedCanvas;
            const ctx = canvas.getContext('2d');

            canvas.width = img.width;
            canvas.height = img.height;
            ctx.drawImage(img, 0, 0);
            URL.revokeObjectURL(img.src);

            // Update stats
            const timestamps = state.get('receivedFrameTimestamps');
            timestamps.push(Date.now());

            // Calculate FPS
            const cutoff = Date.now() - 3000;
            const recent = timestamps.filter(t => t > cutoff);
            state.setState({
                receivedFrameTimestamps: recent,
                framesReceived: state.get('framesReceived') + 1
            });

            if (recent.length > 1) {
                const timeSpan = Date.now() - recent[0];
                const fps = Math.round((recent.length - 1) * 1000 / timeSpan);
                this.elements.fpsDisplay.textContent = `${fps} FPS`;
            }
        };

        img.src = URL.createObjectURL(blob);
    }

    /**
     * Update connection UI based on state
     */
    updateConnectionUI(connectionState) {
        const indicator = this.elements.qualityIndicator;
        const text = this.elements.qualityText;

        indicator.classList.remove('good', 'poor');

        switch (connectionState) {
            case ConnectionState.CONNECTED:
            case ConnectionState.STREAMING:
                indicator.classList.add('good');
                text.textContent = 'Connected';
                break;

            case ConnectionState.CONNECTING:
            case ConnectionState.RECONNECTING:
                text.textContent = 'Connecting...';
                break;

            case ConnectionState.ERROR:
                indicator.classList.add('poor');
                text.textContent = 'Error';
                break;

            default:
                indicator.classList.add('poor');
                text.textContent = 'Disconnected';
        }
    }

    /**
     * Update camera UI
     */
    updateCameraUI(active) {
        if (active) {
            const settings = state.get('settings');
            let statusText = 'Camera: Active';
            if (settings.deviceId && settings.deviceId !== 'default') {
                const devices = cameraManager.getAvailableDevices();
                const dev = devices.find(d => d.deviceId === settings.deviceId);
                if (dev) {
                    statusText += ` (${dev.label || 'Selected camera'})`;
                }
            } else {
                const facing = state.get('currentFacingMode');
                statusText += ` (${facing})`;
            }
            this.elements.cameraStatus.textContent = statusText;
            this.elements.cameraStatusDot.classList.add('active');
            this.elements.localPreview.classList.remove('hidden');
            this.elements.startBtn.textContent = 'Stop Streaming';
            this.elements.startBtn.classList.add('stop');
        } else {
            this.elements.cameraStatus.textContent = 'Camera: Off';
            this.elements.cameraStatusDot.classList.remove('active');
            this.elements.localPreview.classList.add('hidden');
            this.elements.startBtn.textContent = 'Start Camera';
            this.elements.startBtn.classList.remove('stop');
        }
    }

    /**
     * Open settings modal
     */
    async openSettings() {
        const settings = state.get('settings');
        this.elements.jpegQualitySlider.value = settings.jpegQuality;
        this.elements.jpegQualityValue.textContent = settings.jpegQuality + '%';
        this.elements.resolutionSelect.value = settings.resolution;

        // Populate camera devices
        await this.populateDeviceList();
        this.elements.deviceSelect.value = settings.deviceId || 'default';

        this.updateConnectionModeUI();
        this.elements.settingsModal.classList.add('show');
    }

    /**
     * Populate camera device list
     */
    async populateDeviceList() {
        try {
            // Enumerate devices
            const devices = await cameraManager.enumerateDevices();

            // Clear existing options except default
            this.elements.deviceSelect.innerHTML = '<option value="default">Default Camera</option>';

            // Add each video device
            devices.forEach((device, index) => {
                const option = document.createElement('option');
                option.value = device.deviceId;

                // Use device label if available, otherwise generic name
                option.textContent = device.label || `Camera ${index + 1}`;

                this.elements.deviceSelect.appendChild(option);
            });

            console.log(`[Settings] Populated ${devices.length} camera devices`);
        } catch (error) {
            console.error('[Settings] Failed to populate device list:', error);
        }
    }

    /**
     * Close settings modal
     */
    closeSettings() {
        this.elements.settingsModal.classList.remove('show');
    }

    /**
     * Save settings and close modal
     */
    async saveSettings() {
        const oldSettings = state.get('settings');
        const wasStreaming = state.get('cameraActive');

        const newSettings = {
            ...oldSettings,
            jpegQuality: parseInt(this.elements.jpegQualitySlider.value),
            deviceId: this.elements.deviceSelect.value,
            resolution: this.elements.resolutionSelect.value
        };

        const resolutionChanged = oldSettings.resolution !== newSettings.resolution;
        const deviceChanged = oldSettings.deviceId !== newSettings.deviceId;

        state.setState({ settings: newSettings });
        this.closeSettings();

        // Handle device change (requires full camera restart)
        if (wasStreaming && deviceChanged) {
            console.log('[Settings] Camera device changed from', oldSettings.deviceId, 'to', newSettings.deviceId);

            try {
                await this.stopCamera();
                await new Promise(resolve => setTimeout(resolve, 300));
                await this.startCamera();
                console.log('[Settings] Camera restarted with new device');
            } catch (error) {
                console.error('[Settings] Failed to change camera device:', error);
                this.showError('Failed to switch camera device');
            }

            return; // Device change handles everything, skip other logic
        }

        // Handle quality change (can be done without restarting camera)
        if (!resolutionChanged && !deviceChanged && newSettings.connectionMode === 'jpeg' && state.get('ws')?.readyState === WebSocket.OPEN) {
            websocketManager.send(`QUALITY:${newSettings.jpegQuality}`);
            console.log('[Settings] Sent quality setting to server:', newSettings.jpegQuality);
        }

        // Handle resolution change (use applyConstraints to avoid re-requesting permission)
        if (wasStreaming && resolutionChanged && !deviceChanged) {
            console.log('[Settings] Resolution changed from', oldSettings.resolution, 'to', newSettings.resolution);

            // Step 1: Pause frame streaming to stop sending old resolution frames
            const frameInterval = state.get('frameInterval');
            if (frameInterval) {
                clearInterval(frameInterval);
                state.setState({ frameInterval: null, isStreaming: false });
                console.log('[Settings] Paused frame streaming');
            }

            // Step 2: Apply new constraints to existing video track (no permission needed!)
            const stream = state.get('localStream');
            if (stream) {
                const videoTrack = stream.getVideoTracks()[0];
                if (videoTrack) {
                    const [width, height] = newSettings.resolution.split('x').map(Number);

                    try {
                        // Apply new resolution constraints
                        await videoTrack.applyConstraints({
                            width: { ideal: width },
                            height: { ideal: height }
                        });

                        console.log('[Settings] Applied new constraints to video track');

                        // Step 3: Wait for video to adjust and update metadata
                        // Use a one-time listener to avoid multiple rapid updates
                        await new Promise(resolve => {
                            const onMetadataUpdated = () => {
                                // Only update canvas if dimensions actually changed
                                const newWidth = this.elements.localVideo.videoWidth;
                                const newHeight = this.elements.localVideo.videoHeight;

                                if (this.elements.captureCanvas.width !== newWidth ||
                                    this.elements.captureCanvas.height !== newHeight) {
                                    this.elements.captureCanvas.width = newWidth;
                                    this.elements.captureCanvas.height = newHeight;
                                    console.log('[Settings] Canvas resized to:', newWidth, 'x', newHeight);
                                }

                                this.elements.localVideo.removeEventListener('loadedmetadata', onMetadataUpdated);
                                resolve();
                            };

                            this.elements.localVideo.addEventListener('loadedmetadata', onMetadataUpdated, { once: true });

                            // Fallback timeout in case loadedmetadata doesn't fire
                            setTimeout(() => {
                                this.elements.localVideo.removeEventListener('loadedmetadata', onMetadataUpdated);
                                const newWidth = this.elements.localVideo.videoWidth;
                                const newHeight = this.elements.localVideo.videoHeight;
                                if (this.elements.captureCanvas.width !== newWidth ||
                                    this.elements.captureCanvas.height !== newHeight) {
                                    this.elements.captureCanvas.width = newWidth;
                                    this.elements.captureCanvas.height = newHeight;
                                    console.log('[Settings] Canvas resized to (fallback):', newWidth, 'x', newHeight);
                                }
                                resolve();
                            }, 500);
                        });

                        console.log('[Settings] Camera now using resolution:',
                            this.elements.localVideo.videoWidth, 'x', this.elements.localVideo.videoHeight);

                        // Step 5: Signal server to clear buffers and prepare for new resolution
                        if (state.get('ws')?.readyState === WebSocket.OPEN) {
                            websocketManager.send('RESOLUTION_CHANGE');
                            websocketManager.send(`QUALITY:${newSettings.jpegQuality}`);
                            console.log('[Settings] Notified server: resolution change + quality', newSettings.jpegQuality);
                        }

                        // Step 6: Brief delay to ensure server processed the signal
                        await new Promise(resolve => setTimeout(resolve, 100));

                        // Step 6.5: Reconfigure H.264 encoder if using WebRTC
                        const settings = state.get('settings');
                        if (settings.connectionMode === 'webrtc') {
                            await webrtcManager.reconfigureEncoder();
                            console.log('[Settings] Reconfigured H.264 encoder for new resolution');
                        }

                        // Step 7: Resume frame streaming with new resolution
                        this.startJPEGStreaming();
                        console.log('[Settings] Resumed streaming with new resolution');

                    } catch (error) {
                        console.error('[Settings] Failed to apply constraints:', error);

                        // If constraints fail, fall back to stopping and restarting camera
                        console.log('[Settings] Falling back to camera restart...');

                        stream.getTracks().forEach(track => track.stop());
                        await new Promise(resolve => setTimeout(resolve, 200));

                        const [width, height] = newSettings.resolution.split('x').map(Number);
                        const facingMode = state.get('currentFacingMode');

                        try {
                            const newStream = await navigator.mediaDevices.getUserMedia({
                                video: {
                                    width: { ideal: width },
                                    height: { ideal: height },
                                    facingMode
                                },
                                audio: false
                            });

                            this.elements.localVideo.srcObject = newStream;
                            state.setState({ localStream: newStream });

                            await new Promise((resolve) => {
                                this.elements.localVideo.onloadedmetadata = () => {
                                    this.elements.captureCanvas.width = this.elements.localVideo.videoWidth;
                                    this.elements.captureCanvas.height = this.elements.localVideo.videoHeight;
                                    console.log('[Settings] Camera restarted with resolution:',
                                        this.elements.localVideo.videoWidth, 'x', this.elements.localVideo.videoHeight);
                                    resolve();
                                };
                            });

                            if (state.get('ws')?.readyState === WebSocket.OPEN) {
                                websocketManager.send('RESOLUTION_CHANGE');
                                websocketManager.send(`QUALITY:${newSettings.jpegQuality}`);
                            }

                            await new Promise(resolve => setTimeout(resolve, 100));
                            this.startJPEGStreaming();

                        } catch (fallbackError) {
                            console.error('[Settings] Fallback also failed:', fallbackError);
                            this.showError('Failed to change resolution: ' + fallbackError.message);
                            state.setState({ settings: oldSettings });
                        }
                    }
                } else {
                    console.error('[Settings] No video track found');
                }
            }
        }
    }

    async handleDeviceTest() {
        try {
            // Use current resolution as test constraints
            const settings = state.get('settings');
            const [width, height] = settings.resolution.split('x').map(Number);
            const results = await cameraManager.testDevices({ width, height, timeout: 2000 });
            console.log('[Settings] Device test results:', results);
            const success = results.filter(r => r.ok);
            if (success.length === 0) {
                this.showError('No camera could be opened. Check permissions, close other apps using the camera, or try different hardware. See console for details.');
            } else {
                // Show success results in an alert for now
                const successList = success.map(s => `${s.label || s.deviceId}: OK`).join('\n');
                alert('Camera test successful for:\n' + successList);
            }
        } catch (err) {
            console.error('[Settings] Device test failed:', err);
            this.showError('Failed to test devices: ' + (err?.message || err));
        }
    }

    /**
     * Select connection mode
     */
    selectConnectionMode(mode) {
        const settings = state.get('settings');
        const newSettings = { ...settings, connectionMode: mode };

        state.setState({ settings: newSettings });
        state.saveSettings();

        this.updateConnectionModeUI();

        console.log('[Settings] Connection mode changed to:', mode);
    }

    /**
     * Update connection mode UI
     */
    updateConnectionModeUI() {
        const settings = state.get('settings');
        const jpegQualityGroup = document.getElementById('jpegQualityGroup');

        if (settings.connectionMode === 'jpeg') {
            this.elements.modeJpeg.classList.add('selected');
            this.elements.modeWebRTC.classList.remove('selected');
            jpegQualityGroup.style.display = 'block';
        } else {
            this.elements.modeJpeg.classList.remove('selected');
            this.elements.modeWebRTC.classList.add('selected');
            jpegQualityGroup.style.display = 'none';
        }
    }

    /**
     * Reset UI to initial state
     */
    resetUI() {
        const ctx = this.elements.receivedCanvas.getContext('2d');
        ctx.clearRect(0, 0, this.elements.receivedCanvas.width, this.elements.receivedCanvas.height);
        this.elements.placeholderText.style.display = 'block';
        this.elements.fpsDisplay.textContent = '-- FPS';
    }

    /**
     * Show error message to user
     */
    showError(message) {
        // Create error toast notification
        const toast = document.createElement('div');
        toast.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: #ff4444;
            color: white;
            padding: 16px 24px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            z-index: 10001;
            max-width: 400px;
            display: flex;
            align-items: start;
            gap: 12px;
            animation: slideIn 0.3s ease-out;
        `;

        toast.innerHTML = `
            <div style="font-size: 20px; flex-shrink: 0;">❌</div>
            <div style="flex: 1;">
                <div style="font-weight: bold; margin-bottom: 4px;">Error</div>
                <div style="font-size: 14px; opacity: 0.9; line-height: 1.4;">${message}</div>
            </div>
            <button id="errorToastClose" style="
                background: transparent;
                border: none;
                color: white;
                font-size: 20px;
                cursor: pointer;
                padding: 0;
                width: 24px;
                height: 24px;
                display: flex;
                align-items: center;
                justify-content: center;
                opacity: 0.7;
                flex-shrink: 0;
            ">×</button>
        `;

        // Add animation if not already present
        if (!document.getElementById('toastAnimationStyle')) {
            const style = document.createElement('style');
            style.id = 'toastAnimationStyle';
            style.textContent = `
                @keyframes slideIn {
                    from {
                        transform: translateX(400px);
                        opacity: 0;
                    }
                    to {
                        transform: translateX(0);
                        opacity: 1;
                    }
                }
            `;
            document.head.appendChild(style);
        }

        document.body.appendChild(toast);

        // Close button handler
        const closeBtn = document.getElementById('errorToastClose');
        const removeToast = () => {
            toast.style.transition = 'opacity 0.3s ease-out, transform 0.3s ease-out';
            toast.style.opacity = '0';
            toast.style.transform = 'translateX(400px)';
            setTimeout(() => {
                if (toast.parentNode) {
                    document.body.removeChild(toast);
                }
            }, 300);
        };

        closeBtn.onclick = removeToast;

        // Auto-remove after 8 seconds (longer for error messages)
        setTimeout(removeToast, 8000);

        console.error('[UI] Error shown:', message);
    }

    /**
     * Track event listener for cleanup
     */
    addTrackedListener(element, event, handler, options) {
        element.addEventListener(event, handler, options);
        this.eventListeners.push({ element, event, handler, options });
    }

    /**
     * Remove all tracked event listeners
     */
    removeAllListeners() {
        this.eventListeners.forEach(({ element, event, handler, options }) => {
            element.removeEventListener(event, handler, options);
        });
        this.eventListeners = [];
    }

    /**
     * Complete cleanup
     */
    cleanup() {
        this.stopCamera();
        this.removeAllListeners();
    }
}

// Initialize app when DOM is ready
const app = new LinuxFaceApp();

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => app.init());
} else {
    app.init();
}

export { app };
