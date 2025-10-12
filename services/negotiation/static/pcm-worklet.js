/**
 * PCMPlayer AudioWorklet Processor
 * 
 * Handles low-latency PCM16LE audio playback at 24kHz.
 * Implements a ring buffer for smooth playback and backpressure management.
 * 
 * Messages:
 *  - { kind: 'push', pcm16: ArrayBuffer } - Enqueue audio samples
 *  - { kind: 'reset' } - Clear buffer and reset state
 * 
 * Posts back:
 *  - { kind: 'playhead', samples: number } - Playhead position every ~20ms
 *  - { kind: 'underrun' } - Buffer underrun detected
 *  - { kind: 'buffer_depth', ms: number } - Current buffer depth in milliseconds
 */

class PCMPlayer extends AudioWorkletProcessor {
    constructor() {
        super();
        
        // Ring buffer configuration
        // At 24kHz, 5 seconds = 120,000 samples
        this.bufferSize = 120000;
        this.buffer = new Float32Array(this.bufferSize);
        this.writePos = 0;
        this.readPos = 0;
        this.samplesPlayed = 0;
        
        // Target buffer depth: 40-60ms (960-1440 samples at 24kHz)
        this.targetBufferMs = 50;
        this.targetBufferSamples = Math.floor((this.targetBufferMs / 1000) * sampleRate);
        
        // Statistics
        this.underrunCount = 0;
        this.lastPlayheadReport = 0;
        this.playheadReportInterval = Math.floor(sampleRate * 0.02); // ~20ms
        
        // Listen for messages from main thread
        this.port.onmessage = (event) => {
            this.handleMessage(event.data);
        };
        
        // Initial status
        this.port.postMessage({
            kind: 'status',
            message: 'PCMPlayer initialized',
            sampleRate: sampleRate,
            targetBufferMs: this.targetBufferMs
        });
    }
    
    /**
     * Handle incoming messages from main thread
     */
    handleMessage(message) {
        switch (message.kind) {
            case 'push':
                this.enqueuePCM16(message.pcm16);
                break;
                
            case 'reset':
                this.reset();
                break;
                
            default:
                console.warn('PCMPlayer: Unknown message kind', message.kind);
        }
    }
    
    /**
     * Enqueue PCM16LE samples (Int16Array)
     */
    enqueuePCM16(arrayBuffer) {
        // Convert ArrayBuffer to Int16Array
        const int16Samples = new Int16Array(arrayBuffer);
        const sampleCount = int16Samples.length;
        
        // Convert Int16 to Float32 [-1.0, 1.0]
        for (let i = 0; i < sampleCount; i++) {
            // Normalize Int16 (-32768 to 32767) to Float32 (-1.0 to 1.0)
            this.buffer[this.writePos] = int16Samples[i] / 32768.0;
            
            // Advance write position (circular)
            this.writePos = (this.writePos + 1) % this.bufferSize;
            
            // Handle buffer overflow (shouldn't happen with proper backpressure)
            if (this.writePos === this.readPos) {
                console.warn('PCMPlayer: Buffer overflow, dropping oldest sample');
                this.readPos = (this.readPos + 1) % this.bufferSize;
            }
        }
        
        // Report current buffer depth
        const bufferedSamples = this.getBufferedSamples();
        const bufferDepthMs = (bufferedSamples / sampleRate) * 1000;
        
        this.port.postMessage({
            kind: 'buffer_depth',
            ms: bufferDepthMs,
            samples: bufferedSamples
        });
    }
    
    /**
     * Get number of samples currently buffered
     */
    getBufferedSamples() {
        if (this.writePos >= this.readPos) {
            return this.writePos - this.readPos;
        } else {
            return this.bufferSize - this.readPos + this.writePos;
        }
    }
    
    /**
     * Reset buffer and state
     */
    reset() {
        this.writePos = 0;
        this.readPos = 0;
        this.samplesPlayed = 0;
        this.underrunCount = 0;
        
        // Clear buffer
        this.buffer.fill(0);
        
        this.port.postMessage({
            kind: 'status',
            message: 'Buffer reset'
        });
    }
    
    /**
     * Process audio frames (called by AudioWorklet system at 24kHz)
     * 
     * @param {Float32Array[][]} inputs - Input channels (unused)
     * @param {Float32Array[][]} outputs - Output channels to fill
     * @param {Object} parameters - Parameters (unused)
     * @returns {boolean} - True to keep processor alive
     */
    process(inputs, outputs, parameters) {
        const output = outputs[0];
        
        // Handle mono output (output[0] is channel 0)
        if (output.length === 0) {
            return true;
        }
        
        const channelData = output[0]; // Mono
        const frameCount = channelData.length; // Typically 128 frames
        
        const bufferedSamples = this.getBufferedSamples();
        
        // Check for underrun
        if (bufferedSamples < frameCount) {
            // Not enough samples, output silence
            channelData.fill(0);
            
            // Report underrun (but throttle to avoid spam)
            if (bufferedSamples === 0 && this.underrunCount % 10 === 0) {
                this.port.postMessage({
                    kind: 'underrun',
                    samplesPlayed: this.samplesPlayed
                });
            }
            this.underrunCount++;
            
            return true;
        }
        
        // Fill output buffer with samples from ring buffer
        for (let i = 0; i < frameCount; i++) {
            channelData[i] = this.buffer[this.readPos];
            this.readPos = (this.readPos + 1) % this.bufferSize;
        }
        
        // Duplicate to stereo if needed
        if (output.length > 1) {
            output[1].set(channelData);
        }
        
        // Update statistics
        this.samplesPlayed += frameCount;
        
        // Report playhead position periodically (~20ms)
        if (this.samplesPlayed - this.lastPlayheadReport >= this.playheadReportInterval) {
            this.port.postMessage({
                kind: 'playhead',
                samples: this.samplesPlayed,
                timeSeconds: this.samplesPlayed / sampleRate
            });
            this.lastPlayheadReport = this.samplesPlayed;
        }
        
        return true; // Keep processor alive
    }
}

// Register processor
registerProcessor('pcm-player', PCMPlayer);

