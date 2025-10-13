/**
 * Audio2Face WebSocket Client
 * 
 * Connects to FastAPI /a2f/stream WebSocket endpoint and streams
 * audio input, receiving blendshape frames for 3D head animation.
 * 
 * Usage:
 *   const a2f = new Audio2FaceClient();
 *   await a2f.connect();
 *   
 *   a2f.on('frame', (frame) => {
 *       head3d.applyBlendshapes(frame.weights);
 *   });
 *   
 *   await a2f.streamAudio(audioBuffer);
 */

export class Audio2FaceClient {
    constructor(options = {}) {
        this.options = {
            wsUrl: options.wsUrl || `ws://${window.location.host}/a2f/stream`,
            useStub: options.useStub !== false, // Default to stub mode
            reconnectDelay: options.reconnectDelay || 3000,
            ...options
        };
        
        this.ws = null;
        this.isConnected = false;
        this.eventHandlers = {};
        this.frameCount = 0;
        this.audioQueue = [];
        this.isStreaming = false;
    }
    
    /**
     * Connect to Audio2Face WebSocket
     */
    async connect() {
        return new Promise((resolve, reject) => {
            const url = `${this.options.wsUrl}?use_stub=${this.options.useStub}`;
            console.log(`A2F: Connecting to ${url}`);
            
            this.ws = new WebSocket(url);
            this.ws.binaryType = 'arraybuffer';
            
            this.ws.onopen = () => {
                console.log('A2F: WebSocket connected');
            };
            
            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    
                    if (data.ready) {
                        console.log('A2F: Service ready', data);
                        this.isConnected = true;
                        resolve();
                    } else if (data.weights) {
                        // Blendshape frame received
                        this._handleFrame(data);
                    } else if (data.done) {
                        console.log('A2F: Streaming completed');
                        this._emit('done');
                        this.isStreaming = false;
                    } else if (data.error) {
                        console.error('A2F: Server error', data);
                        this._emit('error', new Error(data.error));
                    }
                } catch (err) {
                    console.error('A2F: Failed to parse message', err);
                }
            };
            
            this.ws.onerror = (error) => {
                console.error('A2F: WebSocket error', error);
                this._emit('error', error);
                reject(error);
            };
            
            this.ws.onclose = (event) => {
                console.log('A2F: WebSocket closed', event.code, event.reason);
                this.isConnected = false;
                this._emit('close');
                
                // Auto-reconnect if unexpected closure
                if (!event.wasClean && this.options.reconnectDelay > 0) {
                    console.log(`A2F: Reconnecting in ${this.options.reconnectDelay}ms`);
                    setTimeout(() => this.connect(), this.options.reconnectDelay);
                }
            };
            
            // Timeout if no ready message received
            setTimeout(() => {
                if (!this.isConnected) {
                    reject(new Error('Connection timeout'));
                }
            }, 5000);
        });
    }
    
    /**
     * Disconnect from WebSocket
     */
    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.isConnected = false;
    }
    
    /**
     * Stream audio to Audio2Face service
     * @param {ArrayBuffer|Float32Array|Int16Array} audio - Audio data
     */
    async streamAudio(audio) {
        if (!this.isConnected) {
            throw new Error('Not connected. Call connect() first.');
        }
        
        // Convert audio to PCM16 if needed
        const pcm16 = this._convertToPCM16(audio);
        
        // Send audio chunks
        const chunkSize = 4096; // 4KB chunks
        this.isStreaming = true;
        
        for (let i = 0; i < pcm16.byteLength; i += chunkSize) {
            const chunk = pcm16.slice(i, Math.min(i + chunkSize, pcm16.byteLength));
            
            if (this.ws.readyState === WebSocket.OPEN) {
                this.ws.send(chunk);
            } else {
                console.warn('A2F: WebSocket not open, stopping stream');
                break;
            }
            
            // Small delay to avoid overwhelming the server
            await this._sleep(10);
        }
        
        // Send empty chunk to signal end
        this.ws.send(new ArrayBuffer(0));
        
        console.log('A2F: Audio streaming complete');
    }
    
    /**
     * Stream from microphone
     * @param {number} durationMs - Duration to record in milliseconds
     */
    async streamFromMicrophone(durationMs = 5000) {
        try {
            const stream = await navigator.mediaDevices.getUserMedia({ 
                audio: {
                    sampleRate: 16000,
                    channelCount: 1
                } 
            });
            
            const audioContext = new AudioContext({ sampleRate: 16000 });
            const source = audioContext.createMediaStreamSource(stream);
            const processor = audioContext.createScriptProcessor(4096, 1, 1);
            
            // Buffer for collecting audio
            const audioBuffer = [];
            
            processor.onaudioprocess = (e) => {
                const inputData = e.inputBuffer.getChannelData(0);
                audioBuffer.push(new Float32Array(inputData));
            };
            
            source.connect(processor);
            processor.connect(audioContext.destination);
            
            // Record for specified duration
            await this._sleep(durationMs);
            
            // Stop recording
            stream.getTracks().forEach(track => track.stop());
            processor.disconnect();
            source.disconnect();
            
            // Concatenate audio chunks
            const totalLength = audioBuffer.reduce((sum, arr) => sum + arr.length, 0);
            const combined = new Float32Array(totalLength);
            let offset = 0;
            for (const chunk of audioBuffer) {
                combined.set(chunk, offset);
                offset += chunk.length;
            }
            
            // Stream to A2F
            await this.streamAudio(combined);
            
        } catch (err) {
            console.error('A2F: Microphone access error', err);
            throw err;
        }
    }
    
    /**
     * Handle incoming blendshape frame
     */
    _handleFrame(frame) {
        this.frameCount++;
        this._emit('frame', frame);
        
        // Emit frame rate stats periodically
        if (this.frameCount % 30 === 0) {
            this._emit('stats', {
                frameCount: this.frameCount,
                frameIndex: frame.frame_index,
                timestamp: frame.timestamp_ms
            });
        }
    }
    
    /**
     * Convert audio to PCM16 format
     */
    _convertToPCM16(audio) {
        if (audio instanceof ArrayBuffer) {
            return audio;
        }
        
        if (audio instanceof Int16Array) {
            return audio.buffer;
        }
        
        if (audio instanceof Float32Array) {
            // Convert Float32 (-1.0 to 1.0) to Int16
            const pcm16 = new Int16Array(audio.length);
            for (let i = 0; i < audio.length; i++) {
                const s = Math.max(-1, Math.min(1, audio[i]));
                pcm16[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
            }
            return pcm16.buffer;
        }
        
        throw new Error('Unsupported audio format');
    }
    
    /**
     * Event emitter
     */
    on(event, handler) {
        if (!this.eventHandlers[event]) {
            this.eventHandlers[event] = [];
        }
        this.eventHandlers[event].push(handler);
    }
    
    off(event, handler) {
        if (this.eventHandlers[event]) {
            this.eventHandlers[event] = this.eventHandlers[event]
                .filter(h => h !== handler);
        }
    }
    
    _emit(event, ...args) {
        if (this.eventHandlers[event]) {
            this.eventHandlers[event].forEach(handler => {
                try {
                    handler(...args);
                } catch (err) {
                    console.error(`A2F: Error in ${event} handler`, err);
                }
            });
        }
    }
    
    /**
     * Utility: sleep
     */
    _sleep(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }
    
    /**
     * Get connection status
     */
    get connected() {
        return this.isConnected && this.ws && this.ws.readyState === WebSocket.OPEN;
    }
    
    /**
     * Get streaming status
     */
    get streaming() {
        return this.isStreaming;
    }
}

/**
 * Utility: Load audio file and convert to PCM16
 */
export async function loadAudioFile(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        
        reader.onload = async (e) => {
            try {
                const audioContext = new AudioContext({ sampleRate: 16000 });
                const audioBuffer = await audioContext.decodeAudioData(e.target.result);
                
                // Convert to mono PCM16
                const audioData = audioBuffer.getChannelData(0);
                resolve(audioData);
            } catch (err) {
                reject(err);
            }
        };
        
        reader.onerror = reject;
        reader.readAsArrayBuffer(file);
    });
}

