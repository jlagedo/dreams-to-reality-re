import {
  Scene,
  Vector3,
  Ray,
  AbstractMesh,
  TransformNode,
  ArcRotateCamera,
  SceneLoader,
  AssetContainer,
} from '@babylonjs/core';

export interface PlayerInput {
  forward: boolean;
  backward: boolean;
  left: boolean;
  right: boolean;
  sprint: boolean;
  jump: boolean;
}

export class DuncanPlayer {
  public rootNode: TransformNode;
  public animNode: TransformNode;
  public modelRoot: TransformNode;
  public camera: ArcRotateCamera;
  public isSpawned: boolean = false;

  private scene: Scene;
  private container: AssetContainer | null = null;
  private groundMeshes: AbstractMesh[] = [];

  // Movement physics
  private velocity: Vector3 = Vector3.Zero();
  private isGrounded: boolean = true;
  private targetRotation: number = 0; // Face forward away from camera initially
  private currentRotation: number = 0;
  private walkSpeed: number = 7.0;
  private sprintSpeed: number = 14.0;
  private jumpForce: number = 12.0;
  private gravity: number = -26.0;

  // Procedural run wobble & tilt
  private runCycleTime: number = 0;
  private pitch: number = 0;
  private roll: number = 0;
  private bob: number = 0;
  private initialMeshPositions: Map<AbstractMesh, Vector3> = new Map();

  // Input states
  private input: PlayerInput = {
    forward: false,
    backward: false,
    left: false,
    right: false,
    sprint: false,
    jump: false,
  };

  constructor(scene: Scene, camera: ArcRotateCamera) {
    this.scene = scene;
    this.camera = camera;
    this.rootNode = new TransformNode('duncan_root', this.scene);
    this.animNode = new TransformNode('duncan_anim', this.scene);
    this.animNode.parent = this.rootNode;

    this.modelRoot = new TransformNode('duncan_model', this.scene);
    this.modelRoot.parent = this.animNode;
    // In xh.gltf, Duncan is modeled facing +X with ponytail extending in -X.
    // In Babylon.js, rotating by +PI/2 aligns face with +Z (forward, away from camera)
    // and ponytail with -Z (back, towards camera)!
    this.modelRoot.rotation.y = Math.PI / 2;

    this.bindKeyboard();
  }

  public async spawn(position: Vector3, groundMeshes: AbstractMesh[], initialYaw?: number): Promise<void> {
    this.groundMeshes = groundMeshes;

    if (this.container) {
      this.container.removeAllFromScene();
      this.container.dispose();
      this.container = null;
    }

    try {
      this.container = await SceneLoader.LoadAssetContainerAsync(
        '/api/assets/models/',
        'xh.gltf',
        this.scene
      );
      this.container.addAllToScene();

      // Parent all Duncan meshes to our modelRoot
      for (const mesh of this.container.meshes) {
        if (!mesh.parent) {
          mesh.parent = this.modelRoot;
        }
        mesh.isPickable = false; // Don't block raycasts
        this.initialMeshPositions.set(mesh, mesh.position.clone());
      }

      this.rootNode.position = position.clone();
      if (initialYaw !== undefined) {
        this.targetRotation = initialYaw;
        this.currentRotation = initialYaw;
      }
      this.rootNode.rotation.y = this.targetRotation;
      this.isSpawned = true;

      this.snapCamera(initialYaw);
    } catch (err) {
      console.error('Failed to spawn Duncan player:', err);
    }
  }

  public snapCamera(initialYaw?: number): void {
    const targetOffset = new Vector3(0, 1.8, 0);
    this.camera.target = this.rootNode.position.add(targetOffset);
    this.camera.radius = 6.0;
    this.camera.alpha = initialYaw !== undefined ? initialYaw - Math.PI / 2 : -Math.PI / 2;
    this.camera.beta = Math.PI / 2.7;
    this.camera.minZ = 0.2;
    this.camera.lowerRadiusLimit = 2.0;
    this.camera.upperRadiusLimit = 16.0;
    this.camera.wheelPrecision = 25;
  }

  public updateGroundMeshes(meshes: AbstractMesh[]): void {
    this.groundMeshes = meshes.filter(
      (m) =>
        m.name !== 'duncan_root' &&
        m.parent !== this.rootNode &&
        m.parent !== this.animNode &&
        m.parent !== this.modelRoot
    );
  }

  public update(deltaTime: number): void {
    if (!this.isSpawned) return;

    // Movement direction relative to camera angle
    let moveX = 0;
    let moveZ = 0;

    if (this.input.forward) moveZ += 1;
    if (this.input.backward) moveZ -= 1;
    if (this.input.left) moveX -= 1;
    if (this.input.right) moveX += 1;

    const isMoving = moveX !== 0 || moveZ !== 0;

    if (isMoving) {
      // Calculate angle relative to camera view
      const camAlpha = this.camera.alpha;
      const angle = Math.atan2(moveX, moveZ) - camAlpha - Math.PI / 2;
      this.targetRotation = angle;

      const speed = this.input.sprint ? this.sprintSpeed : this.walkSpeed;
      const forwardDir = new Vector3(Math.sin(angle), 0, Math.cos(angle)).normalize();

      this.velocity.x = forwardDir.x * speed;
      this.velocity.z = forwardDir.z * speed;

      // Procedural run wobble & tilt
      this.runCycleTime += deltaTime * (this.input.sprint ? 14 : 9);
      const targetPitch = this.input.sprint ? 0.22 : 0.12; // lean forward into run
      const targetRoll = Math.sin(this.runCycleTime) * (this.input.sprint ? 0.08 : 0.04); // subtle running sway
      const targetBob = Math.sin(this.runCycleTime * 2) * (this.input.sprint ? 0.06 : 0.03);

      this.pitch += (targetPitch - this.pitch) * Math.min(deltaTime * 8, 1.0);
      this.roll += (targetRoll - this.roll) * Math.min(deltaTime * 10, 1.0);
      this.bob += (targetBob - this.bob) * Math.min(deltaTime * 10, 1.0);
    } else {
      this.velocity.x *= 0.8;
      this.velocity.z *= 0.8;
      if (Math.abs(this.velocity.x) < 0.01) this.velocity.x = 0;
      if (Math.abs(this.velocity.z) < 0.01) this.velocity.z = 0;

      // Return to neutral pose when stopped
      this.pitch += (0 - this.pitch) * Math.min(deltaTime * 6, 1.0);
      this.roll += (0 - this.roll) * Math.min(deltaTime * 6, 1.0);
      this.bob += (0 - this.bob) * Math.min(deltaTime * 6, 1.0);
    }

    // Apply procedural tilt/sway to animNode
    if (!this.isGrounded) {
      // Mid-air levitation tilt
      this.animNode.rotation.x = 0.22;
      this.animNode.rotation.z = 0;
      this.animNode.position.y = 0;
    } else {
      this.animNode.rotation.x = this.pitch;
      this.animNode.rotation.z = this.roll;
      this.animNode.position.y = this.bob;
    }

    // Smooth character rotation
    let angleDiff = this.targetRotation - this.currentRotation;
    while (angleDiff < -Math.PI) angleDiff += Math.PI * 2;
    while (angleDiff > Math.PI) angleDiff -= Math.PI * 2;
    this.currentRotation += angleDiff * Math.min(deltaTime * 12, 1.0);
    this.rootNode.rotation.y = this.currentRotation;

    // Ground raycast for snapping and collision
    const rayOrigin = this.rootNode.position.clone();
    rayOrigin.y += 2.0; // Cast from torso height
    const ray = new Ray(rayOrigin, new Vector3(0, -1, 0), 10.0);

    let groundY = -9999;
    let hitGround = false;

    for (const mesh of this.groundMeshes) {
      if (mesh.getTotalVertices() === 0) continue;
      const hit = ray.intersectsMesh(mesh, false);
      if (hit.hit && hit.pickedPoint) {
        if (hit.pickedPoint.y > groundY && hit.pickedPoint.y <= rayOrigin.y + 0.5) {
          groundY = hit.pickedPoint.y;
          hitGround = true;
        }
      }
    }

    // Jump handling
    if (this.input.jump && this.isGrounded) {
      this.velocity.y = this.jumpForce;
      this.isGrounded = false;
    }

    // Apply gravity
    if (!this.isGrounded) {
      this.velocity.y += this.gravity * deltaTime;
    }

    // Apply velocities
    this.rootNode.position.x += this.velocity.x * deltaTime;
    this.rootNode.position.z += this.velocity.z * deltaTime;
    this.rootNode.position.y += this.velocity.y * deltaTime;

    // Ground snap
    if (hitGround && this.rootNode.position.y <= groundY + 0.1) {
      this.rootNode.position.y = groundY;
      this.velocity.y = 0;
      this.isGrounded = true;
    } else if (!hitGround && this.rootNode.position.y < -40) {
      // Fallen off island -> Respawn on altar
      this.respawn();
    }

    // Update camera target to smoothly follow Duncan's upper body
    const targetOffset = new Vector3(0, 1.8, 0);
    this.camera.target = this.rootNode.position.add(targetOffset);

  }

  public respawn(pos?: Vector3): void {
    // Default spawn: in front of the Angkor stone altar on the ground tiles
    const target = pos || new Vector3(-2.0, 8.5, 14.0);
    this.rootNode.position = target.clone();
    this.velocity = Vector3.Zero();
    this.isGrounded = true;
    this.targetRotation = 0;
    this.currentRotation = 0;
    this.snapCamera();
  }

  private bindKeyboard(): void {
    window.addEventListener('keydown', (e) => {
      if (document.activeElement?.tagName === 'INPUT' || document.activeElement?.tagName === 'SELECT') return;

      switch (e.code) {
        case 'KeyW':
        case 'ArrowUp':
          this.input.forward = true;
          break;
        case 'KeyS':
        case 'ArrowDown':
          this.input.backward = true;
          break;
        case 'KeyA':
        case 'ArrowLeft':
          this.input.left = true;
          break;
        case 'KeyD':
        case 'ArrowRight':
          this.input.right = true;
          break;
        case 'ShiftLeft':
        case 'ShiftRight':
          this.input.sprint = true;
          break;
        case 'Space':
          this.input.jump = true;
          e.preventDefault();
          break;
      }
    });

    window.addEventListener('keyup', (e) => {
      switch (e.code) {
        case 'KeyW':
        case 'ArrowUp':
          this.input.forward = false;
          break;
        case 'KeyS':
        case 'ArrowDown':
          this.input.backward = false;
          break;
        case 'KeyA':
        case 'ArrowLeft':
          this.input.left = false;
          break;
        case 'KeyD':
        case 'ArrowRight':
          this.input.right = false;
          break;
        case 'ShiftLeft':
        case 'ShiftRight':
          this.input.sprint = false;
          break;
        case 'Space':
          this.input.jump = false;
          break;
      }
    });
  }
}
