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

