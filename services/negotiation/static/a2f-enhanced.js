/**
 * Enhanced Audio2Face Client with Production Features
 * 
 * Features:
 * - Backpressure management (drop old blendshapes, never audio)
 * - Frame interpolation (30-50ms queue for smooth motion)
 * - Latency monitoring (TTFB, steady-state, A/V skew)
 * - Emotion/style controls (smile bias, cheek raise, etc.)
 * - Production-ready HUD metrics
 * 
 * Aligns with NVIDIA's Audio2Face Microservice architecture.
 */

import { ARKIT52 } from '/static/a2f.js';

// ============================================================================
// Configuration
// ============================================================================

const CONFIG = {
    // Backpressure
    maxQueueSize: 50,  // Max blendshape frames queued (drop oldest if exceeded)
    dropOldFrames: true,  // Drop old frames, never audio
    
    // Interpolation
    interpolationEnabled: true,
    interpolationWindowMs: 40,  // 30-50ms recommended for smooth motion
    
    // Latency monitoring
    trackLatency: true,
    latencyUpdateIntervalMs: 100,  // Update HUD every 100ms
    targetAVSkewMs: 100,  // Target A/V sync offset (80-120ms typical)
    
    // Emotion controls
    emotionBlendDuration: 0.5,  // Seconds to blend between emotions
};

// ============================================================================
// Blendshape Frame Queue with Backpressure
// ============================================================================

class BlendshapeQueue {
    constructor(maxSize = 50) {
        this.queue = [];
        this.maxSize = maxSize;
        this.droppedCount = 0;
        this.totalEnqueued = 0;
    }
    
    enqueue(frame) {
        this.queue.push(frame);
        this.totalEnqueued++;
        
        // Apply backpressure: drop oldest frames if queue is full
        while (this.queue.length > this.maxSize) {
            const dropped = this.queue.shift();
            this.droppedCount++;
            
            if (this.droppedCount % 10 === 0) {
                console.warn(`Dropped ${this.droppedCount} blendshape frames (backpressure active)`);
            }
        }
    }
    
    /**
     * Get frame at specific audio playhead position with interpolation
     */
    getFrameAt(playheadMs, interpolate = true) {
        if (this.queue.length === 0) return null;
        
        // Find frames surrounding playhead
        const { prev, next } = this.findSurrounding(playheadMs);
        
        if (!prev && !next) return null;
        if (!prev) return next;
        if (!next) return prev;
        
        // Interpolate if enabled
        if (interpolate) {
            return this.interpolate(prev, next, playheadMs);
        } else {
            // Return closest frame
            const prevDist = Math.abs(prev.timestamp_ms - playheadMs);
            const nextDist = Math.abs(next.timestamp_ms - playheadMs);
            return prevDist < nextDist ? prev : next;
        }
    }
    
    findSurrounding(playheadMs) {
        let prev = null;
        let next = null;
        
        for (const frame of this.queue) {
            if (frame.timestamp_ms <= playheadMs) {
                if (!prev || frame.timestamp_ms > prev.timestamp_ms) {
                    prev = frame;
                }
            } else {
                if (!next || frame.timestamp_ms < next.timestamp_ms) {
                    next = frame;
                }
            }
        }
        
        return { prev, next };
    }
    
    interpolate(prevFrame, nextFrame, playheadMs) {
        const timeSpan = nextFrame.timestamp_ms - prevFrame.timestamp_ms;
        if (timeSpan <= 0) return prevFrame;
        
        const t = Math.max(0, Math.min(1, (playheadMs - prevFrame.timestamp_ms) / timeSpan));
        
        // Lerp weights
        const weights = prevFrame.weights.map((prevW, i) => {
            const nextW = nextFrame.weights[i];
            return Math.max(0, Math.min(1, prevW + t * (nextW - prevW)));
        });
        
        return {
            timestamp_ms: playheadMs,
            weights: weights,
            frame_index: prevFrame.frame_index,
            interpolated: true
        };
    }
    
    /**
     * Clean up frames older than playhead - lookback
     */
    cleanupOld(playheadMs, lookbackMs = 200) {
        const cutoff = playheadMs - lookbackMs;
        const initialSize = this.queue.length;
        
        this.queue = this.queue.filter(f => f.timestamp_ms >= cutoff);
        
        const removed = initialSize - this.queue.length;
        if (removed > 0) {
            console.debug(`Cleaned up ${removed} old frames (before ${cutoff.toFixed(1)}ms)`);
        }
        
        return removed;
    }
    
    getStats() {
        return {
            queueSize: this.queue.length,
            maxSize: this.maxSize,
            totalEnqueued: this.totalEnqueued,
            droppedCount: this.droppedCount,
            dropRate: (this.droppedCount / Math.max(1, this.totalEnqueued) * 100).toFixed(1) + '%'
        };
    }
    
    reset() {
        this.queue = [];
        this.droppedCount = 0;
        this.totalEnqueued = 0;
    }
}

// ============================================================================
// Latency Tracker
// ============================================================================

class LatencyTracker {
    constructor() {
        this.sessionStartTime = null;
        this.firstFrameTime = null;
        this.lastFrameTime = null;
        this.frameTimes = [];
        
        this.ttfb = null;  // Time To First Blendshape
        this.avgFrameTime = null;
        this.audioVideoSkew = null;
        this.totalFrames = 0;
        this.droppedFrames = 0;
    }
    
    startSession() {
        this.sessionStartTime = performance.now();
        this.firstFrameTime = null;
        this.frameTimes = [];
        this.totalFrames = 0;
        console.log('[Latency] Session started');
    }
    
    recordFrame(timestampMs) {
        const now = performance.now();
        
        // TTFB: Time to first frame
        if (this.firstFrameTime === null) {
            this.firstFrameTime = now;
            this.ttfb = now - this.sessionStartTime;
            console.log(`[Latency] TTFB: ${this.ttfb.toFixed(1)}ms`);
        }
        
        // Frame delta for average calculation
        if (this.lastFrameTime !== null) {
            const delta = now - this.lastFrameTime;
            this.frameTimes.push(delta);
            
            // Keep last 100 samples
            if (this.frameTimes.length > 100) {
                this.frameTimes.shift();
            }
            
            this.avgFrameTime = this.frameTimes.reduce((a, b) => a + b, 0) / this.frameTimes.length;
        }
        
        this.lastFrameTime = now;
        this.totalFrames++;
    }
    
    recordDrop() {
        this.droppedFrames++;
    }
    
    getMetrics() {
        const effectiveFps = this.avgFrameTime ? 1000 / this.avgFrameTime : 0;
        const dropRate = (this.droppedFrames / Math.max(1, this.totalFrames) * 100);
        
        return {
            ttfb: this.ttfb,
            avgFrameTime: this.avgFrameTime,
            effectiveFps: effectiveFps,
            audioVideoSkew: this.audioVideoSkew,
            totalFrames: this.totalFrames,
            droppedFrames: this.droppedFrames,
            dropRate: dropRate,
            sessionDuration: this.sessionStartTime ? performance.now() - this.sessionStartTime : 0
        };
    }
}

// ============================================================================
// Emotion Controller
// ============================================================================

const EMOTION_PRESETS = {
    neutral: { smile: 0, frown: 0, surprise: 0, seriousness: 0, warmth: 0 },
    friendly: { smile: 0.3, frown: 0, surprise: 0, seriousness: 0, warmth: 0.4 },
    diplomatic: { smile: 0.15, frown: 0, surprise: 0, seriousness: 0.1, warmth: 0.3 },
    stern: { smile: 0, frown: 0.2, surprise: 0, seriousness: 0.6, warmth: 0 },
    concerned: { smile: 0, frown: 0.1, surprise: 0.3, seriousness: 0.3, warmth: 0 },
};

const ARKIT_INDICES = {
    smileLeft: ARKIT52.indexOf('mouthSmileLeft'),
    smileRight: ARKIT52.indexOf('mouthSmileRight'),
    frownLeft: ARKIT52.indexOf('mouthFrownLeft'),
    frownRight: ARKIT52.indexOf('mouthFrownRight'),
    cheekSquintLeft: ARKIT52.indexOf('cheekSquintLeft'),
    cheekSquintRight: ARKIT52.indexOf('cheekSquintRight'),
    browInnerUp: ARKIT52.indexOf('browInnerUp'),
    browOuterUpLeft: ARKIT52.indexOf('browOuterUpLeft'),
    browOuterUpRight: ARKIT52.indexOf('browOuterUpRight'),
    browDownLeft: ARKIT52.indexOf('browDownLeft'),
    browDownRight: ARKIT52.indexOf('browDownRight'),
    jawOpen: ARKIT52.indexOf('jawOpen'),
};

class EmotionController {
    constructor() {
        this.currentBias = { ...EMOTION_PRESETS.neutral };
        this.targetBias = { ...EMOTION_PRESETS.neutral };
        this.transitionProgress = 1.0;
        this.transitionDuration = CONFIG.emotionBlendDuration;
        this.lastUpdate = performance.now();
    }
    
    setEmotion(emotionName, transitionDuration = null) {
        const preset = EMOTION_PRESETS[emotionName];
        if (!preset) {
            console.warn(`Unknown emotion preset: ${emotionName}`);
            return;
        }
        
        this.targetBias = { ...preset };
        this.transitionDuration = transitionDuration !== null ? transitionDuration : CONFIG.emotionBlendDuration;
        this.transitionProgress = this.transitionDuration > 0 ? 0 : 1;
        this.lastUpdate = performance.now();
        
        console.log(`[Emotion] Transitioning to '${emotionName}' over ${this.transitionDuration}s`);
    }
    
    update() {
        if (this.transitionProgress >= 1.0) {
            return this.currentBias;
        }
        
        const now = performance.now();
        const dt = (now - this.lastUpdate) / 1000;  // seconds
        this.lastUpdate = now;
        
        this.transitionProgress += dt / this.transitionDuration;
        this.transitionProgress = Math.min(1.0, this.transitionProgress);
        
        // Lerp current → target
        const t = this.transitionProgress;
        for (const key in this.currentBias) {
            this.currentBias[key] = this.currentBias[key] + t * (this.targetBias[key] - this.currentBias[key]);
        }
        
        return this.currentBias;
    }
    
    applyBias(weights) {
        const bias = this.update();
        const modified = [...weights];
        
        // Apply smile
        if (bias.smile > 0) {
            modified[ARKIT_INDICES.smileLeft] = Math.min(1, modified[ARKIT_INDICES.smileLeft] + bias.smile * 0.4);
            modified[ARKIT_INDICES.smileRight] = Math.min(1, modified[ARKIT_INDICES.smileRight] + bias.smile * 0.4);
            modified[ARKIT_INDICES.cheekSquintLeft] = Math.min(1, modified[ARKIT_INDICES.cheekSquintLeft] + bias.smile * 0.2);
            modified[ARKIT_INDICES.cheekSquintRight] = Math.min(1, modified[ARKIT_INDICES.cheekSquintRight] + bias.smile * 0.2);
        }
        
        // Apply frown
        if (bias.frown > 0) {
            modified[ARKIT_INDICES.frownLeft] = Math.min(1, modified[ARKIT_INDICES.frownLeft] + bias.frown * 0.3);
            modified[ARKIT_INDICES.frownRight] = Math.min(1, modified[ARKIT_INDICES.frownRight] + bias.frown * 0.3);
            modified[ARKIT_INDICES.smileLeft] = Math.max(0, modified[ARKIT_INDICES.smileLeft] * (1 - bias.frown * 0.5));
            modified[ARKIT_INDICES.smileRight] = Math.max(0, modified[ARKIT_INDICES.smileRight] * (1 - bias.frown * 0.5));
        }
        
        // Apply surprise (raise brows)
        if (bias.surprise > 0) {
            modified[ARKIT_INDICES.browInnerUp] = Math.min(1, modified[ARKIT_INDICES.browInnerUp] + bias.surprise * 0.5);
            modified[ARKIT_INDICES.browOuterUpLeft] = Math.min(1, modified[ARKIT_INDICES.browOuterUpLeft] + bias.surprise * 0.4);
            modified[ARKIT_INDICES.browOuterUpRight] = Math.min(1, modified[ARKIT_INDICES.browOuterUpRight] + bias.surprise * 0.4);
        }
        
        // Apply seriousness
        if (bias.seriousness > 0) {
            modified[ARKIT_INDICES.smileLeft] = Math.max(0, modified[ARKIT_INDICES.smileLeft] * (1 - bias.seriousness * 0.7));
            modified[ARKIT_INDICES.smileRight] = Math.max(0, modified[ARKIT_INDICES.smileRight] * (1 - bias.seriousness * 0.7));
            modified[ARKIT_INDICES.browDownLeft] = Math.min(1, modified[ARKIT_INDICES.browDownLeft] + bias.seriousness * 0.15);
            modified[ARKIT_INDICES.browDownRight] = Math.min(1, modified[ARKIT_INDICES.browDownRight] + bias.seriousness * 0.15);
        }
        
        // Apply warmth
        if (bias.warmth > 0) {
            modified[ARKIT_INDICES.smileLeft] = Math.min(1, modified[ARKIT_INDICES.smileLeft] + bias.warmth * 0.25);
            modified[ARKIT_INDICES.smileRight] = Math.min(1, modified[ARKIT_INDICES.smileRight] + bias.warmth * 0.25);
            modified[ARKIT_INDICES.cheekSquintLeft] = Math.min(1, modified[ARKIT_INDICES.cheekSquintLeft] + bias.warmth * 0.15);
            modified[ARKIT_INDICES.cheekSquintRight] = Math.min(1, modified[ARKIT_INDICES.cheekSquintRight] + bias.warmth * 0.15);
        }
        
        return modified;
    }
}

// ============================================================================
// Enhanced A2F Client
// ============================================================================

export class EnhancedA2FClient {
    constructor(options = {}) {
        this.options = {
            wsUrl: options.wsUrl || `ws://${window.location.host}/a2f/stream`,
            maxQueueSize: options.maxQueueSize || CONFIG.maxQueueSize,
            interpolationEnabled: options.interpolationEnabled !== false,
            interpolationWindowMs: options.interpolationWindowMs || CONFIG.interpolationWindowMs,
            enableLatencyTracking: options.enableLatencyTracking !== false,
            enableEmotions: options.enableEmotions !== false,
            ...options
        };
        
        this.ws = null;
        this.isConnected = false;
        
        // Components
        this.blendshapeQueue = new BlendshapeQueue(this.options.maxQueueSize);
        this.latencyTracker = new LatencyTracker();
        this.emotionController = new EmotionController();
        
        // State
        this.currentPlayheadMs = 0;
        this.audioStartTime = null;
        
        // Callbacks
        this.onFrame = null;
        this.onMetrics = null;
    }
    
    async connect() {
        return new Promise((resolve, reject) => {
            const url = this.options.wsUrl;
            console.log(`[A2F] Connecting to ${url}`);
            
            this.ws = new WebSocket(url);
            this.ws.binaryType = 'arraybuffer';
            
            this.ws.onopen = () => {
                console.log('[A2F] Connected');
            };
            
            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    
                    if (data.ready) {
                        console.log('[A2F] Service ready', data);
                        this.isConnected = true;
                        this.latencyTracker.startSession();
                        resolve();
                    } else if (data.weights) {
                        this.handleBlendshapeFrame(data);
                    } else if (data.done) {
                        console.log('[A2F] Stream completed');
                    }
                } catch (err) {
                    console.error('[A2F] Parse error', err);
                }
            };
            
            this.ws.onerror = (err) => {
                console.error('[A2F] WebSocket error', err);
                reject(err);
            };
            
            this.ws.onclose = () => {
                console.log('[A2F] Disconnected');
                this.isConnected = false;
            };
            
            setTimeout(() => {
                if (!this.isConnected) reject(new Error('Connection timeout'));
            }, 5000);
        });
    }
    
    handleBlendshapeFrame(frame) {
        // Track latency
        if (this.options.enableLatencyTracking) {
            this.latencyTracker.recordFrame(frame.timestamp_ms);
        }
        
        // Enqueue frame (backpressure handled automatically)
        this.blendshapeQueue.enqueue(frame);
    }
    
    /**
     * Get blendshape weights for current audio playhead position
     * Call this in your render loop
     */
    getWeightsForPlayhead(playheadMs) {
        this.currentPlayheadMs = playheadMs;
        
        // Get frame from queue (interpolated)
        const frame = this.blendshapeQueue.getFrameAt(
            playheadMs,
            this.options.interpolationEnabled
        );
        
        if (!frame) return null;
        
        // Apply emotion bias if enabled
        let weights = frame.weights;
        if (this.options.enableEmotions) {
            weights = this.emotionController.applyBias(weights);
        }
        
        // Trigger callback
        if (this.onFrame) {
            this.onFrame(weights, frame);
        }
        
        // Cleanup old frames periodically
        if (this.latencyTracker.totalFrames % 30 === 0) {
            this.blendshapeQueue.cleanupOld(playheadMs);
        }
        
        return weights;
    }
    
    /**
     * Set emotion with smooth transition
     */
    setEmotion(emotionName, transitionDuration = null) {
        this.emotionController.setEmotion(emotionName, transitionDuration);
    }
    
    /**
     * Send audio to A2F (PCM16 mono)
     */
    sendAudio(pcm16ArrayBuffer) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(pcm16ArrayBuffer);
        }
    }
    
    /**
     * Get current performance metrics
     */
    getMetrics() {
        const latency = this.latencyTracker.getMetrics();
        const queue = this.blendshapeQueue.getStats();
        
        const metrics = {
            ...latency,
            ...queue,
            interpolationEnabled: this.options.interpolationEnabled,
            emotionControlsEnabled: this.options.enableEmotions,
        };
        
        // Trigger callback
        if (this.onMetrics) {
            this.onMetrics(metrics);
        }
        
        return metrics;
    }
    
    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.isConnected = false;
        this.blendshapeQueue.reset();
    }
}

// ============================================================================
// Latency HUD
// ============================================================================

export class LatencyHUD {
    constructor(containerId) {
        this.container = document.getElementById(containerId);
        if (!this.container) {
            this.container = document.createElement('div');
            this.container.id = containerId;
            this.container.className = 'a2f-latency-hud';
            document.body.appendChild(this.container);
        }
        
        this.render();
    }
    
    render() {
        this.container.innerHTML = `
            <div style="
                position: fixed;
                top: 10px;
                right: 10px;
                background: rgba(0, 0, 0, 0.8);
                color: #0f0;
                font-family: 'Courier New', monospace;
                font-size: 12px;
                padding: 10px;
                border-radius: 4px;
                min-width: 250px;
                z-index: 10000;
            ">
                <div style="font-weight: bold; margin-bottom: 8px; border-bottom: 1px solid #0f0; padding-bottom: 4px;">
                    A2F METRICS
                </div>
                <div id="a2f-hud-content"></div>
            </div>
        `;
        
        this.contentEl = document.getElementById('a2f-hud-content');
    }
    
    update(metrics) {
        const lines = [];
        
        // TTFB
        if (metrics.ttfb !== null) {
            const status = metrics.ttfb < 200 ? '✓' : metrics.ttfb < 500 ? '⚠' : '✗';
            lines.push(`${status} TTFB: ${metrics.ttfb.toFixed(0)}ms`);
        }
        
        // FPS
        if (metrics.effectiveFps) {
            const status = metrics.effectiveFps >= 25 ? '✓' : metrics.effectiveFps >= 15 ? '⚠' : '✗';
            lines.push(`${status} FPS: ${metrics.effectiveFps.toFixed(1)}`);
        }
        
        // Frame time
        if (metrics.avgFrameTime) {
            lines.push(`  Frame: ${metrics.avgFrameTime.toFixed(1)}ms`);
        }
        
        // A/V Skew
        if (metrics.audioVideoSkew !== null) {
            const status = Math.abs(metrics.audioVideoSkew) < 120 ? '✓' : '⚠';
            lines.push(`${status} A/V Skew: ${metrics.audioVideoSkew.toFixed(0)}ms`);
        }
        
        // Queue
        lines.push(`Queue: ${metrics.queueSize}/${metrics.maxSize}`);
        
        // Drops
        const dropStatus = metrics.droppedCount === 0 ? '✓' : parseFloat(metrics.dropRate) < 5 ? '⚠' : '✗';
        lines.push(`${dropStatus} Drops: ${metrics.droppedCount} (${metrics.dropRate})`);
        
        // Interpolation
        lines.push(`Interp: ${metrics.interpolationEnabled ? 'ON' : 'OFF'}`);
        
        // Total frames
        lines.push(`Frames: ${metrics.totalFrames}`);
        
        this.contentEl.innerHTML = lines.join('<br>');
    }
    
    hide() {
        this.container.style.display = 'none';
    }
    
    show() {
        this.container.style.display = 'block';
    }
}

// Export all
export { CONFIG, BlendshapeQueue, LatencyTracker, EmotionController, EMOTION_PRESETS };

