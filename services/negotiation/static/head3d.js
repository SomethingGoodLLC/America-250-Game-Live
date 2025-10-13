/**
 * Three.js 3D Head Renderer with ARKit-52 Morph Targets
 * 
 * Loads a GLB model with ARKit blendshape morph targets and applies
 * real-time weights from NVIDIA Audio2Face service.
 * 
 * Requirements:
 * - GLB model must have ARKit-52 morph target names
 * - Model should be photoreal head (MetaHuman-style recommended)
 * - Morph targets must match names from arkit52.py
 */

import * as THREE from 'https://cdn.jsdelivr.net/npm/three@0.168.0/build/three.module.js';
import { GLTFLoader } from 'https://cdn.jsdelivr.net/npm/three@0.168.0/examples/jsm/loaders/GLTFLoader.js';
import { OrbitControls } from 'https://cdn.jsdelivr.net/npm/three@0.168.0/examples/jsm/controls/OrbitControls.js';

/**
 * ARKit-52 blendshape names (must match backend)
 */
const ARKIT52_NAMES = [
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight",
    "eyeBlinkLeft", "eyeBlinkRight",
    "eyeLookDownLeft", "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight",
    "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight",
    "eyeSquintLeft", "eyeSquintRight", "eyeWideLeft", "eyeWideRight",
    "jawForward", "jawLeft", "jawOpen", "jawRight",
    "mouthClose", "mouthDimpleLeft", "mouthDimpleRight",
    "mouthFrownLeft", "mouthFrownRight", "mouthFunnel", "mouthLeft",
    "mouthLowerDownLeft", "mouthLowerDownRight",
    "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight",
    "mouthRollLower", "mouthRollUpper",
    "mouthShrugLower", "mouthShrugUpper",
    "mouthSmileLeft", "mouthSmileRight",
    "mouthStretchLeft", "mouthStretchRight",
    "mouthUpperUpLeft", "mouthUpperUpRight",
    "noseSneerLeft", "noseSneerRight",
    "tongueOut"
];

/**
 * Head3D - Three.js renderer for animated 3D head
 */
export class Head3D {
    constructor(canvasElement, options = {}) {
        this.canvas = canvasElement;
        this.options = {
            modelPath: options.modelPath || '/static/models/head.glb',
            cameraDistance: options.cameraDistance || 1.5,
            fov: options.fov || 45,
            enableControls: options.enableControls !== false,
            backgroundColor: options.backgroundColor || 0x1a1a1a,
            ...options
        };
        
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.controls = null;
        this.headMesh = null;
        this.morphTargetIndices = {};
        this.isLoaded = false;
        
        this._init();
    }
    
    /**
     * Initialize Three.js scene
     */
    _init() {
        // Create scene
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(this.options.backgroundColor);
        
        // Create camera
        const aspect = this.canvas.clientWidth / this.canvas.clientHeight;
        this.camera = new THREE.PerspectiveCamera(
            this.options.fov,
            aspect,
            0.1,
            1000
        );
        this.camera.position.set(0, 0, this.options.cameraDistance);
        
        // Create renderer
        this.renderer = new THREE.WebGLRenderer({
            canvas: this.canvas,
            antialias: true,
            alpha: true
        });
        this.renderer.setSize(this.canvas.clientWidth, this.canvas.clientHeight);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        this.renderer.outputColorSpace = THREE.SRGBColorSpace;
        this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
        this.renderer.toneMappingExposure = 1.0;
        
        // Add lights
        this._setupLights();
        
        // Add orbit controls (optional)
        if (this.options.enableControls) {
            this.controls = new OrbitControls(this.camera, this.canvas);
            this.controls.enableDamping = true;
            this.controls.dampingFactor = 0.05;
            this.controls.enableZoom = true;
            this.controls.enablePan = false;
        }
        
        // Handle window resize
        window.addEventListener('resize', () => this._onResize());
        
        // Start animation loop
        this._animate();
        
        console.log('Head3D: Scene initialized');
    }
    
    /**
     * Setup scene lighting
     */
    _setupLights() {
        // Key light (main illumination)
        const keyLight = new THREE.DirectionalLight(0xffffff, 1.0);
        keyLight.position.set(1, 1, 1);
        this.scene.add(keyLight);
        
        // Fill light (soften shadows)
        const fillLight = new THREE.DirectionalLight(0xffffff, 0.5);
        fillLight.position.set(-1, 0.5, 0.5);
        this.scene.add(fillLight);
        
        // Back light (rim lighting)
        const backLight = new THREE.DirectionalLight(0xffffff, 0.3);
        backLight.position.set(0, 0, -1);
        this.scene.add(backLight);
        
        // Ambient light
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
        this.scene.add(ambientLight);
    }
    
    /**
     * Load 3D head model from GLB file
     */
    async loadModel(modelPath = null) {
        const path = modelPath || this.options.modelPath;
        
        return new Promise((resolve, reject) => {
            const loader = new GLTFLoader();
            
            console.log(`Head3D: Loading model from ${path}`);
            
            loader.load(
                path,
                (gltf) => {
                    console.log('Head3D: Model loaded successfully');
                    
                    // Find mesh with morph targets
                    gltf.scene.traverse((node) => {
                        if (node.isMesh && node.morphTargetInfluences) {
                            this.headMesh = node;
                            console.log('Head3D: Found mesh with morph targets');
                        }
                    });
                    
                    if (!this.headMesh) {
                        reject(new Error('No mesh with morph targets found in model'));
                        return;
                    }
                    
                    // Build morph target index map
                    this._buildMorphTargetMap();
                    
                    // Add model to scene
                    this.scene.add(gltf.scene);
                    
                    // Center and scale model
                    const box = new THREE.Box3().setFromObject(gltf.scene);
                    const center = box.getCenter(new THREE.Vector3());
                    gltf.scene.position.sub(center);
                    
                    this.isLoaded = true;
                    console.log('Head3D: Model ready');
                    resolve(gltf);
                },
                (progress) => {
                    const percent = (progress.loaded / progress.total) * 100;
                    console.log(`Head3D: Loading... ${percent.toFixed(0)}%`);
                },
                (error) => {
                    console.error('Head3D: Failed to load model', error);
                    reject(error);
                }
            );
        });
    }
    
    /**
     * Build map of ARKit blendshape names to morph target indices
     */
    _buildMorphTargetMap() {
        if (!this.headMesh || !this.headMesh.morphTargetDictionary) {
            console.warn('Head3D: No morph targets found');
            return;
        }
        
        const dictionary = this.headMesh.morphTargetDictionary;
        this.morphTargetIndices = {};
        
        // Map ARKit names to Three.js morph target indices
        ARKIT52_NAMES.forEach((name, arkitIndex) => {
            if (name in dictionary) {
                this.morphTargetIndices[arkitIndex] = dictionary[name];
            } else {
                console.warn(`Head3D: Morph target '${name}' not found in model`);
            }
        });
        
        const mappedCount = Object.keys(this.morphTargetIndices).length;
        console.log(`Head3D: Mapped ${mappedCount}/52 ARKit blendshapes`);
    }
    
    /**
     * Apply blendshape weights from Audio2Face
     * @param {Array<number>} weights - Array of 52 ARKit weights (0.0 to 1.0)
     */
    applyBlendshapes(weights) {
        if (!this.isLoaded || !this.headMesh) {
            console.warn('Head3D: Model not loaded yet');
            return;
        }
        
        if (weights.length !== 52) {
            console.warn(`Head3D: Expected 52 weights, got ${weights.length}`);
            return;
        }
        
        // Apply each weight to corresponding morph target
        weights.forEach((weight, arkitIndex) => {
            const morphIndex = this.morphTargetIndices[arkitIndex];
            if (morphIndex !== undefined) {
                // Clamp weight to valid range
                this.headMesh.morphTargetInfluences[morphIndex] = 
                    Math.max(0.0, Math.min(1.0, weight));
            }
        });
    }
    
    /**
     * Reset all blendshapes to neutral (0.0)
     */
    resetBlendshapes() {
        if (!this.isLoaded || !this.headMesh) return;
        
        const neutralWeights = new Array(52).fill(0.0);
        this.applyBlendshapes(neutralWeights);
    }
    
    /**
     * Animation loop
     */
    _animate() {
        requestAnimationFrame(() => this._animate());
        
        // Update controls
        if (this.controls) {
            this.controls.update();
        }
        
        // Render scene
        this.renderer.render(this.scene, this.camera);
    }
    
    /**
     * Handle window resize
     */
    _onResize() {
        const width = this.canvas.clientWidth;
        const height = this.canvas.clientHeight;
        
        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();
        
        this.renderer.setSize(width, height);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    }
    
    /**
     * Cleanup resources
     */
    dispose() {
        window.removeEventListener('resize', this._onResize);
        
        if (this.controls) {
            this.controls.dispose();
        }
        
        if (this.renderer) {
            this.renderer.dispose();
        }
        
        // Clean up scene resources
        this.scene.traverse((object) => {
            if (object.geometry) object.geometry.dispose();
            if (object.material) {
                if (Array.isArray(object.material)) {
                    object.material.forEach(m => m.dispose());
                } else {
                    object.material.dispose();
                }
            }
        });
    }
}

/**
 * Utility: Create placeholder head geometry (for testing without GLB)
 */
export function createPlaceholderHead(scene) {
    // Simple sphere with basic face features
    const headGeometry = new THREE.SphereGeometry(0.5, 32, 32);
    const headMaterial = new THREE.MeshStandardMaterial({
        color: 0xffdbac,
        roughness: 0.5,
        metalness: 0.0
    });
    const head = new THREE.Mesh(headGeometry, headMaterial);
    scene.add(head);
    
    console.log('Head3D: Created placeholder head (no morph targets)');
    return head;
}

/**
 * Utility: Apply blendshapes by name (for functional API integration)
 * @param {Head3D} head3d - Head3D instance
 * @param {Array<string>} names - ARKit blendshape names
 * @param {Array<number>} weights - Corresponding weights (0.0-1.0)
 */
export function applyBlendshapesByName(head3d, names, weights) {
    if (!head3d || !head3d.isLoaded) {
        return;
    }
    
    // Build weights array indexed by ARKit-52 order
    const arkitWeights = new Array(52).fill(0.0);
    
    names.forEach((name, i) => {
        const arkitIndex = ARKIT52_NAMES.indexOf(name);
        if (arkitIndex >= 0 && i < weights.length) {
            arkitWeights[arkitIndex] = weights[i];
        }
    });
    
    head3d.applyBlendshapes(arkitWeights);
}

/**
 * Utility: Initialize head and return instance (simple wrapper)
 */
export async function initHead(canvasElement, modelPath = '/static/models/head.glb') {
    const head = new Head3D(canvasElement, {
        modelPath: modelPath,
        enableControls: true
    });
    
    await head.loadModel(modelPath);
    return head;
}

