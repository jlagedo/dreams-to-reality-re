import {
  Scene,
  Vector3,
  Ray,
  AbstractMesh,
  Mesh,
  VertexBuffer,
  TransformNode,
  ArcRotateCamera,
  SceneLoader,
  AssetContainer,
} from '@babylonjs/core';
import { quatSlerp, AnimationClipData } from './viewer';

export interface PlayerInput {
  forward: boolean;
  backward: boolean;
  left: boolean;
  right: boolean;
  sprint: boolean;
  jump: boolean;
}

export interface ModelSkinNode {
  index: number;
  parent: number;
  translation: [number, number, number];
  rotation: number[][];
}

export interface ModelSkinData {
  model: string;
  nodes: ModelSkinNode[];
  primitives: Record<string, [number, number, number, number][]>; // [nodeIndex, lx, ly, lz][]
}

function quatToMatrix(q: [number, number, number, number]): number[][] {
  const [x, y, z, w] = q;
  return [
    [
      Math.round((1.0 - 2.0 * (y * y + z * z)) * 32768),
      Math.round(2.0 * (x * y - z * w) * 32768),
      Math.round(2.0 * (x * z + y * w) * 32768),
    ],
    [
      Math.round(2.0 * (x * y + z * w) * 32768),
      Math.round((1.0 - 2.0 * (x * x + z * z)) * 32768),
      Math.round(2.0 * (y * z - x * w) * 32768),
    ],
    [
      Math.round(2.0 * (x * z - y * w) * 32768),
      Math.round(2.0 * (y * z + x * w) * 32768),
      Math.round((1.0 - 2.0 * (x * x + y * y)) * 32768),
    ],
  ];
}

function matMul(a: number[][], b: number[][]): number[][] {
  const out: number[][] = [
    [0, 0, 0],
    [0, 0, 0],
    [0, 0, 0],
  ];
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      let sum = 0;
      for (let k = 0; k < 3; k++) {
        sum += a[r][k] * b[k][c];
      }
      out[r][c] = sum >> 15;
    }
  }
  return out;
}

function matApply(m: number[][], v: [number, number, number]): [number, number, number] {
  return [
    (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2]) >> 15,
    (m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2]) >> 15,
    (m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]) >> 15,
  ];
}

function sampleClipPose(
  clip: AnimationClipData,
  frame: number
): Record<number, [number, number, number, number]> {
  const pose: Record<number, [number, number, number, number]> = {};
  for (const trk of clip.tracks) {
    if (!trk.keyframes || trk.keyframes.length === 0) {
      pose[trk.nodeIndex] = trk.restRotation;
      continue;
    }
    if (trk.keyframes.length === 1 || frame <= trk.keyframes[0].time) {
      pose[trk.nodeIndex] = trk.keyframes[0].rotation;
      continue;
    }
    if (frame >= trk.keyframes[trk.keyframes.length - 1].time) {
      pose[trk.nodeIndex] = trk.keyframes[trk.keyframes.length - 1].rotation;
      continue;
    }
    for (let k = 0; k < trk.keyframes.length - 1; k++) {
      const k0 = trk.keyframes[k];
      const k1 = trk.keyframes[k + 1];
      if (k0.time <= frame && frame <= k1.time) {
        const dt = k1.time - k0.time;
        const alpha = dt > 0 ? (frame - k0.time) / dt : 0;
        pose[trk.nodeIndex] = quatSlerp(k0.rotation, k1.rotation, alpha);
        break;
      }
    }
  }
  return pose;
}

function evaluateWorldTransforms(
  nodes: ModelSkinNode[],
  pose: Record<number, [number, number, number, number]>
): { rot: number[][]; tr: [number, number, number] }[] {
  const world: { rot: number[][]; tr: [number, number, number] }[] = new Array(nodes.length);
  for (let i = 0; i < nodes.length; i++) {
    const nd = nodes[i];
    const q = pose[i] || [0, 0, 0, 1];
    const rot = quatToMatrix(q);
    const tr = nd.translation;
    if (nd.parent === -1 || nd.parent >= i || !world[nd.parent]) {
      world[i] = { rot, tr: [tr[0], tr[1], tr[2]] };
    } else {
      const parent = world[nd.parent];
      const worldRot = matMul(parent.rot, rot);
      const appliedTr = matApply(parent.rot, tr);
      world[i] = {
        rot: worldRot,
        tr: [appliedTr[0] + parent.tr[0], appliedTr[1] + parent.tr[1], appliedTr[2] + parent.tr[2]],
      };
    }
  }
  return world;
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

  // Skeletal Animation System
  private skinData: ModelSkinData | null = null;
  private idleClip: AnimationClipData | null = null;
  private runClip: AnimationClipData | null = null;
  private walkClip: AnimationClipData | null = null;
  private flyClip: AnimationClipData | null = null;
  private jumpClip: AnimationClipData | null = null;

  private currentAnimState: 'idle' | 'walk' | 'run' | 'jump' | 'fly' = 'idle';
  private animTime: number = 0;
  private animBlend: number = 1.0;
  private prevPose: Record<number, [number, number, number, number]> | null = null;
  private lastPose: Record<number, [number, number, number, number]> | null = null;

  private primitiveMeshes: {
    mesh: Mesh;
    bindings: [number, number, number, number][];
    buffer: Float32Array;
  }[] = [];

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
    this.primitiveMeshes = [];

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
    try {
      const [skinRes, idleRes, runRes, walkRes, flyRes, jumpRes] = await Promise.all([
        fetch('/api/skin/xh'),
        fetch('/api/animation/xh_/xh_an000'), // Idle
        fetch('/api/animation/xh_/xh_an055'), // High speed sprint/run
        fetch('/api/animation/xh_/xh_an018'), // Walk
        fetch('/api/animation/xh_/xh_an024'), // Fly / Levitating
        fetch('/api/animation/xh_/xh_an020'), // Jump leap
      ]);

      if (skinRes.ok) this.skinData = await skinRes.json();
      if (idleRes.ok) this.idleClip = await idleRes.json();
      if (runRes.ok) this.runClip = await runRes.json();
      if (walkRes.ok) this.walkClip = await walkRes.json();
      if (flyRes.ok) this.flyClip = await flyRes.json();
      if (jumpRes.ok) this.jumpClip = await jumpRes.json();

      // Wire up skin bindings to meshes
      if (this.skinData && this.container) {
        for (const [primName, bindings] of Object.entries(this.skinData.primitives)) {
          const mesh = this.container.meshes.find(
            (m) => m instanceof Mesh && m.name.toLowerCase().includes(primName.toLowerCase())
          ) as Mesh | undefined;

          if (mesh) {
            const buffer = new Float32Array(bindings.length * 3);
            this.primitiveMeshes.push({ mesh, bindings, buffer });
          }
        }
      }
    } catch (err) {
      console.warn('Failed to load character animations, falling back to static mesh:', err);
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
    if (!this.skinData || this.primitiveMeshes.length === 0) return;

    // Select active animation clip
    let targetState: 'idle' | 'walk' | 'run' | 'jump' | 'fly' = 'idle';
    let targetClip: AnimationClipData | null = this.idleClip;

    if (!this.isGrounded) {
      if (this.velocity.y > 3.0 && this.jumpClip) {
        targetState = 'jump';
        targetClip = this.jumpClip;
      } else if (this.flyClip) {
        targetState = 'fly';
        targetClip = this.flyClip;
      }
    } else if (isMoving) {
      if (this.input.sprint && this.runClip) {
        targetState = 'run';
        targetClip = this.runClip;
      } else if (this.walkClip) {
        targetState = 'walk';
        targetClip = this.walkClip;
      } else if (this.runClip) {
        targetState = 'run';
        targetClip = this.runClip;
      }
    } else {
      targetState = 'idle';
      targetClip = this.idleClip;
    }

    if (!targetClip) return;

    // State transition cross-fading
    if (targetState !== this.currentAnimState) {
      this.prevPose = this.lastPose ? { ...this.lastPose } : null;
      this.currentAnimState = targetState;
      this.animBlend = 0.0;
    }

    // Advance frame time
    let speedMult = 1.0;
    if (this.currentAnimState === 'run') speedMult = this.input.sprint ? 1.0 : 0.85;
    else if (this.currentAnimState === 'walk') speedMult = 1.1;
    else if (this.currentAnimState === 'fly') speedMult = 1.0;

    this.animTime += deltaTime * targetClip.frameRate * speedMult;
    const frame = this.animTime % targetClip.duration;

    // Sample current target clip pose
    const currentPose = sampleClipPose(targetClip, frame);

    // Blend with previous pose if transitioning
    let finalPose = currentPose;
    if (this.prevPose && this.animBlend < 1.0) {
      this.animBlend = Math.min(1.0, this.animBlend + deltaTime * 8.0);
      finalPose = {};
      for (let i = 0; i < this.skinData.nodes.length; i++) {
        const qPrev = this.prevPose[i] || [0, 0, 0, 1];
        const qCur = currentPose[i] || [0, 0, 0, 1];
        finalPose[i] = quatSlerp(qPrev, qCur, this.animBlend);
      }
    }
    this.lastPose = finalPose;

    // Evaluate forward kinematics down the 27 skeletal nodes
    const worldTransforms = evaluateWorldTransforms(this.skinData.nodes, finalPose);

    // Apply deformed vertex coordinates directly to Babylon.js meshes
    for (const { mesh, bindings, buffer } of this.primitiveMeshes) {
      for (let j = 0; j < bindings.length; j++) {
        const [nodeIdx, lx, ly, lz] = bindings[j];
        const { rot, tr } = worldTransforms[nodeIdx];

        // Integer matrix multiply and translation compose (sar 15)
        const wx = ((rot[0][0] * lx + rot[0][1] * ly + rot[0][2] * lz) >> 15) + tr[0];
        const wy = ((rot[1][0] * lx + rot[1][1] * ly + rot[1][2] * lz) >> 15) + tr[1];
        const wz = ((rot[2][0] * lx + rot[2][1] * ly + rot[2][2] * lz) >> 15) + tr[2];

        // Convert Cryo scene units to glTF/Babylon coordinates
        buffer[j * 3 + 0] = wx * 0.01;
        buffer[j * 3 + 1] = -wy * 0.01;
        buffer[j * 3 + 2] = wz * 0.01;
      }

      mesh.setVerticesData(VertexBuffer.PositionKind, buffer, false);
    }
  }

  public respawn(pos?: Vector3): void {
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
