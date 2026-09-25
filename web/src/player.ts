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
import { AnimationLibrary } from './animation/library';
import { detachContainerRoots, SkeletalAnimator } from './animation/renderer';
import type { AnimationClipData } from './animation/types';
import { MODEL_FILE, modelFolder } from './content';

/** Duncan's model folder: XH_.DAN, named without its trailing underscore. */
const DUNCAN = 'xh';

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
  private targetRotation: number = 0;
  private currentRotation: number = 0;
  private walkSpeed: number = 7.0;
  private sprintSpeed: number = 14.0;
  private jumpForce: number = 12.0;
  private gravity: number = -26.0;

  public animator: SkeletalAnimator | null = null;
  public animationReady: Promise<void> = Promise.resolve();
  public animationInspection = false;
  private spawnGeneration = 0;
  private idleClip: AnimationClipData | null = null;
  private runClip: AnimationClipData | null = null;
  private walkClip: AnimationClipData | null = null;
  private flyClip: AnimationClipData | null = null;
  private jumpClip: AnimationClipData | null = null;

  // Procedural lean & tilt into corners
  private pitch: number = 0;

  // Input states
  private input: PlayerInput = {
    forward: false,
    backward: false,
    left: false,
    right: false,
    sprint: false,
    jump: false,
  };

  constructor(scene: Scene, camera: ArcRotateCamera, private readonly animations: AnimationLibrary) {
    this.scene = scene;
    this.camera = camera;
    this.rootNode = new TransformNode('duncan_root', this.scene);
    this.animNode = new TransformNode('duncan_anim', this.scene);
    this.animNode.parent = this.rootNode;

    this.modelRoot = new TransformNode('duncan_model', this.scene);
    this.modelRoot.parent = this.animNode;
    // In xh.gltf, Duncan is modeled facing +X with ponytail extending in -X.
    // The glTF loader's root rotates the source face from +X to -X. This
    // quarter turn then points Duncan along +Z, his movement direction.
    this.modelRoot.rotation.y = Math.PI / 2;

    this.bindKeyboard();
  }

  public spawn(position: Vector3, groundMeshes: AbstractMesh[], initialYaw?: number): Promise<void> {
    this.animationReady = this.spawnInternal(position, groundMeshes, initialYaw);
    return this.animationReady;
  }

  private async spawnInternal(position: Vector3, groundMeshes: AbstractMesh[], initialYaw?: number): Promise<void> {
    const generation = ++this.spawnGeneration;
    this.groundMeshes = groundMeshes;
    this.animator?.dispose();
    this.animator = null;
    this.idleClip = this.runClip = this.walkClip = this.flyClip = this.jumpClip = null;

    if (this.container) {
      detachContainerRoots(this.container);
      this.container.removeAllFromScene();
      this.container.dispose();
      this.container = null;
    }
    try {
      const container = await SceneLoader.LoadAssetContainerAsync(
        modelFolder(DUNCAN),
        MODEL_FILE,
        this.scene
      );
      if (generation !== this.spawnGeneration) { container.dispose(); return; }
      this.container = container;
      container.addAllToScene();

      // Parent all Duncan meshes to our modelRoot
      for (const mesh of this.container.meshes) {
        if (!mesh.parent) {
          mesh.parent = this.modelRoot;
        }
        mesh.isPickable = false; // Don't block raycasts
      }

      this.rootNode.position = position.clone();
      if (initialYaw !== undefined) {
        this.targetRotation = initialYaw;
        this.currentRotation = initialYaw;
      }
      this.rootNode.rotation.y = this.targetRotation;
      this.isSpawned = true;

      this.snapCamera(initialYaw);

      // Load skeletal animation assets concurrently
      await this.loadAnimations();
    } catch (err) {
      console.error('Failed to spawn Duncan player:', err);
    }
  }

  private async loadAnimations(): Promise<void> {
    const container = this.container;
    if (!container) return;
    try {
      // Gameplay action labels remain provisional; the shared runtime is rig-independent.
      const [idle, run, walk, fly, jump] = await Promise.all(
        ['xh_an000', 'xh_an055', 'xh_an018', 'xh_an024', 'xh_an020']
          .map(id => this.animations.clip('xh_', id))
      );
      if (container !== this.container) return;
      const animator = await this.animations.attach('xh_', container, this.scene);
      if (container !== this.container) { animator?.dispose(); return; }
      this.animator = animator;
      this.idleClip = idle; this.runClip = run; this.walkClip = walk;
      this.flyClip = fly; this.jumpClip = jump;
    } catch (error) { console.warn('Duncan animation unavailable:', error); }
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

      // Subtle body lean into motion
      const targetPitch = this.input.sprint ? 0.14 : 0.06;
      this.pitch += (targetPitch - this.pitch) * Math.min(deltaTime * 8, 1.0);
    } else {
      this.velocity.x *= 0.8;
      this.velocity.z *= 0.8;
      if (Math.abs(this.velocity.x) < 0.01) this.velocity.x = 0;
      if (Math.abs(this.velocity.z) < 0.01) this.velocity.z = 0;

      this.pitch += (0 - this.pitch) * Math.min(deltaTime * 6, 1.0);
    }

    // Mid-air body tilt
    if (!this.isGrounded) {
      this.animNode.rotation.x = 0.2;
    } else {
      this.animNode.rotation.x = this.pitch;
    }

    // Smooth character rotation
    let angleDiff = this.targetRotation - this.currentRotation;
    while (angleDiff < -Math.PI) angleDiff += Math.PI * 2;
    while (angleDiff > Math.PI) angleDiff -= Math.PI * 2;
    this.currentRotation += angleDiff * Math.min(deltaTime * 12, 1.0);
    this.rootNode.rotation.y = this.currentRotation;

    // Ground raycast for snapping and collision
    const rayOrigin = this.rootNode.position.clone();
    rayOrigin.y += 2.0;
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

    // Update skeletal animation
    this.updateSkeletalAnimation(deltaTime, isMoving);
  }

  private updateSkeletalAnimation(deltaTime: number, isMoving: boolean): void {
    if (!this.animator || this.animator.disposed || this.animationInspection) return;

    // Select active animation clip
    let targetClip: AnimationClipData | null = this.idleClip;

    if (!this.isGrounded) {
      if (this.velocity.y > 3.0 && this.jumpClip) {
        targetClip = this.jumpClip;
      } else if (this.flyClip) {
        targetClip = this.flyClip;
      }
    } else if (isMoving) {
      if (this.input.sprint && this.runClip) {
        targetClip = this.runClip;
      } else if (this.walkClip) {
        targetClip = this.walkClip;
      } else if (this.runClip) {
        targetClip = this.runClip;
      }
    } else {
      targetClip = this.idleClip;
    }

    if (!targetClip) return;

    if (this.animator.controller.clip !== targetClip) {
      this.animator.controller.setClip(targetClip, {startFrame: 1, engineTransition: true});
    }
    this.animator.update(deltaTime);
  }

  public showBones(show: boolean): void { this.animator?.showBones(show); }

  public respawn(pos?: Vector3, yaw?: number): void {
    const target = pos || new Vector3(-2.0, 8.5, 14.0);
    this.rootNode.position = target.clone();
    this.velocity = Vector3.Zero();
    this.isGrounded = true;
    this.targetRotation = yaw ?? 0;
    this.currentRotation = yaw ?? 0;
    this.rootNode.rotation.y = this.targetRotation;
    this.snapCamera(yaw);
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
