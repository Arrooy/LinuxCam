/**
 * Camera Management Module
 * Handles MediaStream, device switching, and camera lifecycle
 */

import { state } from './state.js';

export class CameraManager {
    constructor() {
        this.captureCanvas = null;
        this.captureCtx = null;
        this.localVideo = null;
        this.availableDevices = [];
    }

    initialize(localVideo, captureCanvas) {
        this.localVideo = localVideo;
        this.captureCanvas = captureCanvas;

        // Optimize canvas context for best performance
        this.captureCtx = captureCanvas.getContext('2d', {
            alpha: false, // No transparency for better performance
            desynchronized: true, // Allow faster rendering by not syncing to display
            willReadFrequently: false, // Hint that we only write to canvas
            colorSpace: 'srgb' // Explicit color space for faster processing
        });
    }

    /**
     * Enumerate available video input devices
     */
    async enumerateDevices() {
        try {
            const devices = await navigator.mediaDevices.enumerateDevices();
            this.availableDevices = devices.filter(device => device.kind === 'videoinput');
            console.log(`[Camera] Found ${this.availableDevices.length} video devices:`,
                this.availableDevices.map(d => ({ id: d.deviceId, label: d.label })));
            return this.availableDevices;
        } catch (error) {
            console.error('[Camera] Failed to enumerate devices:', error);
            return [];
        }
    }

    /**
     * Get list of available cameras
     */
    getAvailableDevices() {
        return this.availableDevices;
    }

    /**
     * Test each enumerated camera device with current resolution constraints to see which can be opened
     * Returns an array of { deviceId, label, ok, error }
     */
    async testDevices({ width, height, timeout = 2000 } = {}) {
        const results = [];
        const devices = this.availableDevices;
        for (const dev of devices) {
            let tempStream = null;
            try {
                const constraints = { video: { deviceId: { exact: dev.deviceId } } };
                if (width && height) {
                    constraints.video.width = { ideal: width };
                    constraints.video.height = { ideal: height };
                }
                const promise = navigator.mediaDevices.getUserMedia({ video: constraints.video, audio: false });
                const race = Promise.race([
                    promise,
                    new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), timeout))
                ]);
                tempStream = await race;
                // success
                results.push({ deviceId: dev.deviceId, label: dev.label, ok: true });
            } catch (err) {
                results.push({ deviceId: dev.deviceId, label: dev.label, ok: false, error: err.message || err.name });
            } finally {
                if (tempStream) {
                    tempStream.getTracks().forEach(t => t.stop());
                }
            }
        }
        return results;
    }

    /**
     * Start camera with configured resolution and device
     */
    async startCamera() {
        const settings = state.get('settings');
        const [width, height] = settings.resolution.split('x').map(Number);
        const selectedDeviceId = settings.deviceId;

        // Build video constraints
        const videoConstraints = {
            width: { ideal: width },
            height: { ideal: height }
        };

        // If specific device selected, use deviceId; otherwise use facingMode
        if (selectedDeviceId && selectedDeviceId !== 'default') {
            videoConstraints.deviceId = { exact: selectedDeviceId };
            console.log('[Camera] Using selected device:', selectedDeviceId);
        } else {
            const facingMode = state.get('currentFacingMode');
            videoConstraints.facingMode = facingMode;
            console.log('[Camera] Using facingMode:', facingMode);
        }

        try {
            console.log('[Camera] Trying constraints:', JSON.stringify(videoConstraints));
            let stream;
            let tempStream;

            try {
                // First try with desired constraints (could be deviceId or facingMode)
                stream = await navigator.mediaDevices.getUserMedia({ video: videoConstraints, audio: false });
            } catch (e) {
                console.warn('[Camera] Initial getUserMedia failed:', e.name, e.message);

                if (videoConstraints.deviceId) {
                    // If we were using a specific device, try facingMode fallback first
                    const facingMode = state.get('currentFacingMode');
                    console.log('[Camera] Falling back to facingMode:', facingMode);
                    try {
                        tempStream = await navigator.mediaDevices.getUserMedia({ video: { width: { ideal: width }, height: { ideal: height }, facingMode }, audio: false });
                        stream = tempStream;
                        tempStream = null;
                        console.log('[Camera] Fallback to facingMode succeeded');
                    } catch (fallbackError) {
                        console.warn('[Camera] Fallback to facingMode failed:', fallbackError.name, fallbackError.message);
                        // Last-resort: iterate through other enumerated devices and try them by deviceId
                        const devices = this.availableDevices.filter(d => d.deviceId !== videoConstraints.deviceId.exact);
                        for (const dev of devices) {
                            try {
                                console.log('[Camera] Trying alternative deviceId:', dev.deviceId);
                                tempStream = await navigator.mediaDevices.getUserMedia({ video: { deviceId: { exact: dev.deviceId }, width: { ideal: width }, height: { ideal: height } }, audio: false });
                                stream = tempStream;
                                tempStream = null;
                                console.log('[Camera] Opened alternative device:', dev.deviceId);
                                break; // success
                            } catch (tryErr) {
                                console.warn('[Camera] Failed to open device', dev.deviceId, tryErr.name, tryErr.message);
                                if (tempStream) {
                                    tempStream.getTracks().forEach(t => t.stop());
                                    tempStream = null;
                                }
                            }
                        }
                    }
                } else {
                    // No deviceId specified — try relaxing constraints first
                    console.log('[Camera] No deviceId specified; trying relax constraints');
                    try {
                        tempStream = await navigator.mediaDevices.getUserMedia({ video: { width: { ideal: width }, height: { ideal: height } }, audio: false });
                        stream = tempStream;
                        tempStream = null;
                        console.log('[Camera] Relaxed constraint succeeded');
                    } catch (relaxError) {
                        console.warn('[Camera] Relaxed constraint failed:', relaxError.name, relaxError.message);
                        // Try enumerating devices to find any that work
                        const devices = this.availableDevices;
                        for (const dev of devices) {
                            try {
                                console.log('[Camera] Trying enumerated deviceId:', dev.deviceId);
                                tempStream = await navigator.mediaDevices.getUserMedia({ video: { deviceId: { exact: dev.deviceId }, width: { ideal: width }, height: { ideal: height } }, audio: false });
                                stream = tempStream;
                                tempStream = null;
                                console.log('[Camera] Opened enumerated device:', dev.deviceId);
                                break; // success
                            } catch (tryErr) {
                                console.warn('[Camera] Failed to open enumerated device', dev.deviceId, tryErr.name, tryErr.message);
                                if (tempStream) {
                                    tempStream.getTracks().forEach(t => t.stop());
                                    tempStream = null;
                                }
                            }
                        }
                    }
                }
            }

            if (!stream) {
                const msg = 'Unable to open any video device; check camera privacy, other apps, or hardware';
                console.error('[Camera] ' + msg);
                throw new Error(msg);
            }

            this.localVideo.srcObject = stream;

            // Update selected device in settings when available so UI can reflect actual device used
            try {
                const openedDeviceId = stream.getVideoTracks()[0].getSettings().deviceId;
                if (openedDeviceId) {
                    // Update settings if different
                    const currentSettings = state.get('settings');
                    if (currentSettings.deviceId !== openedDeviceId) {
                        console.log('[Camera] Updating settings.deviceId to opened device:', openedDeviceId);
                        state.updateSettings({ deviceId: openedDeviceId });
                    }
                }
            } catch (err) {
                // Some browsers may not expose deviceId in getSettings(); ignore silently
            }

            state.setState({
                localStream: stream,
                cameraActive: true
            });

            // After permission granted, enumerate devices to get labels
            await this.enumerateDevices();

            // Update canvas when video loads (only if dimensions actually changed)
            this.localVideo.onloadedmetadata = () => {
                const newWidth = this.localVideo.videoWidth;
                const newHeight = this.localVideo.videoHeight;

                // Only update if dimensions actually changed (avoid resize thrashing)
                if (this.captureCanvas.width !== newWidth || this.captureCanvas.height !== newHeight) {
                    this.captureCanvas.width = newWidth;
                    this.captureCanvas.height = newHeight;
                    console.log('[Camera] Canvas resized to:', newWidth, 'x', newHeight);
                } else {
                    console.log('[Camera] Canvas already at:', newWidth, 'x', newHeight, '(no resize needed)');
                }
            };

            return stream;

        } catch (error) {
            console.error('[Camera] Access failed:', error);
            throw new Error(`Camera access denied: ${error.message}`);
        }
    }

    /**
     * Stop camera and release resources
     */
    stopCamera() {
        const stream = state.get('localStream');

        if (stream) {
            stream.getTracks().forEach(track => {
                track.stop();
                console.log('Stopped track:', track.kind);
            });
        }

        if (this.localVideo.srcObject) {
            this.localVideo.srcObject = null;
        }

        state.setState({
            localStream: null,
            cameraActive: false
        });

        console.log('[Camera] Stopped');
    }

    /**
     * Switch between front/back camera
     */
    async switchCamera() {
        const currentMode = state.get('currentFacingMode');
        const newMode = currentMode === 'user' ? 'environment' : 'user';

        console.log(`[Camera] Switching: ${currentMode} -> ${newMode}`);

        // Stop current stream
        this.stopCamera();

        // Update facing mode
        state.setState({ currentFacingMode: newMode });

        // Start new stream
        try {
            await this.startCamera();
            console.log(`[Camera] Switched to ${newMode} camera`);
        } catch (error) {
            // Restore previous camera on failure
            console.error('[Camera] Switch failed, restoring previous:', error);
            state.setState({ currentFacingMode: currentMode });
            await this.startCamera();
            throw error;
        }
    }

    /**
     * Capture current frame to canvas (with mirroring for front camera)
     * Optimized for minimal latency and maximum performance
     */
    captureFrame() {
        const facingMode = state.get('currentFacingMode');

        // Performance optimization: Only save/restore context when mirroring is needed
        if (facingMode === 'user') {
            // Mirror for front camera (requires context transformation)
            this.captureCtx.save();
            this.captureCtx.scale(-1, 1);
            this.captureCtx.drawImage(
                this.localVideo,
                -this.captureCanvas.width, 0,
                this.captureCanvas.width,
                this.captureCanvas.height
            );
            this.captureCtx.restore();
        } else {
            // Normal for back camera (direct draw - fastest path)
            this.captureCtx.drawImage(
                this.localVideo,
                0, 0,
                this.captureCanvas.width,
                this.captureCanvas.height
            );
        }
    }
}

export const cameraManager = new CameraManager();
