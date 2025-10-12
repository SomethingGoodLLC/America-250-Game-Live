/**
 * Lip Sync Manager
 * 
 * Handles:
 * - Phoneme-to-viseme mapping
 * - Viseme event scheduling and playback
 * - Blendshape animation for ARKit
 * - Event queue with backpressure management
 */

class LipSyncManager {
    constructor() {
        // Event queues
        this.phonemeQueue = [];
        this.visemeQueue = [];
        this.blendshapeQueue = [];
        
        // Playback state
        this.isPlaying = false;
        this.startTime = 0;
        this.currentPlayheadMs = 0;
        
        // Current mouth state
        this.currentVisemeId = 0;
        this.currentMouthOpen = 0.0;
        
        // Animation smoothing
        this.targetMouthOpen = 0.0;
        this.smoothingFactor = 0.3;
        
        // Buffer management
        this.maxQueueSize = 100;
        this.droppedEventCount = 0;
        
        // Viseme-to-mouth-opening mapping (SAPI/Azure viseme IDs 0-21)
        this.visemeToMouth = {
            0: 0.0,   // Silence
            1: 0.2,   // ae, ax, ah
            2: 0.3,   // aa
            3: 0.1,   // ao
            4: 0.4,   // ey
            5: 0.2,   // eh
            6: 0.6,   // uh
            7: 0.3,   // y
            8: 0.5,   // iy
            9: 0.3,   // iy
            10: 0.1,  // m, p, b
            11: 0.2,  // w
            12: 0.3,  // aw
            13: 0.4,  // ow
            14: 0.2,  // uh
            15: 0.5,  // er
            16: 0.4,  // ay
            17: 0.3,  // h
            18: 0.4,  // r
            19: 0.1,  // l
            20: 0.1,  // s, z
            21: 0.15  // sh, ch, zh
        };
        
        // ARKit blendshape mapping for visemes
        this.visemeToBlendshapes = {
            0: { jawOpen: 0.0, mouthClose: 1.0 },
            1: { jawOpen: 0.3, mouthSmileLeft: 0.2, mouthSmileRight: 0.2 },
            2: { jawOpen: 0.7, mouthOpen: 0.6 },
            3: { jawOpen: 0.2, mouthFunnel: 0.3 },
            4: { jawOpen: 0.4, mouthSmileLeft: 0.4, mouthSmileRight: 0.4 },
            5: { jawOpen: 0.3, mouthStretchLeft: 0.2, mouthStretchRight: 0.2 },
            6: { jawOpen: 0.5, mouthPucker: 0.3 },
            7: { jawOpen: 0.2, mouthSmileLeft: 0.3, mouthSmileRight: 0.3 },
            8: { jawOpen: 0.4, mouthSmileLeft: 0.5, mouthSmileRight: 0.5 },
            9: { jawOpen: 0.4, mouthSmileLeft: 0.5, mouthSmileRight: 0.5 },
            10: { jawOpen: 0.0, mouthClose: 0.8, mouthPressLeft: 0.5, mouthPressRight: 0.5 },
            11: { jawOpen: 0.1, mouthPucker: 0.6 },
            12: { jawOpen: 0.6, mouthFunnel: 0.4 },
            13: { jawOpen: 0.3, mouthFunnel: 0.5 },
            14: { jawOpen: 0.2, mouthPucker: 0.2 },
            15: { jawOpen: 0.4, mouthFrownLeft: 0.2, mouthFrownRight: 0.2 },
            16: { jawOpen: 0.6, mouthOpen: 0.5 },
            17: { jawOpen: 0.3 },
            18: { jawOpen: 0.3, mouthPucker: 0.2 },
            19: { jawOpen: 0.2, tongueOut: 0.3 },
            20: { jawOpen: 0.1, mouthSmileLeft: 0.3, mouthSmileRight: 0.3 },
            21: { jawOpen: 0.2, mouthPucker: 0.4 }
        };
        
        // Update interval
        this.updateInterval = null;
    }
    
    /**
     * Initialize lip sync manager
     */
    init() {
        console.log('Initializing lip sync manager...');
        
        // Start update loop (60fps)
        this.updateInterval = setInterval(() => this.update(), 16);
        
        console.log('Lip sync manager initialized');
    }
    
    /**
     * Add phoneme timestamps (array of {phoneme, start_ms, end_ms})
     */
    addPhonemes(phonemes) {
        if (!Array.isArray(phonemes)) {
            return;
        }
        
        // Convert phonemes to visemes
        phonemes.forEach(phoneme => {
            const visemeId = this.phonemeToVisemeId(phoneme.phoneme);
            
            this.visemeQueue.push({
                viseme_id: visemeId,
                audio_offset_ms: phoneme.start_ms,
                duration_ms: phoneme.end_ms - phoneme.start_ms
            });
        });
        
        // Apply backpressure if queue is too large
        this.applyBackpressure();
    }
    
    /**
     * Add single viseme event
     */
    addViseme(viseme) {
        if (!viseme || viseme.viseme_id === undefined) {
            return;
        }
        
        this.visemeQueue.push({
            viseme_id: viseme.viseme_id,
            audio_offset_ms: viseme.audio_offset_ms,
            duration_ms: viseme.duration_ms || 50 // Default 50ms duration
        });
        
        this.applyBackpressure();
    }
    
    /**
     * Add blendshape frames (array of {t_ms, weights[]})
     */
    addBlendshapes(blendshapes) {
        if (!Array.isArray(blendshapes)) {
            return;
        }
        
        blendshapes.forEach(frame => {
            this.blendshapeQueue.push({
                timestamp_ms: frame.t_ms,
                weights: frame.weights
            });
        });
        
        this.applyBackpressure();
    }
    
    /**
     * Map phoneme to SAPI viseme ID
     * 
     * @param {string} phoneme - IPA or ARPABET phoneme
     * @returns {number} - SAPI viseme ID (0-21)
     */
    phonemeToVisemeId(phoneme) {
        // Simple mapping (extend this for more accurate results)
        const phonemelower = phoneme.toLowerCase();
        
        // Map common IPA/ARPABET to SAPI viseme IDs
        const phonemeMap = {
            'a': 1, 'æ': 1, 'ax': 1, 'ah': 1,
            'ɑ': 2, 'aa': 2, 'ɔ': 3, 'ao': 3,
            'e': 4, 'ey': 4, 'ɛ': 5, 'eh': 5,
            'ə': 6, 'uh': 6, 'ʌ': 6,
            'i': 8, 'iy': 8, 'ɪ': 9, 'ih': 9,
            'm': 10, 'p': 10, 'b': 10,
            'w': 11, 'aw': 12, 'o': 13, 'ow': 13,
            'ɜ': 15, 'er': 15,
            'aɪ': 16, 'ay': 16,
            'h': 17, 'r': 18, 'l': 19,
            's': 20, 'z': 20, 'ʃ': 21, 'ʒ': 21, 'ʧ': 21, 'ʤ': 21
        };
        
        return phonemeMap[phonemelower] || 0;
    }
    
    /**
     * Apply backpressure by dropping oldest events
     */
    applyBackpressure() {
        const totalEvents = this.visemeQueue.length + this.blendshapeQueue.length;
        
        if (totalEvents > this.maxQueueSize) {
            const overflow = totalEvents - this.maxQueueSize;
            
            // Drop oldest viseme events first
            const toDrop = Math.min(overflow, this.visemeQueue.length);
            this.visemeQueue.splice(0, toDrop);
            this.droppedEventCount += toDrop;
            
            console.warn(`Lip sync backpressure: dropped ${toDrop} events`);
        }
    }
    
    /**
     * Drop old events (called when audio buffer is too full)
     */
    dropOldEvents() {
        const dropCount = Math.min(10, this.visemeQueue.length);
        this.visemeQueue.splice(0, dropCount);
        this.droppedEventCount += dropCount;
    }
    
    /**
     * Start playback
     */
    start() {
        this.isPlaying = true;
        this.startTime = performance.now();
        this.currentPlayheadMs = 0;
    }
    
    /**
     * Stop playback
     */
    stop() {
        this.isPlaying = false;
        this.reset();
    }
    
    /**
     * Reset state
     */
    reset() {
        this.phonemeQueue = [];
        this.visemeQueue = [];
        this.blendshapeQueue = [];
        this.currentPlayheadMs = 0;
        this.currentVisemeId = 0;
        this.currentMouthOpen = 0.0;
        this.targetMouthOpen = 0.0;
    }
    
    /**
     * Update lip sync animation (called every frame)
     */
    update() {
        // Always smooth towards target, even when not playing
        this.smoothMouthAnimation();
        
        if (!this.isPlaying && this.visemeQueue.length > 0) {
            this.start();
        }
        
        if (!this.isPlaying) {
            return;
        }
        
        // Update playhead
        const currentTime = performance.now();
        this.currentPlayheadMs = currentTime - this.startTime;
        
        // Process viseme events
        this.processVisemeQueue();
        
        // Process blendshape events
        this.processBlendshapeQueue();
        
        // Apply animation to avatar
        this.applyToAvatar();
    }
    
    /**
     * Process viseme queue
     */
    processVisemeQueue() {
        // Process all events that should have triggered by now
        while (this.visemeQueue.length > 0) {
            const event = this.visemeQueue[0];
            
            if (event.audio_offset_ms <= this.currentPlayheadMs) {
                // Trigger this viseme
                this.visemeQueue.shift();
                this.currentVisemeId = event.viseme_id;
                
                // Set target mouth opening
                this.targetMouthOpen = this.visemeToMouth[this.currentVisemeId] || 0.0;
                
            } else {
                // Future events, stop processing
                break;
            }
        }
        
        // Return to neutral if no more events soon
        if (this.visemeQueue.length === 0) {
            this.targetMouthOpen = 0.0;
        }
    }
    
    /**
     * Process blendshape queue
     */
    processBlendshapeQueue() {
        // Process all blendshape frames that should apply now
        while (this.blendshapeQueue.length > 0) {
            const frame = this.blendshapeQueue[0];
            
            if (frame.timestamp_ms <= this.currentPlayheadMs) {
                // Apply this frame
                this.blendshapeQueue.shift();
                this.applyBlendshapeFrame(frame);
            } else {
                break;
            }
        }
    }
    
    /**
     * Smooth mouth animation
     */
    smoothMouthAnimation() {
        // Exponential smoothing
        this.currentMouthOpen += (this.targetMouthOpen - this.currentMouthOpen) * this.smoothingFactor;
        
        // Clamp to [0, 1]
        this.currentMouthOpen = Math.max(0.0, Math.min(1.0, this.currentMouthOpen));
    }
    
    /**
     * Apply current animation to avatar
     */
    applyToAvatar() {
        if (!window.avatarManager) {
            return;
        }
        
        // Check if lip sync is enabled
        const enabled = document.getElementById('enable-lipsync')?.checked;
        if (!enabled) {
            return;
        }
        
        // Animate mouth
        window.avatarManager.animateMouth(this.currentMouthOpen);
        
        // Apply blendshapes for current viseme
        if (window.avatarManager.morphTargets) {
            const blendshapes = this.visemeToBlendshapes[this.currentVisemeId] || {};
            
            for (const [shapeName, weight] of Object.entries(blendshapes)) {
                window.avatarManager.setBlendShape(shapeName, weight);
            }
        }
    }
    
    /**
     * Apply ARKit blendshape frame (55 weights)
     */
    applyBlendshapeFrame(frame) {
        if (!window.avatarManager || !window.avatarManager.morphTargets) {
            return;
        }
        
        // ARKit blendshape order (52 facial + 3 transform)
        const arkitBlendshapeNames = [
            'browDownLeft', 'browDownRight', 'browInnerUp',
            'browOuterUpLeft', 'browOuterUpRight',
            'cheekPuff', 'cheekSquintLeft', 'cheekSquintRight',
            'eyeBlinkLeft', 'eyeBlinkRight',
            'eyeLookDownLeft', 'eyeLookDownRight',
            'eyeLookInLeft', 'eyeLookInRight',
            'eyeLookOutLeft', 'eyeLookOutRight',
            'eyeLookUpLeft', 'eyeLookUpRight',
            'eyeSquintLeft', 'eyeSquintRight',
            'eyeWideLeft', 'eyeWideRight',
            'jawForward', 'jawLeft', 'jawOpen', 'jawRight',
            'mouthClose', 'mouthDimpleLeft', 'mouthDimpleRight',
            'mouthFrownLeft', 'mouthFrownRight',
            'mouthFunnel', 'mouthLeft',
            'mouthLowerDownLeft', 'mouthLowerDownRight',
            'mouthPressLeft', 'mouthPressRight',
            'mouthPucker', 'mouthRight',
            'mouthRollLower', 'mouthRollUpper',
            'mouthShrugLower', 'mouthShrugUpper',
            'mouthSmileLeft', 'mouthSmileRight',
            'mouthStretchLeft', 'mouthStretchRight',
            'mouthUpperUpLeft', 'mouthUpperUpRight',
            'noseSneerLeft', 'noseSneerRight',
            'tongueOut'
        ];
        
        // Apply each blendshape weight
        frame.weights.forEach((weight, index) => {
            if (index < arkitBlendshapeNames.length) {
                window.avatarManager.setBlendShape(arkitBlendshapeNames[index], weight);
            }
        });
    }
    
    /**
     * Cleanup
     */
    dispose() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
        }
        
        this.reset();
    }
}

// Global instance
window.lipSyncManager = new LipSyncManager();


/**
 * LipSyncEngine
 * 
 * Advanced lip-sync scheduler with ADSR envelope and multi-format support.
 * Handles phonemes, visemes, and blendshape frames with precise timing.
 */
class LipSyncEngine {
    /**
     * @param {Function} getPlayheadMs - Returns audio playhead in milliseconds
     * @param {Function} setMorphs - Function to set avatar morph weights (dict with 0..1 values)
     * @param {string} mode - 'viseme' | 'blendshape' | 'phoneme'
     */
    constructor(getPlayheadMs, setMorphs, mode = 'viseme') {
        this.getPlayheadMs = getPlayheadMs;
        this.setMorphs = setMorphs;
        this.mode = mode;
        
        // Event queues
        this.visemeEvents = [];      // [{visemeId, audioOffsetMs, duration}]
        this.blendshapeFrames = [];  // [{tMs, weights[55]}]
        
        // Current state
        this.currentWeights = {};
        this.targetWeights = {};
        this.activeEvents = [];      // Events currently in their envelope
        
        // ADSR envelope parameters (ms)
        this.attackTime = 15;        // 10-20ms attack
        this.releaseTime = 40;       // 30-50ms release
        this.lookAheadMs = 30;       // Look ahead window
        
        // Silence gate
        this.silenceThreshold = 0.01;
        this.silenceTimeMs = 0;
        this.maxSilenceMs = 100;
        
        // Animation frame ID
        this.tickHandle = null;
        this.isRunning = false;
        
        // Import mapping functions dynamically
        this.mappingReady = false;
        this.loadMapping();
    }
    
    /**
     * Load mapping functions from mapping.js
     */
    async loadMapping() {
        try {
            const module = await import('./mapping.js');
            this.arpabetToOVR = module.arpabetToOVR;
            this.getVisemeBlendshapes = module.getVisemeBlendshapes;
            this.OVRViseme = module.OVRViseme;
            this.mappingReady = true;
        } catch (error) {
            console.warn('Failed to load mapping.js, using fallback:', error);
            // Fallback minimal mapping
            this.arpabetToOVR = (phoneme) => 0;
            this.getVisemeBlendshapes = (visemeId) => ({ jawOpen: 0 });
            this.OVRViseme = { SIL: 0 };
            this.mappingReady = true;
        }
    }
    
    /**
     * Ingest phonemes and map to visemes
     * @param {Array} phonemes - Array of {phoneme, start_ms, end_ms} or {phoneme, audioOffsetMs, duration}
     */
    ingestPhonemes(phonemes) {
        if (!Array.isArray(phonemes) || !this.mappingReady) {
            return;
        }
        
        phonemes.forEach(p => {
            // Map phoneme to viseme ID
            const visemeId = this.arpabetToOVR(p.phoneme || p.phoneme_id || '');
            
            // Normalize the event structure
            const audioOffsetMs = p.audioOffsetMs ?? p.start_ms ?? p.audio_offset_ms ?? 0;
            const duration = p.duration ?? (p.end_ms ? p.end_ms - p.start_ms : 100);
            
            this.visemeEvents.push({
                visemeId,
                audioOffsetMs,
                duration
            });
        });
        
        // Sort by time
        this.visemeEvents.sort((a, b) => a.audioOffsetMs - b.audioOffsetMs);
    }
    
    /**
     * Ingest single viseme event
     * @param {Object} event - {visemeId, audioOffsetMs, duration}
     */
    ingestVisemeEvent(event) {
        if (!event || event.visemeId === undefined) {
            return;
        }
        
        const audioOffsetMs = event.audioOffsetMs ?? event.audio_offset_ms ?? 0;
        const duration = event.duration ?? event.duration_ms ?? 100;
        
        this.visemeEvents.push({
            visemeId: event.visemeId ?? event.viseme_id,
            audioOffsetMs,
            duration
        });
        
        // Keep sorted
        this.visemeEvents.sort((a, b) => a.audioOffsetMs - b.audioOffsetMs);
    }
    
    /**
     * Ingest blendshape frames
     * @param {Array} frames - Array of {tMs, weights[55]}
     */
    ingestBlendshapeFrames(frames) {
        if (!Array.isArray(frames)) {
            return;
        }
        
        frames.forEach(frame => {
            this.blendshapeFrames.push({
                tMs: frame.tMs ?? frame.t_ms ?? frame.timestamp_ms ?? 0,
                weights: frame.weights || []
            });
        });
        
        // Sort by time
        this.blendshapeFrames.sort((a, b) => a.tMs - b.tMs);
    }
    
    /**
     * Main tick function - call this every frame
     */
    tick() {
        if (!this.mappingReady) {
            return;
        }
        
        const currentMs = this.getPlayheadMs();
        const lookAheadTarget = currentMs + this.lookAheadMs;
        
        // Process events based on mode
        if (this.mode === 'blendshape') {
            this.processBlendshapeMode(currentMs);
        } else {
            this.processVisemeMode(currentMs, lookAheadTarget);
        }
        
        // Apply silence gate
        this.applySilenceGate(currentMs);
        
        // Apply weights to avatar
        if (this.setMorphs && Object.keys(this.currentWeights).length > 0) {
            this.setMorphs(this.currentWeights);
        }
    }
    
    /**
     * Process viseme/phoneme mode with ADSR envelope
     */
    processVisemeMode(currentMs, lookAheadTarget) {
        // Activate new events in look-ahead window
        while (this.visemeEvents.length > 0 && this.visemeEvents[0].audioOffsetMs <= lookAheadTarget) {
            const event = this.visemeEvents.shift();
            
            // Add to active events with envelope state
            this.activeEvents.push({
                ...event,
                startMs: event.audioOffsetMs,
                endMs: event.audioOffsetMs + event.duration,
                releaseStartMs: event.audioOffsetMs + event.duration,
                releaseEndMs: event.audioOffsetMs + event.duration + this.releaseTime,
                blendshapes: this.getVisemeBlendshapes(event.visemeId)
            });
        }
        
        // Update weights from active events with ADSR envelope
        this.targetWeights = {};
        
        this.activeEvents = this.activeEvents.filter(event => {
            if (currentMs > event.releaseEndMs) {
                // Event finished
                return false;
            }
            
            // Calculate envelope gain
            let gain = 0;
            
            if (currentMs < event.startMs) {
                // Pre-attack (shouldn't happen, but handle gracefully)
                gain = 0;
            } else if (currentMs < event.startMs + this.attackTime) {
                // Attack phase
                const progress = (currentMs - event.startMs) / this.attackTime;
                gain = this.easeInOut(progress);
            } else if (currentMs < event.releaseStartMs) {
                // Sustain phase
                gain = 1.0;
            } else {
                // Release phase
                const progress = (currentMs - event.releaseStartMs) / this.releaseTime;
                gain = 1.0 - this.easeInOut(progress);
            }
            
            // Apply gain to blendshape weights
            for (const [shapeName, weight] of Object.entries(event.blendshapes)) {
                const scaledWeight = weight * gain;
                this.targetWeights[shapeName] = Math.max(
                    this.targetWeights[shapeName] || 0,
                    scaledWeight
                );
            }
            
            return true; // Keep event active
        });
        
        // Smooth transition to target weights
        this.smoothWeights();
    }
    
    /**
     * Process blendshape mode (direct frame application)
     */
    processBlendshapeMode(currentMs) {
        // Apply all frames up to current time
        while (this.blendshapeFrames.length > 0 && this.blendshapeFrames[0].tMs <= currentMs) {
            const frame = this.blendshapeFrames.shift();
            
            // Map 55 ARKit blendshapes to named weights
            const arkitNames = [
                'browDownLeft', 'browDownRight', 'browInnerUp',
                'browOuterUpLeft', 'browOuterUpRight',
                'cheekPuff', 'cheekSquintLeft', 'cheekSquintRight',
                'eyeBlinkLeft', 'eyeBlinkRight',
                'eyeLookDownLeft', 'eyeLookDownRight',
                'eyeLookInLeft', 'eyeLookInRight',
                'eyeLookOutLeft', 'eyeLookOutRight',
                'eyeLookUpLeft', 'eyeLookUpRight',
                'eyeSquintLeft', 'eyeSquintRight',
                'eyeWideLeft', 'eyeWideRight',
                'jawForward', 'jawLeft', 'jawOpen', 'jawRight',
                'mouthClose', 'mouthDimpleLeft', 'mouthDimpleRight',
                'mouthFrownLeft', 'mouthFrownRight',
                'mouthFunnel', 'mouthLeft',
                'mouthLowerDownLeft', 'mouthLowerDownRight',
                'mouthPressLeft', 'mouthPressRight',
                'mouthPucker', 'mouthRight',
                'mouthRollLower', 'mouthRollUpper',
                'mouthShrugLower', 'mouthShrugUpper',
                'mouthSmileLeft', 'mouthSmileRight',
                'mouthStretchLeft', 'mouthStretchRight',
                'mouthUpperUpLeft', 'mouthUpperUpRight',
                'noseSneerLeft', 'noseSneerRight',
                'tongueOut'
            ];
            
            this.currentWeights = {};
            frame.weights.forEach((weight, index) => {
                if (index < arkitNames.length) {
                    this.currentWeights[arkitNames[index]] = weight;
                }
            });
        }
    }
    
    /**
     * Smooth transition between current and target weights
     */
    smoothWeights() {
        const smoothFactor = 0.4; // Higher = faster response
        
        // Interpolate towards target
        for (const [shapeName, targetWeight] of Object.entries(this.targetWeights)) {
            const currentWeight = this.currentWeights[shapeName] || 0;
            this.currentWeights[shapeName] = currentWeight + (targetWeight - currentWeight) * smoothFactor;
        }
        
        // Decay weights not in target
        for (const shapeName of Object.keys(this.currentWeights)) {
            if (!(shapeName in this.targetWeights)) {
                this.currentWeights[shapeName] *= (1 - smoothFactor);
                
                // Remove near-zero weights
                if (this.currentWeights[shapeName] < 0.001) {
                    delete this.currentWeights[shapeName];
                }
            }
        }
    }
    
    /**
     * Apply silence gate to gradually reduce weights when no events
     */
    applySilenceGate(currentMs) {
        // Check if we have active events or upcoming events soon
        const hasActiveEvents = this.activeEvents.length > 0;
        const hasUpcomingEvents = this.visemeEvents.length > 0 && 
                                  this.visemeEvents[0].audioOffsetMs < currentMs + 200;
        
        if (hasActiveEvents || hasUpcomingEvents) {
            this.silenceTimeMs = 0;
            return;
        }
        
        // Accumulate silence time
        this.silenceTimeMs += 16; // ~60fps frame time
        
        if (this.silenceTimeMs > this.maxSilenceMs) {
            // Fade out weights
            const fadeFactor = 0.9;
            for (const shapeName of Object.keys(this.currentWeights)) {
                this.currentWeights[shapeName] *= fadeFactor;
                
                if (this.currentWeights[shapeName] < this.silenceThreshold) {
                    delete this.currentWeights[shapeName];
                }
            }
        }
    }
    
    /**
     * Ease-in-out function for smooth envelope transitions
     * @param {number} t - Progress from 0 to 1
     * @returns {number} Eased value from 0 to 1
     */
    easeInOut(t) {
        // Cubic ease-in-out
        return t < 0.5 
            ? 4 * t * t * t 
            : 1 - Math.pow(-2 * t + 2, 3) / 2;
    }
    
    /**
     * Start the tick loop (requestAnimationFrame)
     */
    start() {
        if (this.isRunning) {
            return;
        }
        
        this.isRunning = true;
        
        const loop = () => {
            if (!this.isRunning) {
                return;
            }
            
            this.tick();
            this.tickHandle = requestAnimationFrame(loop);
        };
        
        loop();
    }
    
    /**
     * Stop the tick loop
     */
    stop() {
        this.isRunning = false;
        
        if (this.tickHandle !== null) {
            cancelAnimationFrame(this.tickHandle);
            this.tickHandle = null;
        }
    }
    
    /**
     * Clear all events and reset state
     */
    reset() {
        this.visemeEvents = [];
        this.blendshapeFrames = [];
        this.activeEvents = [];
        this.currentWeights = {};
        this.targetWeights = {};
        this.silenceTimeMs = 0;
    }
    
    /**
     * Get current queue sizes (for debugging)
     */
    getQueueStats() {
        return {
            visemeEvents: this.visemeEvents.length,
            blendshapeFrames: this.blendshapeFrames.length,
            activeEvents: this.activeEvents.length,
            currentWeights: Object.keys(this.currentWeights).length
        };
    }
}


// Export singleton instance (initialized when needed)
let lipSyncInstance = null;

/**
 * Initialize and get the LipSync singleton
 * @param {Function} getPlayheadMs - Returns audio playhead in milliseconds
 * @param {Function} setMorphs - Function to set avatar morph weights
 * @param {string} mode - 'viseme' | 'blendshape' | 'phoneme'
 * @returns {LipSyncEngine} The singleton instance
 */
export function getLipSync(getPlayheadMs, setMorphs, mode = 'viseme') {
    if (!lipSyncInstance) {
        lipSyncInstance = new LipSyncEngine(getPlayheadMs, setMorphs, mode);
    }
    return lipSyncInstance;
}

/**
 * Direct access to LipSyncEngine class for custom instances
 */
export { LipSyncEngine };

// Make available globally for non-module scripts
window.LipSyncEngine = LipSyncEngine;
window.getLipSync = getLipSync;

