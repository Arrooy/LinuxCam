/**
 * WebRTC Module
 * Manages bidirectional H.264 video streaming over WebRTC DataChannel with hardware acceleration
 */

import { state, ConnectionState } from './state.js';
import { websocketManager } from './websocket.js';
import { cameraManager } from './camera.js';

class WebRTCManager {
    constructor() {
        this.peerConnection = null;
        this.dataChannel = null;
        this.receivedCanvas = null;
        this.peerId = 'browser_' + Date.now();
        this.frameInterval = null;
        this.videoEncoder = null;
        this.videoDecoder = null;
        this.encodedChunks = [];
        this.pendingEncodeResolve = null; // Promise resolver for encoder output synchronization
        this.supportsWebCodecs = typeof VideoEncoder !== 'undefined' && typeof VideoDecoder !== 'undefined';
        this.encodingMode = null; // 'hardware-h264', 'software-h264', or 'jpeg'
        this.decodingMode = null; // 'hardware-h264', 'software-h264', or 'jpeg'
        this.pendingIceCandidates = []; // Buffer ICE candidates until answer is received
        this.receivedAnswer = false;
        this.connectionTimeout = null;
        this.frameChunkBuffer = new Map();

        // Track decoder config (SPS/PPS) to ensure it's sent before any P-frames
        this.decoderConfigSent = false;
        this.cachedDecoderConfig = null;
        this.decoderConfigSentToServer = false; // Track if we've sent AVCC to server
        this.framesSentCount = 0; // Track frames actually sent to server
    }

    calculateBitrateForResolution(pixels) {
        if (pixels >= 1920 * 1080) return 8_000_000;
        if (pixels >= 1280 * 720) return 5_000_000;
        if (pixels >= 960 * 540) return 2_500_000;
        return 1_500_000;
    }

    /**
     * Initialize WebRTC with hardware-accelerated H.264 encoding and decoding
     * @param {HTMLCanvasElement} canvas - Canvas for rendering received video frames
     * @returns {Promise<boolean>} Success status
     */
    async initialize(canvas) {
        this.receivedCanvas = canvas;

        if (!this.supportsWebCodecs) {
            this.showCompatibilityError('WebCodecs API not supported', 'Your browser does not support the WebCodecs API required for hardware-accelerated H.264 encoding/decoding. Please use a modern Chromium-based browser (Chrome 94+, Edge 94+) or Firefox 102+.');
            return false;
        }

        const encoderSuccess = await this.findWorkingH264Encoder();
        if (!encoderSuccess) {
            this.showCompatibilityError('No H.264 Encoder Available', 'Unable to initialize any H.264 encoder (hardware or software). This may be due to missing GPU drivers, browser restrictions, or unsupported hardware. Please check your browser settings and GPU drivers.');
            return false;
        }

        const decoderSuccess = await this.findWorkingH264Decoder();
        if (!decoderSuccess) {
            this.showCompatibilityError('No H.264 Decoder Available', 'Unable to initialize any H.264 decoder (hardware or software). This may be due to missing GPU drivers, browser restrictions, or unsupported hardware. Please check your browser settings and GPU drivers.');
            return false;
        }

        console.log(`[WebRTC] Successfully initialized with encoding: ${this.encodingMode}, decoding: ${this.decodingMode}`);
        return true;
    }

    /**
     * Show compatibility error modal when H.264 support is unavailable
     * @param {string} title - Error title
     * @param {string} message - Detailed error message
     */
    showCompatibilityError(title, message) {
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
                <button id="webrtcErrorClose" style="
                    background: #007bff;
                    color: white;
                    border: none;
                    border-radius: 6px;
                    padding: 10px 20px;
                    cursor: pointer;
                    font-size: 14px;
                ">OK</button>
            </div>
        `;

        overlay.appendChild(modal);
        document.body.appendChild(overlay);

        // Close modal handler
        const closeModal = () => {
            document.body.removeChild(overlay);
        };

        document.getElementById('webrtcErrorClose').onclick = closeModal;
        overlay.onclick = (e) => {
            if (e.target === overlay) closeModal();
        };

        console.error(`[WebRTC] Compatibility Error: ${title} - ${message}`);
    }

    /**
     * Find a working H.264 encoder by testing multiple codec profiles
     * Tries hardware acceleration first, falls back to software if needed
     * @returns {Promise<boolean>} Success status
     */
    async findWorkingH264Encoder() {
        // List of H.264 codec profiles to try, ordered by preference (baseline to high)
        const codecProfiles = [
            'avc1.42001E', // H.264 Baseline Profile Level 3.0 (most compatible)
            'avc1.42001F', // H.264 Baseline Profile Level 3.1
            'avc1.420028', // H.264 Baseline Profile Level 4.0
            'avc1.42002A', // H.264 Baseline Profile Level 4.2
            'avc1.4D001E', // H.264 Main Profile Level 3.0
            'avc1.4D001F', // H.264 Main Profile Level 3.1
            'avc1.4D0028', // H.264 Main Profile Level 4.0
            'avc1.64001E', // H.264 High Profile Level 3.0
            'avc1.64001F', // H.264 High Profile Level 3.1
        ];

        const localStream = state.get('localStream');
        const videoTrack = localStream?.getVideoTracks()[0];
        if (!videoTrack) {
            console.warn('[WebRTC] No video track available for encoder detection');
            return false;
        }

        const settings = videoTrack.getSettings();
        const width = settings.width || 640;
        const height = settings.height || 480;
        const pixels = width * height;
        const bitrate = this.calculateBitrateForResolution(pixels);
        const isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
        const framerate = isMobile ? 24 : 30;

        console.log('[WebRTC] Testing H.264 encoder support (hardware preference)...');
        for (const codec of codecProfiles) {
            const config = {
                codec: codec,
                width: width,
                height: height,
                bitrate: bitrate,
                framerate: framerate,
                latencyMode: 'realtime',
                hardwareAcceleration: 'prefer-hardware',
                bitrateMode: 'constant',
                scalabilityMode: 'L1T1',
                alpha: 'discard'
            };

            try {
                const support = await VideoEncoder.isConfigSupported(config);
                if (support.supported) {
                    console.log(`[WebRTC] Hardware H.264 encoder supported: ${codec}`);
                    const success = await this.initializeH264Encoder(false, codec, true);
                    if (success) {
                        console.log(`[WebRTC] Hardware H.264 encoder successfully initialized: ${codec}`);
                        return true;
                    } else {
                        console.warn(`[WebRTC] Hardware encoder ${codec} supported but initialization failed`);
                    }
                } else {
                    console.log(`[WebRTC] Hardware encoder not supported: ${codec}`);
                }
            } catch (error) {
                console.warn(`[WebRTC] Error testing hardware encoder ${codec}:`, error);
            }
        }

        // Fallback to software encoding
        console.log('[WebRTC] No hardware encoder found, testing software encoding...');
        for (const codec of codecProfiles) {
            const config = {
                codec: codec,
                width: width,
                height: height,
                bitrate: bitrate,
                framerate: framerate,
                latencyMode: 'realtime',
                hardwareAcceleration: 'prefer-software',
                bitrateMode: 'constant',
                scalabilityMode: 'L1T1',
                alpha: 'discard'
            };

            try {
                const support = await VideoEncoder.isConfigSupported(config);
                if (support.supported) {
                    console.log(`[WebRTC] Software H.264 encoder supported: ${codec}`);
                    const success = await this.initializeH264Encoder(true, codec, true);
                    if (success) {
                        console.log(`[WebRTC] Software H.264 encoder successfully initialized: ${codec}`);
                        return true;
                    } else {
                        console.warn(`[WebRTC] Software encoder ${codec} supported but initialization failed`);
                    }
                } else {
                    console.log(`[WebRTC] Software encoder not supported: ${codec}`);
                }
            } catch (error) {
                console.warn(`[WebRTC] Error testing software encoder ${codec}:`, error);
            }
        }

        console.error('[WebRTC] No working H.264 encoder found after testing all profiles');
        return false;
    }

    /**
     * Find a working H.264 decoder by trying multiple codec profiles and configurations
     * @returns {Promise<boolean>} Success status
     */
    async findWorkingH264Decoder() {
        // List of H.264 codec profiles to try, ordered by preference
        const codecProfiles = [
            'avc1.42E01F', // H.264 Baseline Profile Level 3.1 (widely supported)
            'avc1.42E01E', // H.264 Baseline Profile Level 3.0
            'avc1.42E028', // H.264 Baseline Profile Level 4.0
            'avc1.42E02A', // H.264 Baseline Profile Level 4.2
            'avc1.4DE01F', // H.264 Main Profile Level 3.1
            'avc1.4DE01E', // H.264 Main Profile Level 3.0
            'avc1.4DE028', // H.264 Main Profile Level 4.0
            'avc1.64E01F', // H.264 High Profile Level 3.1
            'avc1.64E01E', // H.264 High Profile Level 3.0
        ];

        // Try hardware acceleration first
        console.log('[WebRTC] Testing H.264 decoder support (hardware preference)...');
        for (const codec of codecProfiles) {
            const config = {
                codec: codec,
                hardwareAcceleration: 'prefer-hardware',
                optimizeForLatency: true,
                lowLatency: true,
                powerEfficient: false
            };

            try {
                const support = await VideoDecoder.isConfigSupported(config);
                if (support.supported) {
                    console.log(`[WebRTC] Hardware H.264 decoder supported: ${codec}`);
                    const success = await this.initializeH264Decoder(false, codec, true);
                    if (success) {
                        console.log(`[WebRTC] Hardware H.264 decoder successfully initialized: ${codec}`);
                        return true;
                    } else {
                        console.warn(`[WebRTC] Hardware decoder ${codec} supported but initialization failed`);
                    }
                } else {
                    console.log(`[WebRTC] Hardware decoder not supported: ${codec}`);
                }
            } catch (error) {
                console.warn(`[WebRTC] Error testing hardware decoder ${codec}:`, error);
            }
        }

        // Fallback to software decoding
        console.log('[WebRTC] No hardware decoder found, testing software decoding...');
        for (const codec of codecProfiles) {
            const config = {
                codec: codec,
                hardwareAcceleration: 'prefer-software',
                optimizeForLatency: true,
                lowLatency: true,
                powerEfficient: false
            };

            try {
                const support = await VideoDecoder.isConfigSupported(config);
                if (support.supported) {
                    console.log(`[WebRTC] Software H.264 decoder supported: ${codec}`);
                    const success = await this.initializeH264Decoder(true, codec, true);
                    if (success) {
                        console.log(`[WebRTC] Software H.264 decoder successfully initialized: ${codec}`);
                        return true;
                    } else {
                        console.warn(`[WebRTC] Software decoder ${codec} supported but initialization failed`);
                    }
                } else {
                    console.log(`[WebRTC] Software decoder not supported: ${codec}`);
                }
            } catch (error) {
                console.warn(`[WebRTC] Error testing software decoder ${codec}:`, error);
            }
        }

        console.error('[WebRTC] No working H.264 decoder found after testing all profiles');
        return false;
    }
    /**
     * Initialize WebCodecs H.264 encoder with hardware or software acceleration
     * @param {boolean} forceSoftware - Force software encoding
     * @param {string} codec - Specific H.264 codec profile to use
     * @param {boolean} suppressErrorModal - Don't show error modal (for codec search)
     * @returns {Promise<boolean>} Success status
     */
    async initializeH264Encoder(forceSoftware = false, codec = 'avc1.42001E', suppressErrorModal = false) {
        try {
            const localStream = state.get('localStream');
            const videoTrack = localStream?.getVideoTracks()[0];
            if (!videoTrack) {
                console.warn('[WebRTC] No video track available for encoder initialization');
                return false;
            }

            const settings = videoTrack.getSettings();
            const width = settings.width || 640;
            const height = settings.height || 480;
            const pixels = width * height;
            const bitrate = this.calculateBitrateForResolution(pixels);
            const isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
            const framerate = isMobile ? 24 : 30;
            const accelerationMode = forceSoftware ? 'prefer-software' : 'prefer-hardware';

            const config = {
                codec: codec,
                width: width,
                height: height,
                bitrate: bitrate,
                framerate: framerate,
                latencyMode: 'realtime',
                hardwareAcceleration: accelerationMode,
                bitrateMode: 'constant',
                scalabilityMode: 'L1T1',
                alpha: 'discard',
                // NOTE: Browsers may output AVCC despite requesting Annex B
                avc: {
                    format: 'annexb'
                }
            };

            // Validate configuration support before creating encoder
            const support = await VideoEncoder.isConfigSupported(config);
            if (!support.supported) {
                console.warn(`[WebRTC] Encoder configuration not supported: ${codec} (${accelerationMode})`);
                return false;
            }

            // Log what the browser actually supports vs what we requested
            if (support.config) {
                const actualConfig = support.config;
                console.log(`[WebRTC] Browser encoder config: ${JSON.stringify({
                    codec: actualConfig.codec,
                    hardwareAcceleration: actualConfig.hardwareAcceleration,
                    width: actualConfig.width,
                    height: actualConfig.height,
                    bitrate: actualConfig.bitrate,
                    framerate: actualConfig.framerate
                })}`);
            }

            this.videoEncoder = new VideoEncoder({
                output: (chunk, metadata) => {
                    const chunkData = new Uint8Array(chunk.byteLength);
                    chunk.copyTo(chunkData);

                    let decoderConfig = null;
                    if (metadata?.decoderConfig?.description) {
                        decoderConfig = new Uint8Array(metadata.decoderConfig.description);
                        console.log('[WebRTC] Extracted decoder config from metadata (SPS/PPS):', decoderConfig.length, 'bytes');
                    } else if (chunk.type === 'key' && !this.cachedDecoderConfig) {
                        // Safari HW encoder doesn't provide decoderConfig in metadata.
                        // Extract SPS/PPS from Annex-B keyframe data and build AVCC.
                        const avcc = this.extractAvccFromAnnexB(chunkData);
                        if (avcc) {
                            decoderConfig = avcc;
                            console.log('[WebRTC] Extracted decoder config from Annex-B keyframe:', decoderConfig.length, 'bytes');
                        } else {
                            console.warn('[WebRTC] Keyframe has no SPS/PPS NALs - encoder may not be configured correctly');
                        }
                    }
                    
                    if (decoderConfig) {
                        // Cache decoder config for reuse until confirmed sent
                        const isFirstConfig = !this.cachedDecoderConfig;
                        this.cachedDecoderConfig = decoderConfig;

                        // Send config to server immediately if DataChannel is open
                        // Server needs AVCC extradata before it can decode any frames
                        if (isFirstConfig) {
                            this.sendDecoderConfigToServer();
                        }
                    }

                    const firstBytes = Array.from(chunkData.slice(0, 16))
                        .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' ');
                    console.log(`[WebRTC] Encoder output (${chunk.type}): ${chunkData.length} bytes, first bytes: ${firstBytes}, hasConfig: ${!!decoderConfig}`);

                    this.encodedChunks.push({
                        data: chunkData,
                        timestamp: chunk.timestamp,
                        type: chunk.type,
                        duration: chunk.duration,
                        decoderConfig: decoderConfig
                    });

                    // Signal that chunks are ready for any waiting encode operations
                    if (this.pendingEncodeResolve) {
                        this.pendingEncodeResolve();
                        this.pendingEncodeResolve = null;
                    }
                },
                error: (error) => {
                    console.error('[WebRTC] VideoEncoder error:', error);
                    if (!suppressErrorModal) {
                        this.showCompatibilityError('H.264 Encoder Failed', `The H.264 encoder encountered an error: ${error.message}. This may be due to hardware limitations or browser restrictions. Please try refreshing the page or using a different browser.`);
                    }
                }
            });

            this.videoEncoder.configure(config);

            await new Promise((resolve) => setTimeout(resolve, 100));

            if (this.videoEncoder.state === 'configured') {
                this.encodingMode = forceSoftware ? 'software-h264' : 'hardware-h264';
                console.log(`[WebRTC] H.264 encoder configured (${this.encodingMode}): ${width}x${height} @ ${(bitrate / 1000000).toFixed(1)}Mbps, ${framerate}fps`);
                return true;
            } else {
                console.warn(`[WebRTC] Encoder state: ${this.videoEncoder.state}`);
                this.videoEncoder = null;
                return false;
            }

        } catch (error) {
            console.error(`[WebRTC] Failed to initialize H.264 encoder (${forceSoftware ? 'software' : 'hardware'}):`, error);
            this.videoEncoder = null;
            return false;
        }
    }

    /**
     * Initialize WebCodecs H.264 decoder with hardware or software acceleration
     * @param {boolean} forceSoftware - Force software decoding
     * @param {string} codec - Specific H.264 codec profile to use
     * @param {boolean} suppressErrorModal - Don't show error modal (for codec search)
     * @returns {Promise<boolean>} Success status
     */
    async initializeH264Decoder(forceSoftware = false, codec = 'avc1.42E01F', suppressErrorModal = false) {
        try {
            const decoderConfig = {
                codec: codec, // Use the provided codec profile
                hardwareAcceleration: forceSoftware ? 'prefer-software' : 'prefer-hardware',
                optimizeForLatency: true,
                // Additional low-latency decoder hints
                lowLatency: true,
                powerEfficient: false // Prioritize speed over power consumption
            };

            // Test if configuration is supported before creating decoder
            const support = await VideoDecoder.isConfigSupported(decoderConfig);
            if (!support.supported) {
                console.warn(`[WebRTC] Decoder configuration not supported: ${codec} (${decoderConfig.hardwareAcceleration})`);
                return false;
            }

            // Log what the browser actually supports vs what we requested
            if (support.config) {
                const actualConfig = support.config;
                console.log(`[WebRTC] Browser decoder config: ${JSON.stringify({
                    codec: actualConfig.codec,
                    hardwareAcceleration: actualConfig.hardwareAcceleration,
                    optimizeForLatency: actualConfig.optimizeForLatency
                })}`);
            }

            // Setup canvas for rendering decoded frames (Firefox-compatible approach)
            this.receivedCanvas = document.getElementById('receivedCanvas');
            if (!this.receivedCanvas) {
                throw new Error('receivedCanvas element not found');
            }

            // Optimize canvas context for fastest rendering
            this.receivedCtx = this.receivedCanvas.getContext('2d', {
                alpha: false,
                desynchronized: true,
                willReadFrequently: false,
                colorSpace: 'srgb'
            });

            console.log('[WebRTC] Canvas configured for direct VideoFrame rendering');

            this.videoDecoder = new VideoDecoder({
                output: (frame) => {
                    let frameClosed = false;
                    try {
                        // CRITICAL: Must draw frame synchronously before closing
                        // requestAnimationFrame would cause "VideoFrame has been closed" error

                        if (this.receivedCanvas.width !== frame.displayWidth ||
                            this.receivedCanvas.height !== frame.displayHeight) {
                            this.receivedCanvas.width = frame.displayWidth;
                            this.receivedCanvas.height = frame.displayHeight;
                            console.log('[WebRTC] Canvas resized:', frame.displayWidth, 'x', frame.displayHeight);
                        }

                        this.receivedCtx.drawImage(frame, 0, 0);

                        frame.close();
                        frameClosed = true;

                        const timestamps = state.get('receivedFrameTimestamps');
                        timestamps.push(performance.now());

                        const cutoff = performance.now() - 3000;
                        const recent = timestamps.filter(t => t > cutoff);
                        const newFrameCount = state.get('framesReceived') + 1;
                        state.setState({
                            receivedFrameTimestamps: recent,
                            framesReceived: newFrameCount
                        });

                        if (recent.length > 1 && (newFrameCount % 10) === 0) {
                            const timeSpan = performance.now() - recent[0];
                            const fps = Math.round((recent.length - 1) * 1000 / timeSpan);
                            document.getElementById('fpsDisplay').textContent = `${fps} FPS`;
                        }
                    } catch (error) {
                        console.error('[WebRTC] Error rendering decoded frame:', error);
                        if (!frameClosed) {
                            try {
                                frame.close();
                            } catch (closeError) {
                                // Ignore if frame was already closed
                            }
                        }
                    }
                },
                error: (error) => {
                    console.error('[WebRTC] VideoDecoder error:', error);
                    // Only show error modal if not suppressed (during codec search)
                    if (!suppressErrorModal) {
                        this.showCompatibilityError('H.264 Decoder Failed', `The H.264 decoder encountered an error: ${error.message}. This may be due to hardware limitations or browser restrictions. Please try refreshing the page or using a different browser.`);
                    }
                }
            });

            this.videoDecoder.configure(decoderConfig);

            // Test if decoder actually works by checking state after a brief delay
            await new Promise((resolve) => setTimeout(resolve, 100));

            if (this.videoDecoder.state === 'configured') {
                this.decodingMode = forceSoftware ? 'software-h264' : 'hardware-h264';
                console.log(`[WebRTC] H.264 decoder configured (${this.decodingMode}): ${codec}`);
                return true;
            } else {
                console.warn(`[WebRTC] Decoder state: ${this.videoDecoder.state}`);
                this.videoDecoder = null;
                return false;
            }

        } catch (error) {
            console.error(`[WebRTC] Failed to initialize H.264 decoder (${forceSoftware ? 'software' : 'hardware'}):`, error);
            this.videoDecoder = null;
            return false;
        }
    }

    /**
     * Create WebRTC peer connection and data channel
     */
    async connect() {
        try {
            // Verify that we have valid encoders/decoders before proceeding
            if (!this.encodingMode || !(this.encodingMode === 'hardware-h264' || this.encodingMode === 'software-h264')) {
                console.error('[WebRTC] Cannot connect: No valid H.264 encoder available');
                throw new Error('No valid H.264 encoder available for WebRTC connection');
            }

            if (!this.decodingMode || !(this.decodingMode === 'hardware-h264' || this.decodingMode === 'software-h264')) {
                console.error('[WebRTC] Cannot connect: No valid H.264 decoder available');
                throw new Error('No valid H.264 decoder available for WebRTC connection');
            }

            console.log('[WebRTC] Step 1: Creating peer connection...');
            // Create peer connection
            this.peerConnection = new RTCPeerConnection({
                iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
            });
            console.log('[WebRTC] Step 1 complete: Peer connection created');

            console.log('[WebRTC] Step 2: Creating data channel...');
            // Create data channel for bidirectional frame transfer
            this.dataChannel = this.peerConnection.createDataChannel('cameraFrames', {
                ordered: false,  // Allow out-of-order delivery for lower latency
                maxRetransmits: 0  // Don't retransmit dropped packets
            });
            console.log('[WebRTC] Step 2 complete: Data channel created');

            console.log('[WebRTC] Step 3: Setting up event handlers...');
            this.setupDataChannel();
            this.setupPeerConnection();
            console.log('[WebRTC] Step 3 complete: Event handlers set up');

            // Note: We receive frames via DataChannel, not RTP video track
            // This allows us to use WebCodecs for hardware-accelerated H.264 decoding

            console.log('[WebRTC] Step 4: Creating SDP offer...');
            // Create offer
            const offer = await this.peerConnection.createOffer();
            console.log('[WebRTC] Step 4a: Offer created, setting local description...');
            await this.peerConnection.setLocalDescription(offer);
            console.log('[WebRTC] Step 4 complete: Local description set');

            console.log('[WebRTC] Step 5: Waiting for ICE gathering (current state:', this.peerConnection.iceGatheringState + ')...');
            // Wait for ICE gathering to complete
            await this.waitForICEGathering();
            console.log('[WebRTC] Step 5 complete: ICE gathering finished (state:', this.peerConnection.iceGatheringState + ')');

            console.log('[WebRTC] Step 6: Preparing offer for transmission...');
            // Send offer to server via WebSocket
            const signaling = {
                type: 'browser_offer',
                peerId: this.peerId,
                sdp: this.peerConnection.localDescription.sdp
            };

            console.log('[WebRTC] Step 6a: Offer prepared:', {
                type: signaling.type,
                peerId: signaling.peerId,
                sdpLength: signaling.sdp.length,
                sdpPreview: signaling.sdp.substring(0, 100),
                iceGatheringState: this.peerConnection.iceGatheringState,
                connectionState: this.peerConnection.connectionState
            });
            
            console.log('[WebRTC] Step 6b: Serializing offer...');
            // Serialize signaling data and validate it before sending
            const signalingMessage = 'WEBRTC:' + JSON.stringify(signaling);
            console.log('[WebRTC] Step 6c: Serialized message:', {
                length: signalingMessage.length,
                first200: signalingMessage.substring(0, 200),
                charCodes: Array.from(signalingMessage.substring(0, 20)).map(c => c.charCodeAt(0)).join(',')
            });

            console.log('[WebRTC] Step 6d: Setting up connection timeout...');
            // Set up connection timeout before sending offer
            this.connectionTimeout = setTimeout(() => {
                if (!this.receivedAnswer) {
                    console.error('[WebRTC] Connection timeout: No answer received from server within 10 seconds');
                    const error = new Error('WebRTC connection timeout: Server did not respond to offer within 10 seconds. The server may be offline or overloaded.');
                    throw error;
                }
            }, 10000); // 10 second timeout

            console.log('[WebRTC] Step 6e: Sending offer via WebSocket...');
            console.log('[WebRTC] WebSocket readyState:', state.get('ws')?.readyState, '(1=OPEN)');
            const sendSuccess = websocketManager.send(signalingMessage);
            if (!sendSuccess) {
                console.error('[WebRTC] Step 6 FAILED: WebSocket send returned false');
                clearTimeout(this.connectionTimeout);
                throw new Error('Failed to send WebRTC offer: WebSocket not connected');
            }

            console.log('[WebRTC] Step 6 complete: Offer sent successfully');
            state.setState({ lastServerResponseTime: Date.now() }); // Reset timeout when sending offer
            console.log('[WebRTC] Step 7: Waiting for server answer...');

        } catch (error) {
            console.error('[WebRTC] Failed to connect:', error);
            if (this.connectionTimeout) {
                clearTimeout(this.connectionTimeout);
                this.connectionTimeout = null;
            }
            throw error;
        }
    }

    /**
     * Setup data channel event handlers
     */
    setupDataChannel() {
        this.dataChannel.onopen = () => {
            console.log('[WebRTC] Data channel opened - connection established!');

            // Send cached decoder config (AVCC) to server immediately if available
            // Server needs this to initialize FFmpeg decoder with proper SPS/PPS
            this.sendDecoderConfigToServer();

            // Check if we have a valid encoder before starting streaming
            if (!this.encodingMode || !(this.encodingMode === 'hardware-h264' || this.encodingMode === 'software-h264')) {
                console.error('[WebRTC] Data channel opened but no valid H.264 encoder available. Cannot start streaming.');
                this.showCompatibilityError('Streaming Unavailable', 'The data channel connected successfully, but no H.264 encoder is available for streaming. Please check your browser compatibility and try refreshing the page.');
                return;
            }

            console.log(`[WebRTC] Starting frame streaming with ${this.encodingMode} encoding...`);
            state.setState({ connectionState: ConnectionState.STREAMING });

            // Wait 100ms for DataChannel buffer to stabilize before sending first frame
            // This prevents the first keyframe chunks from being dropped during connection establishment
            // TODO: Can we make this an event instead of a fixed length timeout
            setTimeout(() => {
                if (this.dataChannel && this.dataChannel.readyState === 'open') {
                    this.startFrameStreaming();
                } else {
                    console.warn('[WebRTC] DataChannel closed during startup delay');
                }
            }, 100);
        };

        this.dataChannel.onmessage = async (event) => {
            // Receive frames from server (H.264 chunked or unchunked)
            try {
                const data = event.data;

                // Update server response time to prevent WebSocket timeout
                state.setState({ lastServerResponseTime: Date.now() });

                if (data instanceof ArrayBuffer) {
                    const byteArray = new Uint8Array(data);

                    // Check if this is a chunked message (header: [flags:1][chunkIdx:2][totalChunks:2][frameSequence:4])
                    // Server only chunks frames > 12KB, so messages without header are complete frames
                    const isPotentiallyChunked = byteArray.length > 9;

                    if (isPotentiallyChunked) {
                        // Read potential chunk header
                        const flags = byteArray[0];
                        const chunkIdx = byteArray[1] | (byteArray[2] << 8);
                        const totalChunks = byteArray[3] | (byteArray[4] << 8);
                        const frameSequence = byteArray[5] | (byteArray[6] << 8) |
                            (byteArray[7] << 16) | (byteArray[8] << 24);

                        // Validate if this looks like a chunk header (sanity checks)
                        const isFirstChunk = (flags & 0x01) !== 0;
                        const isLastChunk = (flags & 0x02) !== 0;
                        const isKeyframe = (flags & 0x04) !== 0;

                        // Chunked messages should have reasonable totalChunks (2-100)
                        // and chunkIdx should be valid
                        const looksLikeChunk = totalChunks >= 2 && totalChunks <= 100 &&
                            chunkIdx >= 0 && chunkIdx < totalChunks;

                        if (looksLikeChunk) {
                            console.log(`[WebRTC] Received chunk ${chunkIdx + 1}/${totalChunks} (seq=${frameSequence}, ${byteArray.length - 9} bytes, keyframe=${isKeyframe})`);

                            // Get or create buffer for this frame using unique frame sequence
                            const frameId = frameSequence.toString();
                            if (!this.frameChunkBuffer.has(frameId)) {
                                this.frameChunkBuffer.set(frameId, {
                                    chunks: new Array(totalChunks),
                                    receivedCount: 0,
                                    totalChunks: totalChunks,
                                    isKeyframe: isKeyframe,
                                    frameSequence: frameSequence,
                                    firstChunkTime: performance.now()
                                });
                            }

                            const frameBuffer = this.frameChunkBuffer.get(frameId);

                            // Store chunk data (skip 9-byte header)
                            frameBuffer.chunks[chunkIdx] = byteArray.slice(9);
                            frameBuffer.receivedCount++;

                            // Check if frame is complete
                            if (frameBuffer.receivedCount === totalChunks) {
                                console.log(`[WebRTC] Frame complete (seq=${frameSequence}, ${totalChunks} chunks), reassembling...`);

                                // Calculate total size
                                let totalSize = 0;
                                for (const chunk of frameBuffer.chunks) {
                                    totalSize += chunk.length;
                                }

                                // Reassemble frame
                                const completeFrame = new Uint8Array(totalSize);
                                let offset = 0;
                                for (const chunk of frameBuffer.chunks) {
                                    completeFrame.set(chunk, offset);
                                    offset += chunk.length;
                                }

                                const reassemblyTime = performance.now() - frameBuffer.firstChunkTime;
                                console.log(`[WebRTC] Reassembled frame seq=${frameSequence}: ${totalSize} bytes in ${reassemblyTime.toFixed(1)}ms`);

                                // Clean up buffer
                                this.frameChunkBuffer.delete(frameId);

                                // Decode reassembled frame
                                this.decodeH264Frame(completeFrame, frameBuffer.isKeyframe);
                            }

                            return; // Chunked message handled
                        }
                    }

                    // Not chunked - treat as complete H.264 frame
                    console.log('[WebRTC] Received complete frame:', byteArray.length, 'bytes');

                    // Detect if H.264 based on Annex B start code
                    const isH264 = (byteArray[0] === 0x00 && byteArray[1] === 0x00 &&
                        (byteArray[2] === 0x00 || byteArray[2] === 0x01));

                    if (isH264) {
                        this.decodeH264Frame(byteArray);
                    } else {
                        console.warn('[WebRTC] Unknown frame format, first bytes:',
                            Array.from(byteArray.slice(0, 8)).map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
                    }
                }
            } catch (error) {
                console.error('[WebRTC] Error processing received frame:', error);
            }
        };

        this.dataChannel.onclose = () => {
            console.log('[WebRTC] Data channel closed');
            this.stopFrameStreaming();
        };

        this.dataChannel.onerror = (error) => {
            console.error('[WebRTC] Data channel error:', error);
        };
    }

    /**
     * Decode H.264 frame (either complete or reassembled from chunks)
     * @param {Uint8Array} byteArray - H.264 frame data in Annex B format
     * @param {boolean} forceKeyframe - Force keyframe flag (from chunk header)
     */
    decodeH264Frame(byteArray, forceKeyframe = false) {
        if (!this.videoDecoder || this.videoDecoder.state !== 'configured') {
            console.warn('[WebRTC] No H.264 decoder available for received frame');
            return;
        }

        try {
            // Scan NAL units to determine if this is a keyframe
            // A frame may contain: SPS (7), PPS (8), SEI (6), IDR (5), etc.
            let isKeyframe = forceKeyframe;
            const nalTypes = [];

            for (let i = 0; i < byteArray.length - 4; i++) {
                // Look for Annex B start codes: 0x00 0x00 0x00 0x01 or 0x00 0x00 0x01
                if (byteArray[i] === 0x00 && byteArray[i + 1] === 0x00) {
                    let nalByte;
                    if (byteArray[i + 2] === 0x00 && byteArray[i + 3] === 0x01) {
                        nalByte = byteArray[i + 4];
                        i += 3;
                    } else if (byteArray[i + 2] === 0x01) {
                        nalByte = byteArray[i + 3];
                        i += 2;
                    } else {
                        continue;
                    }

                    const nalType = nalByte & 0x1F;
                    nalTypes.push(nalType);
                    if (nalType === 5) { // IDR slice
                        isKeyframe = true;
                    }
                }
            }

            console.log(`[WebRTC] Decoding H.264 (${isKeyframe ? 'keyframe' : 'delta'}): NAL units [${nalTypes.join(', ')}]`);

            const chunk = new EncodedVideoChunk({
                type: isKeyframe ? 'key' : 'delta',
                timestamp: performance.now() * 1000,
                data: byteArray
            });

            this.videoDecoder.decode(chunk);
        } catch (error) {
            console.error('[WebRTC] Error decoding H.264 frame:', error);
        }
    }

    /**
     * Send decoder config (AVCC with SPS/PPS) to server so it can initialize FFmpeg decoder
     * Format: [0x01][AVCC bytes...]
     * Server detects this message type by the 0x01 prefix and extracts AVCC for decoder extradata
     * @returns {boolean} true if config was sent successfully
     */
    sendDecoderConfigToServer() {
        if (!this.dataChannel || this.dataChannel.readyState !== 'open') {
            console.log('[WebRTC] Cannot send decoder config - DataChannel not open');
            return false;
        }

        if (!this.cachedDecoderConfig || this.cachedDecoderConfig.length < 7) {
            console.log('[WebRTC] No cached decoder config available yet');
            return false;
        }

        if (this.decoderConfigSentToServer) {
            console.log('[WebRTC] Decoder config already sent to server');
            return true;
        }

        // Build config message: [0x01 type byte][AVCC bytes]
        const configMessage = new Uint8Array(1 + this.cachedDecoderConfig.length);
        configMessage[0] = 0x01; // Message type: decoder config
        configMessage.set(this.cachedDecoderConfig, 1);

        try {
            this.dataChannel.send(configMessage.buffer);
            this.decoderConfigSentToServer = true;
            console.log(`[WebRTC]  Sent decoder config to server (${this.cachedDecoderConfig.length} AVCC bytes) - server can now decode frames`);
            return true;
        } catch (e) {
            console.error('[WebRTC] Failed to send decoder config:', e);
            return false;
        }
    }

    /**
     * Send data over DataChannel with automatic chunking for large messages
     * Chunks messages larger than 12KB to avoid buffer overflow on mobile Safari
     * @param {Uint8Array} data - Data to send
     * @param {boolean} isKeyframe - Whether this is a keyframe (for chunk metadata)
     * @returns {number} Total bytes sent
     */
    sendDataChannelMessage(data, isKeyframe = false) {
        const CHUNK_SIZE = 12 * 1024; // 12KB per chunk (mobile Safari compatible)

        // Small messages: send directly without chunking overhead
        if (data.length <= CHUNK_SIZE) {
            this.dataChannel.send(data.buffer);
            return data.length;
        }

        // Get unique frame sequence number for this frame
        if (!this.nextOutgoingFrameSequence) {
            this.nextOutgoingFrameSequence = 0;
        }
        const frameSequence = this.nextOutgoingFrameSequence++;

        // Large messages: chunk with 9-byte header [flags:1][chunkIdx:2][totalChunks:2][frameSequence:4]
        const totalChunks = Math.ceil(data.length / CHUNK_SIZE);
        console.log(`[WebRTC] Chunking large message (${data.length} bytes) into ${totalChunks} chunks (seq=${frameSequence})`);

        // Send all chunks immediately - browser manages DataChannel buffer internally
        for (let chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++) {
            const offset = chunkIdx * CHUNK_SIZE;
            const chunkSize = Math.min(CHUNK_SIZE, data.length - offset);

            // Build chunk with header
            const chunk = new Uint8Array(9 + chunkSize);

            // Flags: bit 0 = first_chunk, bit 1 = last_chunk, bit 2 = is_keyframe
            let flags = 0;
            if (chunkIdx === 0) flags |= 0x01; // First chunk
            if (chunkIdx === totalChunks - 1) flags |= 0x02; // Last chunk
            if (isKeyframe) flags |= 0x04; // Keyframe

            // Write header
            chunk[0] = flags;
            chunk[1] = chunkIdx & 0xFF;
            chunk[2] = (chunkIdx >> 8) & 0xFF;
            chunk[3] = totalChunks & 0xFF;
            chunk[4] = (totalChunks >> 8) & 0xFF;
            chunk[5] = frameSequence & 0xFF;
            chunk[6] = (frameSequence >> 8) & 0xFF;
            chunk[7] = (frameSequence >> 16) & 0xFF;
            chunk[8] = (frameSequence >> 24) & 0xFF;

            // Copy chunk data
            chunk.set(data.subarray(offset, offset + chunkSize), 9);

            // Send immediately - browser handles DataChannel buffering and flow control
            this.dataChannel.send(chunk.buffer);

            console.log(`[WebRTC] Sent chunk ${chunkIdx + 1}/${totalChunks} (seq=${frameSequence}, ${chunkSize} bytes)`);
        }

        return data.length;
    }

    /**
     * Create image from blob
     */
    createImageFromBlob(blob) {
        return new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => {
                URL.revokeObjectURL(img.src);
                resolve(img);
            };
            img.onerror = reject;
            img.src = URL.createObjectURL(blob);
        });
    }

    /**
     * Setup peer connection event handlers
     */
    setupPeerConnection() {
        // Note: We don't use video tracks - frames come via DataChannel

        // Handle ICE candidates
        this.peerConnection.onicecandidate = (event) => {
            if (event.candidate) {
                // Buffer ICE candidates until we receive the answer
                // This prevents "peer not found" errors on the server
                if (!this.receivedAnswer) {
                    console.log('[WebRTC] Buffering ICE candidate (waiting for answer):', {
                        candidate: event.candidate.candidate.substring(0, 50) + '...',
                        sdpMid: event.candidate.sdpMid,
                        bufferedCount: this.pendingIceCandidates.length + 1
                    });
                    this.pendingIceCandidates.push(event.candidate);
                    return;
                }

                console.log('[WebRTC] Sending ICE candidate immediately:', {
                    candidate: event.candidate.candidate.substring(0, 50) + '...',
                    sdpMid: event.candidate.sdpMid
                });

                const signaling = {
                    type: 'ice_candidate',
                    peerId: this.peerId,
                    candidate: event.candidate.candidate,
                    mid: event.candidate.sdpMid
                };
                websocketManager.send('WEBRTC:' + JSON.stringify(signaling));
            } else {
                console.log('[WebRTC] ICE gathering complete (null candidate received)');
            }
        };

        // Handle connection state changes
        this.peerConnection.onconnectionstatechange = () => {
            console.log('[WebRTC] Connection state changed:', {
                connectionState: this.peerConnection.connectionState,
                iceConnectionState: this.peerConnection.iceConnectionState,
                iceGatheringState: this.peerConnection.iceGatheringState,
                signalingState: this.peerConnection.signalingState
            });

            switch (this.peerConnection.connectionState) {
                case 'connecting':
                    console.log('[WebRTC] Connection establishing...');
                    break;
                case 'connected':
                    console.log('[WebRTC] Peer connection established successfully!');
                    state.setState({ connectionState: ConnectionState.CONNECTED });
                    break;
                case 'disconnected':
                    console.warn('[WebRTC] Connection temporarily disconnected');
                    state.setState({ connectionState: ConnectionState.DISCONNECTED });
                    this.stopFrameStreaming();
                    break;
                case 'failed':
                    console.error('[WebRTC] Connection failed permanently');
                    state.setState({ connectionState: ConnectionState.DISCONNECTED });
                    this.stopFrameStreaming();
                    break;
                case 'closed':
                    console.log('[WebRTC] Connection closed');
                    state.setState({ connectionState: ConnectionState.DISCONNECTED });
                    this.stopFrameStreaming();
                    break;
            }
        };
    }

    /**
     * Wait for ICE gathering to complete with timeout
     * ICE gathering can take a long time or never complete in some network configurations,
     * so we use a reasonable timeout and proceed with whatever candidates we have
     * 
     * Mobile Safari often doesn't fire 'complete' state properly, so we use shorter timeout
     * and rely on trickle ICE (candidates sent as they arrive)
     */
    waitForICEGathering() {
        return new Promise((resolve) => {
            console.log('[WebRTC] Waiting for ICE gathering, current state:', this.peerConnection.iceGatheringState);

            if (this.peerConnection.iceGatheringState === 'complete') {
                console.log('[WebRTC] ICE gathering already complete');
                resolve();
                return;
            }

            // Detect mobile Safari - it has poor ICE gathering state management
            const isMobileSafari = /iPhone|iPad|iPod/i.test(navigator.userAgent) && /Safari/i.test(navigator.userAgent);
            const timeout = isMobileSafari ? 500 : 3000; // 500ms for Safari, 3s for others
            
            console.log(`[WebRTC] Using ${timeout}ms ICE gathering timeout (mobile Safari: ${isMobileSafari})`);

            // Set timeout - proceed with whatever candidates we have
            // Safari relies on trickle ICE, so we don't need to wait for all candidates
            const iceTimeout = setTimeout(() => {
                console.warn(`[WebRTC] ICE gathering timeout after ${timeout}ms, proceeding with`, this.pendingIceCandidates.length, 'candidates');
                this.peerConnection.removeEventListener('icegatheringstatechange', checkState);
                resolve();
            }, timeout);

            const checkState = () => {
                console.log('[WebRTC] ICE gathering state changed:', this.peerConnection.iceGatheringState);
                if (this.peerConnection.iceGatheringState === 'complete') {
                    console.log('[WebRTC] ICE gathering completed naturally with', this.pendingIceCandidates.length, 'candidates');
                    clearTimeout(iceTimeout);
                    this.peerConnection.removeEventListener('icegatheringstatechange', checkState);
                    resolve();
                }
            };

            this.peerConnection.addEventListener('icegatheringstatechange', checkState);
        });
    }

    /**
     * Handle server answer
     */
    async handleAnswer(answerSdp) {
        try {
            console.log('[WebRTC] Received answer from server, setting remote description...');

            // Clear connection timeout since we received an answer
            if (this.connectionTimeout) {
                clearTimeout(this.connectionTimeout);
                this.connectionTimeout = null;
                console.log('[WebRTC] Connection timeout cleared - answer received');
            }

            await this.peerConnection.setRemoteDescription({
                type: 'answer',
                sdp: answerSdp
            });
            console.log('[WebRTC] Remote description set successfully (answer received)');

            // Mark that we've received the answer
            this.receivedAnswer = true;

            // Send any buffered ICE candidates
            if (this.pendingIceCandidates.length > 0) {
                console.log(`[WebRTC] Sending ${this.pendingIceCandidates.length} buffered ICE candidates`);
                for (const candidate of this.pendingIceCandidates) {
                    const signaling = {
                        type: 'ice_candidate',
                        peerId: this.peerId,
                        candidate: candidate.candidate,
                        mid: candidate.sdpMid
                    };
                    websocketManager.send('WEBRTC:' + JSON.stringify(signaling));
                }
                this.pendingIceCandidates = [];
                console.log('[WebRTC] All buffered ICE candidates sent');
            }
        } catch (error) {
            console.error('[WebRTC] Failed to set remote description:', error);
            throw error;
        }
    }

    /**
     * Handle ICE candidate from server
     */
    async handleIceCandidate(candidate, mid) {
        try {
            await this.peerConnection.addIceCandidate({
                candidate: candidate,
                sdpMid: mid
            });
            console.log('[WebRTC] Added ICE candidate');
        } catch (error) {
            console.error('[WebRTC] Failed to add ICE candidate:', error);
        }
    }

    /**
     * Reconfigure H.264 encoder for new resolution
     */
    async reconfigureEncoder() {
        if (!this.videoEncoder || !(this.encodingMode === 'hardware-h264' || this.encodingMode === 'software-h264')) {
            console.warn('[WebRTC] Cannot reconfigure: no valid H.264 encoder available');
            return;
        }

        try {
            const localStream = state.get('localStream');
            const videoTrack = localStream?.getVideoTracks()[0];
            if (!videoTrack) {
                console.warn('[WebRTC] No video track for encoder reconfiguration');
                return;
            }

            const settings = videoTrack.getSettings();
            const width = settings.width || 640;
            const height = settings.height || 480;

            // Clear any pending encode promises before flushing
            if (this.pendingEncodeResolve) {
                this.pendingEncodeResolve();
                this.pendingEncodeResolve = null;
            }

            // Flush pending frames
            await this.videoEncoder.flush();

            // Reset decoder config tracking - encoder will generate new SPS/PPS after reconfigure
            this.decoderConfigSent = false;
            this.cachedDecoderConfig = null;
            this.decoderConfigSentToServer = false;
            this.framesSentCount = 0;

            const isSoftware = this.encodingMode === 'software-h264';

            // Adaptive bitrate based on new resolution (match initializeH264Encoder values)
            const pixels = width * height;
            let bitrate;
            if (pixels >= 1920 * 1080) {
                bitrate = 8_000_000; // 8 Mbps for 1080p
            } else if (pixels >= 1280 * 720) {
                bitrate = 5_000_000; // 5 Mbps for 720p
            } else if (pixels >= 960 * 540) {
                bitrate = 2_500_000; // 2.5 Mbps for 540p
            } else {
                bitrate = 1_500_000; // 1.5 Mbps for 480p and below
            }

            const isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
            const framerate = isMobile ? 24 : 30;

            // Reconfigure with new resolution, maintaining hardware/software mode
            this.videoEncoder.configure({
                codec: 'avc1.42001E', // H.264 Baseline Profile Level 3.0
                width: width,
                height: height,
                bitrate: bitrate,
                framerate: framerate,
                latencyMode: 'realtime',
                hardwareAcceleration: isSoftware ? 'prefer-software' : 'prefer-hardware'
            });

            console.log(`[WebRTC] H.264 encoder reconfigured (${this.encodingMode}): ${width}x${height} @ ${(bitrate / 1000000).toFixed(1)}Mbps, ${framerate}fps`);
        } catch (error) {
            console.error('[WebRTC] Failed to reconfigure encoder:', error);
            // Show error modal instead of falling back to JPEG
            this.showCompatibilityError('Encoder Reconfiguration Failed', `Failed to reconfigure H.264 encoder: ${error.message}. This may happen when changing camera resolution. Please refresh the page to retry.`);
        }
    }

    /**
     * Start sending camera frames via data channel with adaptive frame rate
     */
    startFrameStreaming() {
        if (this.frameInterval) {
            return;
        }

        // Adaptive frame rate based on device capabilities and network conditions
        const targetFrameRate = this.getOptimalFrameRate();
        const frameIntervalMs = 1000 / targetFrameRate;

        console.log(`[WebRTC] Starting adaptive frame streaming: ${targetFrameRate}fps (${frameIntervalMs.toFixed(1)}ms interval)`);

        // Use requestAnimationFrame for smoother, more efficient frame capture
        let lastCaptureTime = 0;
        let frameCount = 0;
        let droppedFrames = 0;
        let streamStarted = false; // Track if we've sent the first frame

        const captureLoop = async (currentTime) => {
            // Skip frame if not enough time has passed (adaptive frame rate)
            if (currentTime - lastCaptureTime < frameIntervalMs) {
                this.frameInterval = requestAnimationFrame(captureLoop);
                return;
            }

            // Check if data channel is still open before processing
            if (!this.dataChannel || this.dataChannel.readyState !== 'open') {
                console.log('[WebRTC] Data channel closed, stopping frame streaming');
                return;
            }

            // Get video track and validate it's still active
            const localStream = state.get('localStream');
            const videoTrack = localStream?.getVideoTracks()[0];
            if (!videoTrack || videoTrack.readyState !== 'live') {
                console.warn('[WebRTC] Video track not available or not live');
                this.frameInterval = requestAnimationFrame(captureLoop);
                return;
            }

            const canvas = document.getElementById('captureCanvas');
            if (!canvas || canvas.width === 0 || canvas.height === 0) {
                this.frameInterval = requestAnimationFrame(captureLoop);
                return;
            }

            // Drop frame if encoding queue is backing up (prevent latency buildup)
            // Also check if we have too many pending chunks (encoder output not being sent fast enough)
            if (this.videoEncoder && (this.videoEncoder.encodeQueueSize > 1 || this.encodedChunks.length > 3)) {
                droppedFrames++;
                console.log(`[WebRTC] Dropped frame - encoder queue: ${this.videoEncoder.encodeQueueSize}, pending chunks: ${this.encodedChunks.length}`);
                this.frameInterval = requestAnimationFrame(captureLoop);
                return;
            }

            try {
                cameraManager.captureFrame();

                if (this.encodingMode === 'hardware-h264' || this.encodingMode === 'software-h264') {
                    const forceKeyframe = !streamStarted;
                    await this.sendH264Frame(canvas, forceKeyframe);
                    if (forceKeyframe) {
                        streamStarted = true;
                        console.log('[WebRTC] First frame sent as forced keyframe');
                    }
                } else {
                    console.error('[WebRTC] No valid encoding mode available');
                    return;
                }

                lastCaptureTime = currentTime;
                frameCount++;

                if ((frameCount % 300) === 0) {
                    const dropRate = droppedFrames / frameCount * 100;
                    console.log(`[WebRTC] Performance: ${frameCount} frames sent, ${droppedFrames} dropped (${dropRate.toFixed(1)}%)`);
                }

            } catch (error) {
                console.error('[WebRTC] Error in frame capture loop:', error);
            }

            // Continue the loop
            this.frameInterval = requestAnimationFrame(captureLoop);
        };

        // Start the frame capture loop
        this.frameInterval = requestAnimationFrame(captureLoop);

        state.setState({ frameInterval: this.frameInterval, isStreaming: true });

        // Update UI to show encoding mode
        this.updateEncodingModeDisplay();
    }

    /**
     * Calculate optimal frame rate based on device capabilities
     */
    getOptimalFrameRate() {
        // Base frame rate on device type and current resolution
        const isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
        const localStream = state.get('localStream');
        const videoTrack = localStream?.getVideoTracks()[0];

        if (!videoTrack) return 24; // Fallback

        const settings = videoTrack.getSettings();
        const pixels = (settings.width || 640) * (settings.height || 480);

        // Adaptive frame rate based on resolution and device
        let targetFrameRate;
        if (pixels >= 1920 * 1080) {
            // 1080p: Lower frame rate for stability
            targetFrameRate = isMobile ? 20 : 24;
        } else if (pixels >= 1280 * 720) {
            // 720p: Balanced frame rate
            targetFrameRate = isMobile ? 24 : 30;
        } else {
            // Lower resolutions: Higher frame rate possible
            targetFrameRate = isMobile ? 24 : 30;
        }

        return targetFrameRate;
    }

    /**
     * Update UI to display current encoding mode
     */
    updateEncodingModeDisplay() {
        const encodingModeElement = document.getElementById('encodingMode');
        if (encodingModeElement && this.encodingMode) {
            const modeLabels = {
                'hardware-h264': '🚀 H.264 (HW)',
                'software-h264': '⚙️ H.264 (SW)'
            };
            encodingModeElement.textContent = modeLabels[this.encodingMode] || 'No H.264 Support';
        }
    }

    /**
     * Extract SPS and PPS NAL units from AVCC decoder config and convert to Annex B format
     * AVCC decoder config structure:
     *   - byte 0: version (always 1)
     *   - byte 1: profile
     *   - byte 2: compatibility
     *   - byte 3: level
     *   - byte 4: 6 bits reserved (all 1s) + 2 bits NAL length size minus 1
     *   - byte 5: 3 bits reserved (all 1s) + 5 bits number of SPS
     *   - for each SPS: 2 bytes length + SPS data
     *   - 1 byte number of PPS
     *   - for each PPS: 2 bytes length + PPS data
     * 
     * @param {Uint8Array} avccConfig - AVCC decoder config data
     * @returns {Uint8Array|null} Annex B formatted SPS/PPS or null if parsing failed
     */
    extractSPSPPSFromAVCC(avccConfig) {
        if (!avccConfig || avccConfig.length < 7) {
            console.warn('[WebRTC] AVCC config too short:', avccConfig?.length);
            return null;
        }

        try {
            const parts = [];
            let offset = 5; // Skip version, profile, compatibility, level, and NAL length size

            // Extract number of SPS
            const numSPS = avccConfig[offset] & 0x1F;
            offset++;

            console.log(`[WebRTC] Extracting ${numSPS} SPS from AVCC config`);

            // Extract each SPS
            for (let i = 0; i < numSPS; i++) {
                if (offset + 2 > avccConfig.length) {
                    console.error('[WebRTC] AVCC config truncated at SPS length');
                    return null;
                }

                const spsLength = (avccConfig[offset] << 8) | avccConfig[offset + 1];
                offset += 2;

                if (offset + spsLength > avccConfig.length) {
                    console.error('[WebRTC] AVCC config truncated at SPS data');
                    return null;
                }

                // Add Annex B start code + SPS data
                parts.push(new Uint8Array([0x00, 0x00, 0x00, 0x01]));
                parts.push(avccConfig.slice(offset, offset + spsLength));
                offset += spsLength;

                console.log(`[WebRTC] Extracted SPS ${i}: ${spsLength} bytes`);
            }

            // Extract number of PPS
            if (offset >= avccConfig.length) {
                console.error('[WebRTC] AVCC config truncated before PPS count');
                return null;
            }

            const numPPS = avccConfig[offset];
            offset++;

            console.log(`[WebRTC] Extracting ${numPPS} PPS from AVCC config`);

            // Extract each PPS
            for (let i = 0; i < numPPS; i++) {
                if (offset + 2 > avccConfig.length) {
                    console.error('[WebRTC] AVCC config truncated at PPS length');
                    return null;
                }

                const ppsLength = (avccConfig[offset] << 8) | avccConfig[offset + 1];
                offset += 2;

                if (offset + ppsLength > avccConfig.length) {
                    console.error('[WebRTC] AVCC config truncated at PPS data');
                    return null;
                }

                // Add Annex B start code + PPS data
                parts.push(new Uint8Array([0x00, 0x00, 0x00, 0x01]));
                parts.push(avccConfig.slice(offset, offset + ppsLength));
                offset += ppsLength;

                console.log(`[WebRTC] Extracted PPS ${i}: ${ppsLength} bytes`);
            }

            // Concatenate all parts
            let totalLength = 0;
            for (const part of parts) {
                totalLength += part.length;
            }

            const result = new Uint8Array(totalLength);
            let position = 0;
            for (const part of parts) {
                result.set(part, position);
                position += part.length;
            }

            console.log(`[WebRTC] Successfully extracted SPS/PPS: ${result.length} bytes total`);
            return result;

        } catch (error) {
            console.error('[WebRTC] Error extracting SPS/PPS from AVCC config:', error);
            return null;
        }
    }

    /**
     * Extract SPS and PPS NAL units from Annex B frame and build AVCC extradata.
     * Safari HW encoder outputs Annex-B format with SPS/PPS embedded in keyframes,
     * NOT in metadata.decoderConfig.description. This function extracts them
     * and builds AVCC format that FFmpeg requires as extradata.
     * @param {Uint8Array} data - Annex B formatted H.264 data
     * @returns {Uint8Array|null} AVCC extradata or null if SPS/PPS not found
     */
    extractAvccFromAnnexB(data) {
        const nals = this.parseAnnexBNALUnits(data);
        
        let spsNal = null;
        let ppsNal = null;
        
        for (const nal of nals) {
            const nalType = nal.type;
            if (nalType === 7 && !spsNal) {
                spsNal = nal.data;
                console.log(`[WebRTC] Found SPS NAL: ${spsNal.length} bytes`);
            } else if (nalType === 8 && !ppsNal) {
                ppsNal = nal.data;
                console.log(`[WebRTC] Found PPS NAL: ${ppsNal.length} bytes`);
            }
        }
        
        if (!spsNal || !ppsNal) {
            return null;
        }
        
        // Build AVCC extradata (AVCDecoderConfigurationRecord)
        // Format: version, profile, compat, level, lengthSize, numSPS, SPS..., numPPS, PPS...
        const profile = spsNal[1];
        const compat = spsNal[2];
        const level = spsNal[3];
        
        const avccLength = 6 + 2 + spsNal.length + 1 + 2 + ppsNal.length;
        const avcc = new Uint8Array(avccLength);
        let pos = 0;
        
        avcc[pos++] = 0x01;           // configurationVersion
        avcc[pos++] = profile;        // AVCProfileIndication
        avcc[pos++] = compat;         // profile_compatibility
        avcc[pos++] = level;          // AVCLevelIndication
        avcc[pos++] = 0xFF;           // lengthSizeMinusOne (3, meaning 4-byte NAL lengths)
        avcc[pos++] = 0xE1;           // numOfSequenceParameterSets (1, with reserved bits)
        
        // SPS length (big-endian 16-bit)
        avcc[pos++] = (spsNal.length >> 8) & 0xFF;
        avcc[pos++] = spsNal.length & 0xFF;
        avcc.set(spsNal, pos);
        pos += spsNal.length;
        
        avcc[pos++] = 0x01;           // numOfPictureParameterSets
        
        // PPS length (big-endian 16-bit)
        avcc[pos++] = (ppsNal.length >> 8) & 0xFF;
        avcc[pos++] = ppsNal.length & 0xFF;
        avcc.set(ppsNal, pos);
        
        console.log(`[WebRTC] Built AVCC extradata: ${avcc.length} bytes (profile=${profile}, level=${level})`);
        return avcc;
    }
    
    /**
     * Parse Annex B frame into array of NAL units with type and raw data (excluding start code).
     * @param {Uint8Array} data - Annex B formatted H.264 data
     * @returns {Array<{type: number, data: Uint8Array}>} Array of NAL units
     */
    parseAnnexBNALUnits(data) {
        const nals = [];
        let pos = 0;
        let nalStart = -1;
        let nalHeaderPos = -1;

        const findStartCode = (startPos) => {
            for (let i = startPos; i < data.length - 2; i++) {
                if (data[i] === 0x00 && data[i + 1] === 0x00) {
                    if (data[i + 2] === 0x01) {
                        return { pos: i, length: 3 };
                    }
                    if (i + 3 < data.length && data[i + 2] === 0x00 && data[i + 3] === 0x01) {
                        return { pos: i, length: 4 };
                    }
                }
            }
            return null;
        };

        while (pos < data.length) {
            const startCode = findStartCode(pos);
            
            if (startCode === null) {
                // No more start codes - if we have a pending NAL, finish it
                if (nalStart !== -1) {
                    const nalData = data.slice(nalHeaderPos, data.length);
                    const nalType = nalData[0] & 0x1F;
                    nals.push({ type: nalType, data: nalData });
                }
                break;
            }
            
            // Found a start code
            if (nalStart !== -1) {
                // Finish previous NAL
                const nalData = data.slice(nalHeaderPos, startCode.pos);
                const nalType = nalData[0] & 0x1F;
                nals.push({ type: nalType, data: nalData });
            }
            
            // Start new NAL
            nalStart = startCode.pos;
            nalHeaderPos = startCode.pos + startCode.length;
            pos = nalHeaderPos + 1;
        }

        return nals;
    }

    /**
     * Analyze Annex B frame to extract NAL unit types
     * @param {Uint8Array} data - Annex B formatted H.264 data
     * @returns {Array<number>} Array of NAL unit types found
     */
    analyzeAnnexBNALUnits(data) {
        const nalTypes = [];
        let pos = 0;

        while (pos < data.length - 4) {
            // Look for start code (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
            let startCodeLength = 0;
            if (data[pos] === 0x00 && data[pos + 1] === 0x00 && data[pos + 2] === 0x00 && data[pos + 3] === 0x01) {
                startCodeLength = 4;
            } else if (data[pos] === 0x00 && data[pos + 1] === 0x00 && data[pos + 2] === 0x01) {
                startCodeLength = 3;
            } else {
                pos++;
                continue;
            }

            // NAL unit type is in the lower 5 bits of the byte after start code
            const nalHeaderPos = pos + startCodeLength;
            if (nalHeaderPos < data.length) {
                const nalType = data[nalHeaderPos] & 0x1F;
                nalTypes.push(nalType);
            }

            pos += startCodeLength + 1;
        }

        return nalTypes;
    }

    /**
     * Convert AVCC (length-prefixed NALs) to Annex B (start-code prefixed) when possible.
     * Tries big-endian 4-byte length parsing first, then little-endian as a fallback.
     * Returns a Uint8Array containing Annex B formatted data, or null if conversion failed
     * (or input already looks like Annex B). This is defensive because some browsers
     * output AVCC while others output Annex B.
     */
    convertAVCCToAnnexB(byteArray) {
        if (!byteArray || byteArray.length < 4) {
            console.warn('[WebRTC] convertAVCCToAnnexB: Invalid input (too short)');
            return null;
        }

        // If it already looks like Annex B, return it as-is
        if ((byteArray[0] === 0x00 && byteArray[1] === 0x00 && byteArray[2] === 0x00 && byteArray[3] === 0x01) ||
            (byteArray[0] === 0x00 && byteArray[1] === 0x00 && byteArray[2] === 0x01)) {
            return byteArray; // already Annex B
        }

        const tryParse = (isBigEndian) => {
            const parts = [];
            let offset = 0;
            let nalCount = 0;

            while (offset + 4 <= byteArray.length) {
                let len;
                if (isBigEndian) {
                    len = (byteArray[offset] << 24) | (byteArray[offset + 1] << 16) | (byteArray[offset + 2] << 8) | (byteArray[offset + 3]);
                    // >>> 0 to coerce to unsigned 32-bit
                    len = len >>> 0;
                } else {
                    len = (byteArray[offset]) | (byteArray[offset + 1] << 8) | (byteArray[offset + 2] << 16) | (byteArray[offset + 3] << 24);
                    len = len >>> 0;
                }

                // Enhanced sanity checks
                const remainingBytes = byteArray.length - offset - 4;
                if (len <= 0 || len > remainingBytes) {
                    // Invalid length - log details for debugging
                    if (nalCount === 0) {
                        // Failed on first NAL, likely wrong endianness
                        return null;
                    } else {
                        // Failed mid-stream - log warning but return what we have so far
                        console.warn(`[WebRTC] AVCC parse incomplete: len=${len}, remaining=${remainingBytes}, offset=${offset}, nalCount=${nalCount}`);
                        break;
                    }
                }

                const nalStart = offset + 4;
                parts.push(new Uint8Array([0x00, 0x00, 0x00, 0x01]));
                parts.push(byteArray.subarray(nalStart, nalStart + len));

                offset = nalStart + len;
                nalCount++;
            }

            if (parts.length > 0) {
                // Concatenate all parts even if we didn't consume all bytes
                // Some encoders may add padding
                let total = 0;
                for (const p of parts) total += p.length;
                const out = new Uint8Array(total);
                let pos = 0;
                for (const p of parts) {
                    out.set(p, pos);
                    pos += p.length;
                }

                // Log if we didn't consume all bytes (potential padding or partial frame)
                if (offset < byteArray.length) {
                    const unconsumed = byteArray.length - offset;
                    console.debug(`[WebRTC] AVCC conversion left ${unconsumed} bytes unconsumed (nalCount: ${nalCount})`);
                }

                return out;
            }
            return null;
        };

        // Try big-endian first (standard for AVCC/MP4), then little-endian
        let annex = tryParse(true);
        if (annex) return annex;

        annex = tryParse(false);
        if (annex) return annex;

        // Could not parse as AVCC - log sample for debugging
        const sample = Array.from(byteArray.slice(0, Math.min(16, byteArray.length)))
            .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' ');
        console.error(`[WebRTC] Failed to convert AVCC to Annex B. Length: ${byteArray.length}, Sample: ${sample}`);

        return null;
    }

    /**
     * Send H.264 encoded frame via DataChannel
     * Handles encoder chunk consolidation and automatic transport chunking for large frames
     * @param {HTMLCanvasElement} canvas - Canvas containing the frame to encode
     * @param {boolean} forceKeyframe - Force this frame to be a keyframe
     */
    async sendH264Frame(canvas, forceKeyframe = false) {
        try {
            const videoFrame = new VideoFrame(canvas, {
                timestamp: performance.now() * 1000,
                duration: 33333
            });

            if (!this.encodingFrameCount) {
                this.encodingFrameCount = 0;
            }
            this.encodingFrameCount++;

            const keyframeInterval = this.encodingFrameCount < 90 ? 15 : 30;

            // Force keyframes until we have decoder config (SPS/PPS).
            // Safari HW encoder may not honor first keyframe request immediately.
            const needsConfig = !this.cachedDecoderConfig && this.encodingFrameCount <= 10;
            const keyFrame = forceKeyframe || needsConfig || (this.encodingFrameCount % keyframeInterval) === 0;

            if (keyFrame) {
                console.log(`[WebRTC] Requesting keyframe (encodingFrame: ${this.encodingFrameCount}, forced: ${forceKeyframe}, needsConfig: ${needsConfig})`);

                // Hardware encoders ignore keyFrame hints without flush
                // Flush completes current GOP and forces new one starting with IDR frame
                try {
                    await this.videoEncoder.flush();
                } catch (err) {
                    console.warn('[WebRTC] Failed to flush encoder:', err);
                }
            }

            // Process accumulated encoded chunks BEFORE encoding current frame
            // IMPORTANT: WebCodecs encoder is asynchronous - chunks from previous frames
            // accumulate in this.encodedChunks. We send those FIRST, then encode the new frame.
            // This prevents frame skipping and maintains correct H.264 sequence order.
            const chunksToSend = [...this.encodedChunks];
            this.encodedChunks.length = 0;

            // Encode the frame and wait for encoder output callback to fire
            // Use Promise-based synchronization instead of arbitrary timeout
            const encodePromise = new Promise(resolve => {
                this.pendingEncodeResolve = resolve;
            });

            this.videoEncoder.encode(videoFrame, { keyFrame });
            videoFrame.close();

            // Wait for encoder output callback
            // Use longer timeout for keyframes (especially first frame) since encoder needs more time
            const timeout = keyFrame ? 500 : 100;
            const timeoutPromise = new Promise(resolve => setTimeout(resolve, timeout));
            await Promise.race([encodePromise, timeoutPromise]);

            // If no previous chunks to send but encoder just output new ones, grab them
            if (chunksToSend.length === 0 && this.encodedChunks.length > 0) {
                console.log('[WebRTC] Encoder output arrived during wait, sending immediately');
                chunksToSend.push(...this.encodedChunks);
                this.encodedChunks.length = 0;
            }

            if (chunksToSend.length === 0) {
                console.log('[WebRTC] No encoded chunks ready yet - encoding current frame, will send on next call');
                return;
            }

            // CRITICAL: Ensure decoder config (AVCC) is sent to server BEFORE any frame data
            // Server needs this to initialize FFmpeg decoder with proper SPS/PPS extradata
            if (!this.decoderConfigSentToServer) {
                // Try to send config if we have it cached
                if (this.cachedDecoderConfig) {
                    if (!this.sendDecoderConfigToServer()) {
                        console.warn('[WebRTC] Failed to send decoder config - blocking frame send');
                        return;
                    }
                } else {
                    // No config yet - first keyframe with decoderConfig hasn't arrived
                    console.log('[WebRTC] Waiting for decoder config before sending frames...');
                    return;
                }
            }

            let spsPpsData = null;
            const firstChunk = chunksToSend[0];
            const isKeyframe = firstChunk.type === 'key';

            // Check if encoder outputs Annex B directly (some software encoders do this)
            const isAnnexB = firstChunk.data[0] === 0x00 && firstChunk.data[1] === 0x00 &&
                (firstChunk.data[2] === 0x00 || firstChunk.data[2] === 0x01);

            // CRITICAL: First frame sent MUST be a keyframe with SPS/PPS
            if (this.framesSentCount === 0 && !isKeyframe) {
                console.warn('[WebRTC] First frame is not a keyframe - discarding and waiting for keyframe');
                return;
            }

            // For Annex B keyframes, analyze what NAL units are present
            if (isAnnexB && isKeyframe) {
                const nalTypes = this.analyzeAnnexBNALUnits(firstChunk.data);
                const isSafari = /Safari/i.test(navigator.userAgent) && !/Chrome|Chromium/i.test(navigator.userAgent);
                console.log(`[WebRTC] Encoder outputs Annex B keyframe with NAL types: [${nalTypes.join(', ')}] (Safari=${isSafari}, seq=${this.framesSentCount})`);

                // Check if we have both SPS (7) and PPS (8)
                const hasSPS = nalTypes.includes(7);
                const hasPPS = nalTypes.includes(8);

                if (hasSPS && hasPPS) {
                    console.log('[WebRTC]  Keyframe contains both SPS and PPS - ready to decode');
                    this.decoderConfigSent = true;
                } else {
                    console.warn(`[WebRTC] Keyframe missing parameter sets: SPS=${hasSPS}, PPS=${hasPPS}, NAL types: [${nalTypes.join(', ')}]`);
                    
                    // CRITICAL: Safari may not include SPS/PPS in first keyframe
                    // If this is the first frame and we don't have SPS/PPS, we MUST discard it
                    if (this.framesSentCount === 0) {
                        console.error('[WebRTC] FATAL: First keyframe missing SPS/PPS - discarding and requesting new keyframe');
                        // Force next frame to be keyframe with flush
                        this.encodingFrameCount = 0;
                        return;
                    }
                }
            }
            // For AVCC format, extract SPS/PPS from decoderConfig
            else if (!isAnnexB) {
                const isSafari = /Safari/i.test(navigator.userAgent) && !/Chrome|Chromium/i.test(navigator.userAgent);
                const needsDecoderConfig = isKeyframe || !this.decoderConfigSent;

                console.log(`[WebRTC] AVCC format detected (Safari=${isSafari}, keyframe=${isKeyframe}, seq=${this.framesSentCount}, hasDecoderConfig=${!!firstChunk.decoderConfig})`);

                if (needsDecoderConfig) {
                    const decoderConfigSource = (isKeyframe && firstChunk.decoderConfig)
                        ? firstChunk.decoderConfig
                        : this.cachedDecoderConfig;

                    if (decoderConfigSource) {
                        console.log(`[WebRTC] ${isKeyframe ? 'Keyframe' : 'Using cached'} decoder config, extracting SPS/PPS (${decoderConfigSource.length} bytes)...`);
                        spsPpsData = this.extractSPSPPSFromAVCC(decoderConfigSource);
                        if (spsPpsData) {
                            console.log(`[WebRTC] Extracted SPS/PPS: ${spsPpsData.length} bytes`);
                            this.decoderConfigSent = true;
                        } else {
                            console.error('[WebRTC] Failed to extract SPS/PPS from decoder config');
                        }
                    } else if (!this.decoderConfigSent) {
                        console.warn('[WebRTC] No decoder config available yet, cannot send P-frame without SPS/PPS');
                        return;
                    }
                }
            }
            // Block P-frames if no SPS/PPS sent yet (shouldn't happen with Annex B)
            else if (!this.decoderConfigSent) {
                console.warn('[WebRTC] Blocking P-frame - no SPS/PPS sent yet');
                return;
            }

            const annexBChunks = [];
            let totalSize = spsPpsData ? spsPpsData.length : 0;

            for (const chunk of chunksToSend) {
                const sampleBytes = Array.from(chunk.data.slice(0, 16))
                    .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' ');
                console.log(`[WebRTC] Encoder chunk ${annexBChunks.length + 1}/${chunksToSend.length} (${chunk.type}): ${chunk.data.byteLength} bytes, sample: ${sampleBytes}`);

                const annexB = this.convertAVCCToAnnexB(chunk.data);
                if (!annexB) {
                    console.error(`[WebRTC] Failed to convert encoder chunk to Annex B - dropping entire frame`);
                    return;
                }
                annexBChunks.push(annexB);
                totalSize += annexB.length;
            }

            const completeFrame = new Uint8Array(totalSize);
            let offset = 0;

            if (spsPpsData) {
                completeFrame.set(spsPpsData, offset);
                offset += spsPpsData.length;
                console.log(`[WebRTC] Prepended SPS/PPS (${spsPpsData.length} bytes) to frame`);
            }

            for (const annexB of annexBChunks) {
                completeFrame.set(annexB, offset);
                offset += annexB.length;
            }

            console.log(`[WebRTC] Combined ${chunksToSend.length} encoder chunks into complete frame: ${completeFrame.length} bytes (${isKeyframe ? 'keyframe' : 'delta'})`);

            if (!(this.dataChannel && this.dataChannel.readyState === 'open')) {
                console.warn('[WebRTC] DataChannel not ready - dropping frame');
                return;
            }

            try {
                const bytesSent = this.sendDataChannelMessage(completeFrame, isKeyframe);

                this.framesSentCount++; // Track frames actually sent

                const framesSent = state.get('framesSent') + 1;
                const dataSent = state.get('dataSent') + (bytesSent / 1024);
                state.setState({ framesSent, dataSent });

                if (isKeyframe) {
                    console.log(`[WebRTC] KEYFRAME #${framesSent} sent: ${bytesSent} bytes (encoding frame ${this.encodingFrameCount}), combined ${chunksToSend.length} encoder chunks`);
                } else {
                    console.log(`[WebRTC] Sent frame #${framesSent} (delta): ${bytesSent} bytes (encoding frame ${this.encodingFrameCount}), combined ${chunksToSend.length} encoder chunks`);
                }
            } catch (err) {
                console.error('[WebRTC] Error sending complete H.264 frame:', err);
            }

        } catch (error) {
            console.error(`[WebRTC] H.264 encoding error (${this.encodingMode}):`, error);
            this.showCompatibilityError('H.264 Encoding Failed', `Failed to encode H.264 frame: ${error.message}. The encoder may have crashed or become unavailable. Please refresh the page to retry.`);
        }
    }

    /**
     * Stop sending camera frames
     */
    stopFrameStreaming() {
        if (this.frameInterval) {
            cancelAnimationFrame(this.frameInterval);
            this.frameInterval = null;
            state.setState({ frameInterval: null, isStreaming: false });

            this.encodingFrameCount = 0;
            this.framesSentCount = 0;

            console.log('[WebRTC] Stopped frame streaming');
        }
    }

    /**
     * Close WebRTC connection
     */
    close() {
        console.log('[WebRTC] Closing...');

        this.stopFrameStreaming();

        // Clear connection timeout
        if (this.connectionTimeout) {
            clearTimeout(this.connectionTimeout);
            this.connectionTimeout = null;
        }

        // Clear any pending encode promises
        if (this.pendingEncodeResolve) {
            this.pendingEncodeResolve();
            this.pendingEncodeResolve = null;
        }

        if (this.videoEncoder) {
            try {
                this.videoEncoder.close();
            } catch (error) {
                console.error('[WebRTC] Error closing video encoder:', error);
            }
            this.videoEncoder = null;
        }

        if (this.videoDecoder) {
            try {
                this.videoDecoder.close();
            } catch (error) {
                console.error('[WebRTC] Error closing video decoder:', error);
            }
            this.videoDecoder = null;
        }

        this.encodedChunks = [];
        this.pendingIceCandidates = [];
        this.receivedAnswer = false;

        // Reset decoder config tracking
        this.decoderConfigSent = false;
        this.cachedDecoderConfig = null;
        this.decoderConfigSentToServer = false;
        this.framesSentCount = 0;

        if (this.dataChannel) {
            this.dataChannel.close();
            this.dataChannel = null;
        }

        if (this.peerConnection) {
            this.peerConnection.close();
            this.peerConnection = null;
        }

        console.log('[WebRTC] Closed');
    }
}

export const webrtcManager = new WebRTCManager();
