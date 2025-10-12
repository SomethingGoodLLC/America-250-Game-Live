/**
 * Diplomatic Avatar Main Application
 * 
 * Handles:
 * - WebSocket connection to /ws/stream endpoint
 * - AudioWorklet-based PCM playback at 24kHz
 * - UI state management and event handling
 * - Provider/voice selection and configuration
 * - Latency monitoring and HUD updates
 */

class DiplomaticAvatarApp {
    constructor() {
        // WebSocket
        this.ws = null;
        this.wsConnected = false;
        
        // Audio system
        this.audioContext = null;
        this.pcmPlayerNode = null;
        this.audioInitialized = false;
        
        // State
        this.currentProvider = 'cartesia';
        this.currentVoiceId = null;
        this.currentMode = 'phonemes';
        this.sampleRate = 24000;
        this.isSpeaking = false;
        
        // Statistics
        this.stats = {
            audioLatencyMs: 0,
            bufferDepthMs: 0,
            visemeCount: 0,
            droppedEvents: 0,
            lastAudioTimestamp: 0
        };
        
        // Buffer management
        this.targetBufferMs = 50; // Target 40-60ms buffer
        this.maxBufferMs = 120;   // Drop events if buffer > 120ms
        
        // Voice lists cache
        this.voiceLists = {
            cartesia: [],
            azure: [],
            elevenlabs: []
        };
        
        // Initialize after DOM load
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => this.init());
        } else {
            this.init();
        }
    }
    
    /**
     * Initialize application
     */
    async init() {
        console.log('Initializing Diplomatic Avatar App...');
        
        // Initialize UI event handlers
        this.initUIHandlers();
        
        // Load initial voice list
        await this.loadVoices();
        
        // Initialize audio system
        await this.initAudio();
        
        // Initialize avatar (in avatar.js)
        if (window.avatarManager) {
            window.avatarManager.init();
        }
        
        // Initialize lip sync (in lipsync.js)
        if (window.lipSyncManager) {
            window.lipSyncManager.init();
        }
        
        this.log('Application initialized. Ready to connect.');
    }
    
    /**
     * Initialize audio system with AudioWorklet
     */
    async initAudio() {
        try {
            // Create AudioContext at 24kHz
            this.audioContext = new AudioContext({
                sampleRate: this.sampleRate,
                latencyHint: 'interactive'
            });
            
            console.log('AudioContext created:', {
                sampleRate: this.audioContext.sampleRate,
                state: this.audioContext.state
            });
            
            // Load AudioWorklet module
            await this.audioContext.audioWorklet.addModule('/static/pcm-worklet.js');
            console.log('AudioWorklet module loaded');
            
            // Create PCMPlayer node
            this.pcmPlayerNode = new AudioWorkletNode(this.audioContext, 'pcm-player');
            
            // Connect to destination
            this.pcmPlayerNode.connect(this.audioContext.destination);
            
            // Listen for messages from worklet
            this.pcmPlayerNode.port.onmessage = (event) => {
                this.handleWorkletMessage(event.data);
            };
            
            this.audioInitialized = true;
            this.log('Audio system initialized (24kHz AudioWorklet)');
            
        } catch (error) {
            console.error('Failed to initialize audio:', error);
            this.log(`Audio initialization failed: ${error.message}`, 'error');
        }
    }
    
    /**
     * Handle messages from AudioWorklet
     */
    handleWorkletMessage(message) {
        switch (message.kind) {
            case 'playhead':
                // Update playhead position
                break;
                
            case 'buffer_depth':
                this.stats.bufferDepthMs = message.ms;
                this.updateHUD();
                
                // Check if buffer is too full (drop events in WebSocket handler)
                if (message.ms > this.maxBufferMs && window.lipSyncManager) {
                    window.lipSyncManager.dropOldEvents();
                    this.stats.droppedEvents++;
                }
                break;
                
            case 'underrun':
                console.warn('Audio buffer underrun');
                break;
                
            case 'status':
                console.log('Worklet status:', message.message);
                break;
        }
    }
    
    /**
     * Initialize UI event handlers
     */
    initUIHandlers() {
        // Provider selection
        document.getElementById('provider-select').addEventListener('change', (e) => {
            this.currentProvider = e.target.value;
            this.loadVoices();
        });
        
        // Mode selection
        document.getElementById('mode-select').addEventListener('change', (e) => {
            this.currentMode = e.target.value;
        });
        
        // Sample rate selection
        document.getElementById('sample-rate-select').addEventListener('change', (e) => {
            this.sampleRate = parseInt(e.target.value);
        });
        
        // Connection buttons
        document.getElementById('connect-btn').addEventListener('click', () => {
            this.connect();
        });
        
        document.getElementById('disconnect-btn').addEventListener('click', () => {
            this.disconnect();
        });
        
        // Speech control buttons
        document.getElementById('speak-btn').addEventListener('click', () => {
            this.speak();
        });
        
        document.getElementById('stop-btn').addEventListener('click', () => {
            this.stop();
        });
        
        document.getElementById('clear-btn').addEventListener('click', () => {
            document.getElementById('text-input').value = '';
        });
        
        // Refresh voices button
        document.getElementById('refresh-voices-btn').addEventListener('click', () => {
            this.loadVoices(true);
        });
        
        // Quick phrase buttons
        document.querySelectorAll('.quick-phrase').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const text = e.target.dataset.text;
                document.getElementById('text-input').value = text;
            });
        });
        
        // Debug HUD toggle
        document.getElementById('show-debug').addEventListener('change', (e) => {
            document.getElementById('latency-hud').style.display = e.target.checked ? 'block' : 'none';
        });
        
        // Clear log button
        document.getElementById('clear-log-btn').addEventListener('click', () => {
            document.getElementById('event-log').innerHTML = '';
        });
        
        // Enable audio context on user interaction
        document.addEventListener('click', () => {
            if (this.audioContext && this.audioContext.state === 'suspended') {
                this.audioContext.resume();
            }
        }, { once: true });
    }
    
    /**
     * Load voice list for current provider
     */
    async loadVoices(forceRefresh = false) {
        const provider = this.currentProvider;
        const select = document.getElementById('voice-select');
        
        // Use cached list if available
        if (!forceRefresh && this.voiceLists[provider].length > 0) {
            this.populateVoiceSelect(this.voiceLists[provider]);
            return;
        }
        
        select.innerHTML = '<option value="">Loading voices...</option>';
        
        try {
            let response;
            
            if (provider === 'cartesia') {
                response = await fetch('/cartesia/voices');
            } else if (provider === 'azure') {
                response = await fetch('/azure/voices');
            } else if (provider === 'elevenlabs') {
                response = await fetch('/elevenlabs/voices');
            }
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const data = await response.json();
            let voices = [];
            
            // Normalize voice format
            if (provider === 'cartesia' && data.voices) {
                voices = data.voices.map(v => ({
                    id: v.id,
                    name: v.name,
                    language: v.language
                }));
            } else if (provider === 'azure' && data.voices) {
                voices = data.voices.map(v => ({
                    id: v.ShortName,
                    name: v.LocalName,
                    language: v.Locale
                }));
            } else if (provider === 'elevenlabs' && data.voices) {
                voices = data.voices.map(v => ({
                    id: v.voice_id,
                    name: v.name,
                    language: 'en'
                }));
            }
            
            this.voiceLists[provider] = voices;
            this.populateVoiceSelect(voices);
            
            this.log(`Loaded ${voices.length} voices for ${provider}`);
            
        } catch (error) {
            console.error('Failed to load voices:', error);
            select.innerHTML = '<option value="">Failed to load voices</option>';
            this.log(`Failed to load voices: ${error.message}`, 'error');
        }
    }
    
    /**
     * Populate voice select dropdown
     */
    populateVoiceSelect(voices) {
        const select = document.getElementById('voice-select');
        select.innerHTML = '';
        
        if (voices.length === 0) {
            select.innerHTML = '<option value="">No voices available</option>';
            return;
        }
        
        voices.forEach(voice => {
            const option = document.createElement('option');
            option.value = voice.id;
            option.textContent = `${voice.name} (${voice.language})`;
            select.appendChild(option);
        });
        
        // Select first voice by default
        if (voices.length > 0) {
            this.currentVoiceId = voices[0].id;
            select.value = this.currentVoiceId;
        }
        
        // Listen for voice changes
        select.addEventListener('change', (e) => {
            this.currentVoiceId = e.target.value;
        });
    }
    
    /**
     * Connect to WebSocket streaming endpoint
     */
    async connect() {
        if (this.wsConnected) {
            this.log('Already connected', 'warning');
            return;
        }
        
        // Ensure audio context is running
        if (this.audioContext && this.audioContext.state === 'suspended') {
            await this.audioContext.resume();
        }
        
        if (!this.audioInitialized) {
            this.log('Audio system not initialized', 'error');
            return;
        }
        
        if (!this.currentVoiceId) {
            this.log('Please select a voice', 'error');
            return;
        }
        
        try {
            // Construct WebSocket URL
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const host = window.location.host;
            const wsUrl = `${protocol}//${host}/ws/stream`;
            
            this.log(`Connecting to ${wsUrl}...`);
            
            this.ws = new WebSocket(wsUrl);
            
            this.ws.onopen = () => {
                this.onWebSocketOpen();
            };
            
            this.ws.onmessage = (event) => {
                this.onWebSocketMessage(event);
            };
            
            this.ws.onerror = (error) => {
                this.onWebSocketError(error);
            };
            
            this.ws.onclose = () => {
                this.onWebSocketClose();
            };
            
        } catch (error) {
            console.error('WebSocket connection error:', error);
            this.log(`Connection failed: ${error.message}`, 'error');
        }
    }
    
    /**
     * WebSocket opened - send configuration
     */
    onWebSocketOpen() {
        this.log('WebSocket connected. Sending configuration...');
        
        // Send initial configuration
        const config = {
            provider: this.currentProvider,
            voice_id: this.currentVoiceId,
            mode: this.currentMode,
            sample_rate: this.sampleRate
        };
        
        this.ws.send(JSON.stringify(config));
    }
    
    /**
     * Handle WebSocket message
     */
    async onWebSocketMessage(event) {
        try {
            const message = JSON.parse(event.data);
            
            // Handle config acknowledgment
            if (message.type === 'config_ack') {
                this.wsConnected = true;
                this.updateConnectionStatus(true);
                this.log(`Connected with ${message.message}`);
                
                // Enable speak button
                document.getElementById('speak-btn').disabled = false;
                document.getElementById('connect-btn').disabled = true;
                document.getElementById('disconnect-btn').disabled = false;
                return;
            }
            
            // Handle StreamEvent
            if (message.kind) {
                await this.handleStreamEvent(message);
            }
            
        } catch (error) {
            console.error('Error handling WebSocket message:', error);
        }
    }
    
    /**
     * Handle stream event from server
     */
    async handleStreamEvent(event) {
        switch (event.kind) {
            case 'audio':
                await this.handleAudioEvent(event);
                break;
                
            case 'phonemes':
                this.handlePhonemeEvent(event);
                break;
                
            case 'viseme':
                this.handleVisemeEvent(event);
                break;
                
            case 'blendshapes':
                this.handleBlendshapeEvent(event);
                break;
                
            case 'done':
                this.handleDoneEvent();
                break;
                
            case 'error':
                this.handleErrorEvent(event);
                break;
        }
    }
    
    /**
     * Handle audio chunk
     */
    async handleAudioEvent(event) {
        if (!event.pcm16_b64) {
            return;
        }
        
        // Decode base64 to binary
        const binaryString = atob(event.pcm16_b64);
        const bytes = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
            bytes[i] = binaryString.charCodeAt(i);
        }
        
        // Send to AudioWorklet
        this.pcmPlayerNode.port.postMessage({
            kind: 'push',
            pcm16: bytes.buffer
        });
        
        // Update latency stats
        this.stats.lastAudioTimestamp = performance.now();
    }
    
    /**
     * Handle phoneme event
     */
    handlePhonemeEvent(event) {
        if (window.lipSyncManager && event.phonemes) {
            window.lipSyncManager.addPhonemes(event.phonemes);
        }
    }
    
    /**
     * Handle viseme event
     */
    handleVisemeEvent(event) {
        if (window.lipSyncManager && event.viseme) {
            window.lipSyncManager.addViseme(event.viseme);
            this.stats.visemeCount++;
            this.updateHUD();
        }
    }
    
    /**
     * Handle blendshape event
     */
    handleBlendshapeEvent(event) {
        if (window.lipSyncManager && event.blendshapes) {
            window.lipSyncManager.addBlendshapes(event.blendshapes);
        }
    }
    
    /**
     * Handle synthesis done
     */
    handleDoneEvent() {
        this.log('Synthesis complete');
        this.isSpeaking = false;
        document.getElementById('speak-btn').disabled = false;
        document.getElementById('stop-btn').disabled = true;
    }
    
    /**
     * Handle error event
     */
    handleErrorEvent(event) {
        this.log(`Error: ${event.error}`, 'error');
        this.isSpeaking = false;
        document.getElementById('speak-btn').disabled = false;
        document.getElementById('stop-btn').disabled = true;
    }
    
    /**
     * WebSocket error
     */
    onWebSocketError(error) {
        console.error('WebSocket error:', error);
        this.log('WebSocket error occurred', 'error');
    }
    
    /**
     * WebSocket closed
     */
    onWebSocketClose() {
        this.log('WebSocket disconnected');
        this.wsConnected = false;
        this.updateConnectionStatus(false);
        
        // Reset UI state
        document.getElementById('speak-btn').disabled = true;
        document.getElementById('stop-btn').disabled = true;
        document.getElementById('connect-btn').disabled = false;
        document.getElementById('disconnect-btn').disabled = true;
    }
    
    /**
     * Disconnect WebSocket
     */
    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        
        // Reset audio worklet
        if (this.pcmPlayerNode) {
            this.pcmPlayerNode.port.postMessage({ kind: 'reset' });
        }
    }
    
    /**
     * Send text to speak
     */
    speak() {
        if (!this.wsConnected) {
            this.log('Not connected to server', 'error');
            return;
        }
        
        const text = document.getElementById('text-input').value.trim();
        if (!text) {
            this.log('No text to speak', 'warning');
            return;
        }
        
        this.log(`Speaking: "${text.substring(0, 50)}..."`);
        
        // Send text message
        this.ws.send(JSON.stringify({
            text: text
        }));
        
        this.isSpeaking = true;
        document.getElementById('speak-btn').disabled = true;
        document.getElementById('stop-btn').disabled = false;
        
        // Reset stats
        this.stats.visemeCount = 0;
        this.updateHUD();
    }
    
    /**
     * Stop speaking
     */
    stop() {
        if (!this.wsConnected) {
            return;
        }
        
        this.log('Stopping speech...');
        
        // Send stop command
        this.ws.send(JSON.stringify({
            command: 'stop'
        }));
        
        // Reset audio buffer
        if (this.pcmPlayerNode) {
            this.pcmPlayerNode.port.postMessage({ kind: 'reset' });
        }
        
        this.isSpeaking = false;
        document.getElementById('speak-btn').disabled = false;
        document.getElementById('stop-btn').disabled = true;
    }
    
    /**
     * Update connection status indicator
     */
    updateConnectionStatus(connected) {
        const badge = document.getElementById('connection-status');
        if (connected) {
            badge.textContent = 'Connected';
            badge.className = 'status-badge connected';
        } else {
            badge.textContent = 'Disconnected';
            badge.className = 'status-badge disconnected';
        }
    }
    
    /**
     * Update HUD with current statistics
     */
    updateHUD() {
        document.getElementById('audio-latency').textContent = 
            `${Math.round(this.stats.audioLatencyMs)} ms`;
        document.getElementById('buffer-depth').textContent = 
            `${Math.round(this.stats.bufferDepthMs)} ms`;
        document.getElementById('viseme-count').textContent = 
            this.stats.visemeCount;
        document.getElementById('dropped-events').textContent = 
            this.stats.droppedEvents;
    }
    
    /**
     * Log message to event log
     */
    log(message, level = 'info') {
        const logDiv = document.getElementById('event-log');
        const timestamp = new Date().toLocaleTimeString();
        
        const entry = document.createElement('div');
        entry.className = `log-entry log-${level}`;
        entry.textContent = `[${timestamp}] ${message}`;
        
        logDiv.appendChild(entry);
        logDiv.scrollTop = logDiv.scrollHeight;
        
        // Limit log entries
        while (logDiv.children.length > 100) {
            logDiv.removeChild(logDiv.firstChild);
        }
        
        // Console log
        if (level === 'error') {
            console.error(message);
        } else if (level === 'warning') {
            console.warn(message);
        } else {
            console.log(message);
        }
    }
}

// Initialize app
window.app = new DiplomaticAvatarApp();

