/**
 * Avatar Manager
 * 
 * Handles:
 * - Three.js scene setup and rendering
 * - GLTF avatar loading (Ready Player Me compatible)
 * - Facial animation (morph targets / blend shapes)
 * - Camera controls and lighting
 */

class AvatarManager {
    constructor() {
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.avatar = null;
        this.morphTargets = null;
        this.animationMixer = null;
        
        // Canvas reference
        this.canvas = null;
        
        // Animation state
        this.isAnimating = false;
        this.blinkTimer = 0;
        this.idleTimer = 0;
        
        // Morph target mappings (ARKit blendshape names)
        this.morphTargetMap = {};
        
        // Default avatar URL (Ready Player Me placeholder)
        this.defaultAvatarUrl = null;
        
        // Render loop
        this.lastFrameTime = 0;
    }
    
    /**
     * Initialize Three.js scene and load avatar
     */
    async init() {
        console.log('Initializing avatar manager...');
        
        try {
            this.canvas = document.getElementById('stage');
            if (!this.canvas) {
                console.error('Canvas element #stage not found');
                return;
            }
            
            // Setup scene
            this.setupScene();
            
            // Setup camera
            this.setupCamera();
            
            // Setup renderer
            this.setupRenderer();
            
            // Setup lighting
            this.setupLighting();
            
            // Load placeholder avatar
            await this.loadPlaceholderAvatar();
            
            // Start render loop
            this.startRenderLoop();
            
            // Handle window resize
            window.addEventListener('resize', () => this.onWindowResize());
            
            console.log('Avatar manager initialized');
            
        } catch (error) {
            console.error('Failed to initialize avatar:', error);
        }
    }
    
    /**
     * Setup Three.js scene
     */
    setupScene() {
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x1a1a2e);
    }
    
    /**
     * Setup camera
     */
    setupCamera() {
        const aspect = this.canvas.clientWidth / this.canvas.clientHeight;
        this.camera = new THREE.PerspectiveCamera(45, aspect, 0.1, 1000);
        this.camera.position.set(0, 1.6, 1.5);
        this.camera.lookAt(0, 1.5, 0);
    }
    
    /**
     * Setup WebGL renderer
     */
    setupRenderer() {
        this.renderer = new THREE.WebGLRenderer({
            canvas: this.canvas,
            antialias: true,
            alpha: true
        });
        
        this.renderer.setSize(this.canvas.clientWidth, this.canvas.clientHeight);
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    }
    
    /**
     * Setup scene lighting
     */
    setupLighting() {
        // Ambient light
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.5);
        this.scene.add(ambientLight);
        
        // Key light (front)
        const keyLight = new THREE.DirectionalLight(0xffffff, 0.8);
        keyLight.position.set(0, 2, 3);
        keyLight.castShadow = true;
        this.scene.add(keyLight);
        
        // Fill light (side)
        const fillLight = new THREE.DirectionalLight(0xffffff, 0.3);
        fillLight.position.set(-2, 1, 1);
        this.scene.add(fillLight);
        
        // Rim light (back)
        const rimLight = new THREE.DirectionalLight(0xffffff, 0.2);
        rimLight.position.set(0, 1, -2);
        this.scene.add(rimLight);
    }
    
    /**
     * Load placeholder avatar (basic head geometry)
     */
    async loadPlaceholderAvatar() {
        console.log('Loading placeholder avatar...');
        
        // Create simple head geometry
        const geometry = new THREE.SphereGeometry(0.3, 32, 32);
        const material = new THREE.MeshStandardMaterial({
            color: 0xffdbac,
            roughness: 0.8,
            metalness: 0.1
        });
        
        const head = new THREE.Mesh(geometry, material);
        head.position.set(0, 1.5, 0);
        head.castShadow = true;
        head.receiveShadow = true;
        
        // Create simple mouth
        const mouthGeometry = new THREE.TorusGeometry(0.08, 0.02, 8, 16, Math.PI);
        const mouthMaterial = new THREE.MeshStandardMaterial({ color: 0x331111 });
        const mouth = new THREE.Mesh(mouthGeometry, mouthMaterial);
        mouth.position.set(0, -0.05, 0.25);
        mouth.rotation.x = Math.PI / 2;
        head.add(mouth);
        
        // Create eyes
        const eyeGeometry = new THREE.SphereGeometry(0.04, 16, 16);
        const eyeMaterial = new THREE.MeshStandardMaterial({ color: 0x222222 });
        
        const leftEye = new THREE.Mesh(eyeGeometry, eyeMaterial);
        leftEye.position.set(-0.1, 0.08, 0.26);
        head.add(leftEye);
        
        const rightEye = new THREE.Mesh(eyeGeometry, eyeMaterial);
        rightEye.position.set(0.1, 0.08, 0.26);
        head.add(rightEye);
        
        this.avatar = head;
        this.scene.add(this.avatar);
        
        console.log('Placeholder avatar loaded');
    }
    
    /**
     * Load Ready Player Me avatar from URL
     * 
     * @param {string} avatarUrl - Ready Player Me avatar URL (.glb)
     */
    async loadReadyPlayerMeAvatar(avatarUrl) {
        console.log('Loading Ready Player Me avatar:', avatarUrl);
        
        try {
            // Remove existing avatar
            if (this.avatar) {
                this.scene.remove(this.avatar);
            }
            
            // Load GLTF model
            const loader = new THREE.GLTFLoader();
            
            return new Promise((resolve, reject) => {
                loader.load(
                    avatarUrl,
                    (gltf) => {
                        this.avatar = gltf.scene;
                        
                        // Scale and position avatar
                        this.avatar.scale.set(1, 1, 1);
                        this.avatar.position.set(0, 0, 0);
                        
                        // Enable shadows
                        this.avatar.traverse((child) => {
                            if (child.isMesh) {
                                child.castShadow = true;
                                child.receiveShadow = true;
                                
                                // Find morph targets (blend shapes)
                                if (child.morphTargetDictionary) {
                                    console.log('Found morph targets:', child.morphTargetDictionary);
                                    this.morphTargets = child;
                                    this.morphTargetMap = child.morphTargetDictionary;
                                }
                            }
                        });
                        
                        // Setup animation mixer
                        if (gltf.animations && gltf.animations.length > 0) {
                            this.animationMixer = new THREE.AnimationMixer(this.avatar);
                            gltf.animations.forEach((clip) => {
                                this.animationMixer.clipAction(clip).play();
                            });
                        }
                        
                        this.scene.add(this.avatar);
                        console.log('Ready Player Me avatar loaded');
                        resolve(gltf);
                    },
                    (progress) => {
                        console.log('Loading progress:', (progress.loaded / progress.total * 100).toFixed(2) + '%');
                    },
                    (error) => {
                        console.error('Error loading avatar:', error);
                        reject(error);
                    }
                );
            });
            
        } catch (error) {
            console.error('Failed to load avatar:', error);
            throw error;
        }
    }
    
    /**
     * Set blend shape / morph target weight
     * 
     * @param {string} shapeName - ARKit blend shape name (e.g., "jawOpen", "mouthSmile")
     * @param {number} weight - Weight value [0.0, 1.0]
     */
    setBlendShape(shapeName, weight) {
        if (!this.morphTargets || !this.morphTargetMap) {
            return;
        }
        
        // Map ARKit name to morph target index
        const index = this.morphTargetMap[shapeName];
        if (index !== undefined) {
            this.morphTargets.morphTargetInfluences[index] = weight;
        }
    }
    
    /**
     * Animate mouth opening (simple jaw control)
     * 
     * @param {number} openAmount - Mouth open amount [0.0, 1.0]
     */
    animateMouth(openAmount) {
        // For placeholder avatar, scale mouth
        if (this.avatar && !this.morphTargets) {
            const mouth = this.avatar.children.find(child => child.geometry?.type === 'TorusGeometry');
            if (mouth) {
                mouth.scale.y = 1 + openAmount * 0.5;
            }
        }
        
        // For Ready Player Me avatar, use blend shapes
        if (this.morphTargets) {
            this.setBlendShape('jawOpen', openAmount);
            this.setBlendShape('mouthOpen', openAmount * 0.8);
        }
    }
    
    /**
     * Start render loop
     */
    startRenderLoop() {
        this.isAnimating = true;
        this.animate();
    }
    
    /**
     * Animation loop
     */
    animate() {
        if (!this.isAnimating) {
            return;
        }
        
        requestAnimationFrame(() => this.animate());
        
        const currentTime = performance.now();
        const deltaTime = (currentTime - this.lastFrameTime) / 1000;
        this.lastFrameTime = currentTime;
        
        // Update animation mixer
        if (this.animationMixer) {
            this.animationMixer.update(deltaTime);
        }
        
        // Idle animations
        this.updateIdleAnimations(currentTime);
        
        // Render scene
        this.renderer.render(this.scene, this.camera);
    }
    
    /**
     * Update idle animations (blink, breathing, subtle movement)
     */
    updateIdleAnimations(time) {
        // Blink every 3-5 seconds
        this.blinkTimer += 1;
        if (this.blinkTimer > 180) { // ~3 seconds at 60fps
            this.blink();
            this.blinkTimer = 0;
        }
        
        // Subtle head movement
        if (this.avatar) {
            const breathCycle = Math.sin(time * 0.001) * 0.01;
            this.avatar.position.y = breathCycle;
            
            const headSway = Math.sin(time * 0.0005) * 0.05;
            this.avatar.rotation.y = headSway;
        }
    }
    
    /**
     * Trigger blink animation
     */
    blink() {
        if (!this.morphTargets) {
            return;
        }
        
        // Blink animation (120ms)
        this.setBlendShape('eyeBlinkLeft', 1.0);
        this.setBlendShape('eyeBlinkRight', 1.0);
        
        setTimeout(() => {
            this.setBlendShape('eyeBlinkLeft', 0.0);
            this.setBlendShape('eyeBlinkRight', 0.0);
        }, 120);
    }
    
    /**
     * Handle window resize
     */
    onWindowResize() {
        if (!this.camera || !this.renderer || !this.canvas) {
            return;
        }
        
        const width = this.canvas.clientWidth;
        const height = this.canvas.clientHeight;
        
        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();
        
        this.renderer.setSize(width, height);
    }
    
    /**
     * Stop animation and cleanup
     */
    dispose() {
        this.isAnimating = false;
        
        if (this.renderer) {
            this.renderer.dispose();
        }
        
        if (this.avatar) {
            this.scene.remove(this.avatar);
        }
    }
}

// Global instance
window.avatarManager = new AvatarManager();

