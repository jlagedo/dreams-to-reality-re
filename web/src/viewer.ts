import {
  Engine,
  Scene,
  Vector3,
  Color3,
  Color4,
  HemisphericLight,
  DirectionalLight,
  ArcRotateCamera,
  UniversalCamera,
  SceneLoader,
  AbstractMesh,
  Mesh,
  MeshBuilder,
  Texture,
  StandardMaterial,
  PBRMaterial,
  KeyboardEventTypes,
  AssetContainer,
  TransformNode,
  LinesMesh,
  DefaultRenderingPipeline,
  PostProcess,
  Effect,
  Camera,
} from '@babylonjs/core';
import '@babylonjs/loaders/glTF';
import { DuncanPlayer } from './player';
import { AudioManager } from './audio';
import { AnimationLibrary } from './animation/library';
import { detachContainerRoots, SkeletalAnimator } from './animation/renderer';
import { sampleClipPose } from './animation/controller';
import type { PlaybackState } from './animation/controller';
import type { AnimationClipData, RootMode } from './animation/types';
export type { AnimationClipData } from './animation/types';

export type CameraMode = 'orbit' | 'walk' | 'duncan';

Effect.ShadersStore['dreamsCrtFragmentShader'] = `
precision highp float;

varying vec2 vUV;
uniform sampler2D textureSampler;
uniform vec2 screenSize;
uniform vec2 virtualResolution;
uniform float scanlineStrength;
uniform float maskStrength;
uniform float curvature;
uniform float glowStrength;
uniform float noiseStrength;
uniform float vignetteStrength;
uniform float overscan;
uniform float aspectLock;
uniform float time;

float random(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233)) + time * 0.001) * 43758.5453);
}

void main(void) {
  vec2 contentScale = vec2(1.0);
  if (aspectLock > 0.5) {
    float screenAspect = screenSize.x / max(screenSize.y, 1.0);
    float targetAspect = 4.0 / 3.0;
    if (screenAspect > targetAspect) {
      contentScale.x = targetAspect / screenAspect;
    } else {
      contentScale.y = screenAspect / targetAspect;
    }
  }

  vec2 localUV = (vUV - 0.5) / contentScale + 0.5;
  if (localUV.x < 0.0 || localUV.x > 1.0 || localUV.y < 0.0 || localUV.y > 1.0) {
    gl_FragColor = vec4(0.003, 0.005, 0.007, 1.0);
    return;
  }

  vec2 centered = localUV * 2.0 - 1.0;
  centered *= 1.0 + curvature * vec2(centered.y * centered.y, centered.x * centered.x);
  centered *= 1.0 - overscan;
  localUV = centered * 0.5 + 0.5;

  if (localUV.x < 0.0 || localUV.x > 1.0 || localUV.y < 0.0 || localUV.y > 1.0) {
    gl_FragColor = vec4(0.003, 0.005, 0.007, 1.0);
    return;
  }

  vec2 snappedUV = (floor(localUV * virtualResolution) + 0.5) / virtualResolution;
  vec2 sampleUV = (snappedUV - 0.5) * contentScale + 0.5;
  vec2 pixelStep = vec2(contentScale.x / virtualResolution.x, 0.0);

  vec3 color = texture2D(textureSampler, sampleUV).rgb;
  vec3 horizontalGlow = (
    texture2D(textureSampler, sampleUV - pixelStep).rgb +
    texture2D(textureSampler, sampleUV + pixelStep).rgb
  ) * 0.5;
  color = mix(color, horizontalGlow, glowStrength);

  float scanline = 0.5 + 0.5 * cos(fract(localUV.y * virtualResolution.y) * 6.2831853);
  color *= 1.0 - scanlineStrength * scanline;

  float maskCell = mod(floor(gl_FragCoord.x), 3.0);
  vec3 phosphor = maskCell < 1.0
    ? vec3(1.12, 0.94, 0.94)
    : (maskCell < 2.0 ? vec3(0.94, 1.12, 0.94) : vec3(0.94, 0.94, 1.12));
  color *= mix(vec3(1.0), phosphor, maskStrength);

  vec2 vignetteUV = localUV * (1.0 - localUV.yx);
  float vignette = pow(clamp(vignetteUV.x * vignetteUV.y * 16.0, 0.0, 1.0), 0.22);
  color *= mix(1.0, vignette, vignetteStrength);

  float noise = random(gl_FragCoord.xy) - 0.5;
  color += noise * noiseStrength;

  gl_FragColor = vec4(max(color, 0.0), 1.0);
}
`;

export interface GraphicsSettings {
  renderingMode: 'default' | 'crt';
  renderScale: number;
  msaaSamples: number;
  fxaaEnabled: boolean;
  anisotropy: number;
  textureFilter: 'nearest' | 'linear';
  toneMappingEnabled: boolean;
  exposure: number;
  contrast: number;
  sharpen: number;
  crtResolution: 640 | 800;
  crtStrength: number;
  crtScanlines: number;
  crtMask: number;
  crtCurvature: number;
  crtGlow: number;
  crtNoise: number;
  crtVignette: number;
  crtOverscan: number;
  crtAspectLock: boolean;
}

export const DEFAULT_GRAPHICS_SETTINGS: GraphicsSettings = {
  renderingMode: 'default',
  renderScale: 1,
  msaaSamples: 1,
  fxaaEnabled: false,
  anisotropy: 8,
  textureFilter: 'nearest',
  toneMappingEnabled: false,
  exposure: 1,
  contrast: 1,
  sharpen: 0,
  crtResolution: 640,
  crtStrength: 1,
  crtScanlines: 0.1,
  crtMask: 0.05,
  crtCurvature: 0.025,
  crtGlow: 0.08,
  crtNoise: 0.01,
  crtVignette: 0.1,
  crtOverscan: 0.015,
  crtAspectLock: true,
};

export interface ProjectObject {
  name: string;
  asset: string;
  assetStem: string;
  type: number;
  category: 'scene' | 'npc' | 'creature' | 'prop';
  position: [number, number, number];
  rawYaw: number;
  yaw: number;
  flags?: number;
  isActive?: boolean;
  isCharacter?: boolean;
  behaviorType?: number;
  speed?: number;
  routeIndex?: number;
  health?: number;
  radius?: number;
}

export interface ProjectLink {
  name: string;
  destination: string;
  destProject: number | null;
  destSceneStem?: string;
  destSceneName?: string;
  min: [number, number, number];
  max: [number, number, number];
  center: [number, number, number];
  size: [number, number, number];
}

export interface ProjectBox {
  name: string;
  kind: number;
  points: [number, number, number][];
}

export interface ProjectData {
  index: number;
  name: string;
  scene: string;
  sceneStem: string;
  spawnPosition?: [number, number, number];
  spawnRawHeading?: number;
  spawnHeadingDeg?: number;
  spawnYaw?: number;
  animVideo?: string;
  animMaterial?: string;
  animVideo2?: string;
  animMaterial2?: string;
  ambientRgb?: [number, number, number];
  dirLight1?: [number, number, number];
  dirLight2?: [number, number, number];
  fog?: [number, number, number, number];
  skyRgb?: [number, number, number, number];
  lightingMode?: number;
  cameraFov?: number;
  cdTrack?: number;
  objects: ProjectObject[];
  links: ProjectLink[];
  boxes: ProjectBox[];
}

export class DreamsViewer {
  public engine: Engine;
  public scene: Scene;
  public orbitCamera: ArcRotateCamera;
  public walkCamera: UniversalCamera;
  public hemiLight!: HemisphericLight;
  public currentMode: CameraMode = 'orbit';
  public retroFilterEnabled: boolean = true;

  private graphicsPipeline: DefaultRenderingPipeline;
  private graphicsSettings: GraphicsSettings = { ...DEFAULT_GRAPHICS_SETTINGS };
  private crtPostProcesses: { camera: Camera; postProcess: PostProcess }[] = [];

  public player: DuncanPlayer;
  public audio: AudioManager;
  public readonly animations = new AnimationLibrary();
  public currentAssetCategory: 'scenes' | 'models' = 'scenes';
  private npcAnimators = new Map<string, SkeletalAnimator>();
  private npcAnimationErrors = new Map<string, string>();
  private modelAnimator: SkeletalAnimator | null = null;
  private modelAnimationError: string | null = null;
  private loadGeneration = 0;
  private inspectionRequest = 0;
  private inspectionTarget: SkeletalAnimator | null = null;
  private inspectionSaved: PlaybackState | null = null;
  private inspectionPreview: PlaybackState | null = null;
  private inspectionBlendRequest = 0;

  private currentContainer: AssetContainer | null = null;
  private currentAssetMeshes: AbstractMesh[] = [];
  public currentSceneId: string = '';

  private spawnedNpcContainers: AssetContainer[] = [];
  private spawnedNpcRoots: TransformNode[] = [];
  private waypointLines: LinesMesh[] = [];
  private portalMeshes: Mesh[] = [];
  private activePortalLinks: { mesh: Mesh; link: ProjectLink }[] = [];
  public currentProjectData: ProjectData | null = null;

  // Entity selection and debug overlay states
  public selectedEntity: ProjectObject | null = null;
  private entityHighlightBox: Mesh | null = null;
  private entityBoundingBoxes: Mesh[] = [];
  public showEntityBounds: boolean = true;
  public showPortals: boolean = true;
  public showWaypoints: boolean = true;

  // Animation player state
  public activeClip: AnimationClipData | null = null;
  public isPlayingAnimation: boolean = false;
  private animationInspection = false;
  public currentAnimFrame: number = 0;
  public animSpeed: number = 1.0;

  private portalPulseTime: number = 0;
  private portalCooldown: boolean = false;

  private onStatusChange?: (msg: string) => void;
  private onStatsChange?: (meshCount: number) => void;
  private onPortalTransitionNotify?: (targetScene: string) => void;
  private onSceneChange?: (sceneId: string) => void;
  public onEntitySelected?: (entity: ProjectObject | null) => void;
  public onAnimFrameUpdate?: (frame: number, maxFrame: number, pose: Record<number, number[]>) => void;
  public onProjectDataLoaded?: (project: ProjectData) => void;

  constructor(
    canvas: HTMLCanvasElement,
    callbacks?: {
      onStatusChange?: (msg: string) => void;
      onStatsChange?: (meshCount: number) => void;
      onPortalTransitionNotify?: (targetScene: string) => void;
      onSceneChange?: (sceneId: string) => void;
      onEntitySelected?: (entity: ProjectObject | null) => void;
      onAnimFrameUpdate?: (frame: number, maxFrame: number, pose: Record<number, number[]>) => void;
      onProjectDataLoaded?: (project: ProjectData) => void;
    }
  ) {
    this.onStatusChange = callbacks?.onStatusChange;
    this.onStatsChange = callbacks?.onStatsChange;
    this.onPortalTransitionNotify = callbacks?.onPortalTransitionNotify;
    this.onSceneChange = callbacks?.onSceneChange;
    this.onEntitySelected = callbacks?.onEntitySelected;
    this.onAnimFrameUpdate = callbacks?.onAnimFrameUpdate;
    this.onProjectDataLoaded = callbacks?.onProjectDataLoaded;

    this.audio = new AudioManager();

    this.engine = new Engine(
      canvas,
      true,
      {
        preserveDrawingBuffer: true,
        stencil: true,
      },
      true
    );

    this.scene = new Scene(this.engine);
    this.scene.clearColor = new Color4(0.04, 0.06, 0.08, 1.0);

    // Setup orbit camera
    this.orbitCamera = new ArcRotateCamera(
      'orbitCam',
      -Math.PI / 2,
      Math.PI / 3,
      100,
      Vector3.Zero(),
      this.scene
    );
    this.orbitCamera.wheelPrecision = 15;
    this.orbitCamera.minZ = 0.5;
    this.orbitCamera.maxZ = 50000;
    this.orbitCamera.attachControl(canvas, true);

    // Setup free-fly walk camera
    this.walkCamera = new UniversalCamera(
      'walkCam',
      new Vector3(0, 10, -50),
      this.scene
    );
    this.walkCamera.minZ = 0.5;
    this.walkCamera.maxZ = 50000;
    this.walkCamera.speed = 4;
    this.walkCamera.keysUp = [87, 38];
    this.walkCamera.keysDown = [83, 40];
    this.walkCamera.keysLeft = [65, 37];
    this.walkCamera.keysRight = [68, 39];

    this.scene.activeCamera = this.orbitCamera;

    // Lighting
    this.hemiLight = new HemisphericLight('hemiLight', new Vector3(0, 1, 0), this.scene);
    this.hemiLight.intensity = 0.9;
    this.hemiLight.groundColor = new Color3(0.2, 0.2, 0.25);

    const dirLight = new DirectionalLight('dirLight', new Vector3(-1, -2, -1), this.scene);
    dirLight.position = new Vector3(20, 40, 20);
    dirLight.intensity = 0.65;

    // Instantiate Duncan player
    this.player = new DuncanPlayer(this.scene, this.orbitCamera, this.animations);

    this.graphicsPipeline = new DefaultRenderingPipeline(
      'graphicsPipeline',
      true,
      this.scene,
      [this.orbitCamera, this.walkCamera]
    );
    this.createCrtPostProcesses([this.orbitCamera, this.walkCamera]);
    this.applyGraphicsSettings(DEFAULT_GRAPHICS_SETTINGS);

    window.addEventListener('resize', () => this.engine.resize());

    // Keyboard shortcut 'I' to toggle Babylon Inspector
    this.scene.onKeyboardObservable.add((kbInfo) => {
      if (
        kbInfo.type === KeyboardEventTypes.KEYDOWN &&
        (kbInfo.event.key === 'i' || kbInfo.event.key === 'I')
      ) {
        if (
          document.activeElement?.tagName !== 'INPUT' &&
          document.activeElement?.tagName !== 'SELECT'
        ) {
          this.toggleInspector();
        }
      }
    });

    // Pointer pick for entities
    this.scene.onPointerDown = (_evt, pickResult) => {
      if (pickResult && pickResult.hit && pickResult.pickedMesh) {
        const mName = pickResult.pickedMesh.name;
        if (mName.startsWith('bbox_') && this.currentProjectData) {
          const objName = mName.replace('bbox_', '');
          const obj = this.currentProjectData.objects.find((o) => o.name === objName);
          if (obj) {
            this.selectEntity(obj);
          }
        }
      }
    };

    // Render loop
    this.engine.runRenderLoop(() => {
      const dt = this.engine.getDeltaTime() / 1000;

      if (this.currentMode === 'duncan') {
        this.player.update(dt);
        this.checkPortalCollisions();
      }

      // Each actor owns a clock and pose. Inspection temporarily owns just its target.
      for (const animator of this.npcAnimators.values()) {
        if (!(this.animationInspection && animator === this.inspectionTarget)) animator.update(dt);
      }
      if (this.modelAnimator && !(this.animationInspection && this.modelAnimator === this.inspectionTarget))
        this.modelAnimator.update(dt);
      if (this.animationInspection && this.inspectionTarget && this.activeClip && this.isPlayingAnimation) {
        const controller = this.inspectionTarget.controller;
        controller.speed = this.animSpeed;
        controller.playing = true;
        this.inspectionTarget.update(dt);
        this.isPlayingAnimation = controller.playing;
        this.currentAnimFrame = controller.frame;
        this.onAnimFrameUpdate?.(Math.floor(controller.frame), this.activeClip.duration, controller.pose());
      }

      // Highlight selected entity with pulse
      if (this.entityHighlightBox && this.entityHighlightBox.isEnabled()) {
        const pulse = 1.0 + Math.sin(this.portalPulseTime * 2.0) * 0.08;
        this.entityHighlightBox.scaling.set(pulse, pulse, pulse);
      }

      // Pulse portal link meshes
      if (this.portalMeshes.length > 0) {
        this.portalPulseTime += dt * 3.5;
        const s = 1.0 + Math.sin(this.portalPulseTime) * 0.12;
        for (const pm of this.portalMeshes) {
          pm.scaling.set(s, s, s);
        }
      }

      this.scene.render();
    });
  }

  public setCameraMode(mode: CameraMode, canvas: HTMLCanvasElement): void {
    this.currentMode = mode;
    if (this.inspectionTarget === this.player.animator && mode !== 'duncan') this.releaseInspection();

    if (mode === 'orbit') {
      this.orbitCamera.lowerRadiusLimit = null;
      this.orbitCamera.upperRadiusLimit = null;
      this.walkCamera.detachControl();
      this.orbitCamera.attachControl(canvas, true);
      this.scene.activeCamera = this.orbitCamera;
      this.player.rootNode.setEnabled(false);
    } else if (mode === 'walk') {
      this.orbitCamera.lowerRadiusLimit = null;
      this.orbitCamera.upperRadiusLimit = null;
      this.orbitCamera.detachControl();
      this.walkCamera.position = this.orbitCamera.target.clone().add(new Vector3(0, 5, -15));
      this.walkCamera.setTarget(this.orbitCamera.target);
      this.walkCamera.attachControl(canvas, true);
      this.scene.activeCamera = this.walkCamera;
      this.player.rootNode.setEnabled(false);
    } else if (mode === 'duncan') {
      this.walkCamera.detachControl();
      this.orbitCamera.attachControl(canvas, true);
      this.scene.activeCamera = this.orbitCamera;
      this.player.rootNode.setEnabled(true);

      // If not yet spawned, spawn Duncan; otherwise immediately snap camera
      if (!this.player.isSpawned) {
        const spawnPos = this.getDefaultSpawnPos(this.currentSceneId);
        const spawnYaw = this.currentProjectData ? this.currentProjectData.spawnYaw : undefined;
        this.player.spawn(spawnPos, this.currentAssetMeshes, spawnYaw);
      } else {
        const spawnYaw = this.currentProjectData ? this.currentProjectData.spawnYaw : undefined;
        this.player.snapCamera(spawnYaw);
      }
    }
  }

  public getDefaultSpawnPos(sceneId: string): Vector3 {
    if (this.currentProjectData && this.currentProjectData.spawnPosition) {
      const sp = this.currentProjectData.spawnPosition;
      return new Vector3(sp[0], sp[1], sp[2]);
    }
    if (sceneId === 'h18angkr') {
      return new Vector3(-3.19, 6.25, -31.87);
    } else if (sceneId === 'f08_gpic') {
      return new Vector3(0.0, 1.0, 0.0);
    } else if (sceneId === 'e13_angk') {
      return new Vector3(0.0, 5.0, 0.0);
    }
    return new Vector3(0.0, 5.0, 0.0);
  }

  public resetCamera(): void {
    if (this.currentMode === 'duncan' && this.player.isSpawned) {
      this.player.respawn(this.getDefaultSpawnPos(this.currentSceneId));
      return;
    }

    if (this.currentAssetMeshes.length === 0) return;

    let min = new Vector3(Number.MAX_VALUE, Number.MAX_VALUE, Number.MAX_VALUE);
    let max = new Vector3(-Number.MAX_VALUE, -Number.MAX_VALUE, -Number.MAX_VALUE);
    let validMeshCount = 0;

    for (const mesh of this.currentAssetMeshes) {
      if (mesh.getTotalVertices() === 0 || mesh.name.startsWith('portal_') || mesh.name.startsWith('box_')) continue;
      mesh.computeWorldMatrix(true);
      const b = mesh.getBoundingInfo().boundingBox;
      min = Vector3.Minimize(min, b.minimumWorld);
      max = Vector3.Maximize(max, b.maximumWorld);
      validMeshCount++;
    }

    if (validMeshCount === 0 || !isFinite(min.x) || !isFinite(max.x)) {
      this.orbitCamera.target = Vector3.Zero();
      this.orbitCamera.radius = 30;
      return;
    }

    const center = min.add(max).scale(0.5);
    const size = max.subtract(min).length();

    this.orbitCamera.target = center;
    this.orbitCamera.radius = Math.max(size * 1.2, 10);
    this.orbitCamera.alpha = -Math.PI / 2;
    this.orbitCamera.beta = Math.PI / 3;

    this.orbitCamera.maxZ = Math.max(size * 10, 50000);
    this.walkCamera.maxZ = Math.max(size * 10, 50000);

    if (this.currentMode === 'walk') {
      this.walkCamera.position = center.clone().add(new Vector3(0, Math.max(size * 0.1, 5), -Math.max(size * 0.3, 20)));
      this.walkCamera.setTarget(center);
      this.walkCamera.speed = Math.max(size * 0.05, 2);
    }
  }

  public async loadAsset(category: 'scenes' | 'models', filename: string, preserveInspection = false): Promise<void> {
    const generation = ++this.loadGeneration;
    if (!preserveInspection) { ++this.inspectionRequest; this.activeClip = null; this.isPlayingAnimation = false; }
    this.currentAssetCategory = category;
    this.onStatusChange?.(`Loading ${filename}...`);
    this.currentSceneId = filename.replace('.gltf', '').toLowerCase();

    // Clean up previous loaded container, spawned NPCs, portals, and waypoints
    this.clearSceneEntities();

    if (this.currentContainer) {
      this.currentContainer.removeAllFromScene();
      this.currentContainer.dispose();
      this.currentContainer = null;
    }
    this.currentAssetMeshes = [];

    const rootUrl = `/api/assets/${category}/`;

    try {
      const container = await SceneLoader.LoadAssetContainerAsync(rootUrl, filename, this.scene);
      if (generation !== this.loadGeneration) { container.dispose(); return; }
      container.addAllToScene();

      this.currentContainer = container;
      this.currentAssetMeshes = container.meshes;

      // Adjust materials for 1997 game geometry
      for (const mat of container.materials) {
        mat.backFaceCulling = false;
        if (mat instanceof StandardMaterial) {
          mat.specularColor = new Color3(0.1, 0.1, 0.1);
        } else if (mat instanceof PBRMaterial) {
          mat.roughness = 0.9;
          mat.metallic = 0.0;
        }
      }

      for (const mesh of container.meshes) {
        if (mesh.getTotalVertices() > 0 && !mesh.material) {
          const defMat = new StandardMaterial('defMat_' + mesh.name, this.scene);
          defMat.diffuseColor = new Color3(0.7, 0.72, 0.75);
          defMat.backFaceCulling = false;
          mesh.material = defMat;
        }
      }

      this.applyTextureFiltering();

      // If loading a scene, spawn its project entities (NPCs, portals, waypoints)
      if (category === 'scenes') {
        await this.spawnProjectEntities(this.currentSceneId, generation);
        if (generation !== this.loadGeneration) return;
        this.onSceneChange?.(this.currentSceneId);
      }

      if (category === 'models') {
        try {
          const animator = await this.animations.attach(this.currentSceneId, container, this.scene);
          if (generation !== this.loadGeneration) { animator?.dispose(); return; }
          this.modelAnimator = animator;
        } catch (error) {
          this.modelAnimationError = String(error);
          this.onStatusChange?.(`Animation unavailable: ${error}`);
        }
      }

      // If Duncan is active, update his ground collider meshes
      if (this.player.isSpawned) {
        this.player.updateGroundMeshes(this.currentAssetMeshes);
      }

      this.resetCamera();

      const totalVerts = this.currentAssetMeshes.reduce((acc, m) => acc + m.getTotalVertices(), 0);
      this.onStatusChange?.(`Loaded ${filename} (${totalVerts.toLocaleString()} vertices)`);
      this.onStatsChange?.(this.currentAssetMeshes.length);
    } catch (err) {
      console.error('Failed to load glTF asset:', err);
      this.onStatusChange?.(`Error loading ${filename}: ${err}`);
    }
  }

  private clearSceneEntities(): void {
    this.releaseInspection();
    this.modelAnimator?.dispose(); this.modelAnimator = null;
    this.modelAnimationError = null;
    for (const animator of this.npcAnimators.values()) animator.dispose();
    this.npcAnimators.clear();
    this.npcAnimationErrors.clear();
    // Dispose entity bounding boxes
    for (const bbox of this.entityBoundingBoxes) {
      bbox.dispose();
    }
    this.entityBoundingBoxes = [];
    if (this.entityHighlightBox) {
      this.entityHighlightBox.setEnabled(false);
    }
    this.selectedEntity = null;
    this.onEntitySelected?.(null);

    // Dispose spawned NPC containers
    for (const c of this.spawnedNpcContainers) {
      detachContainerRoots(c);
      c.removeAllFromScene();
      c.dispose();
    }
    this.spawnedNpcContainers = [];

    // Dispose root transform nodes
    for (const r of this.spawnedNpcRoots) {
      r.dispose();
    }
    this.spawnedNpcRoots = [];

    // Dispose waypoint line meshes
    for (const line of this.waypointLines) {
      line.dispose();
    }
    this.waypointLines = [];

    // Dispose portal meshes
    for (const pm of this.portalMeshes) {
      pm.dispose();
    }
    this.portalMeshes = [];
    this.activePortalLinks = [];
    this.currentProjectData = null;
  }

  private async spawnProjectEntities(sceneStem: string, generation: number): Promise<void> {
    try {
      const res = await fetch(`/api/project/scene/${sceneStem}`);
      if (!res.ok) return;

      const project: ProjectData | null = await res.json();
      if (!project || generation !== this.loadGeneration) return;

      this.currentProjectData = project;
      console.log(`[Dreams] Spawning entities for ${project.name} (${sceneStem}):`, project);
      this.onProjectDataLoaded?.(project);

      // Apply project ambient lighting if available
      if (project.ambientRgb && project.ambientRgb.length === 3) {
        const [r, g, b] = project.ambientRgb;
        if (r > 0 || g > 0 || b > 0) {
          this.hemiLight.groundColor = new Color3(r / 255.0, g / 255.0, b / 255.0);
        }
      }

      // 1. Spawn Scene Objects (NPCs, Creatures, Props)
      for (const obj of project.objects) {
        if (obj.name === 'OBJET0') continue; // OBJET0 is the scene terrain itself
        if (obj.isActive === false) continue; // Skip dormant objects

        try {
          const modelUrl = `/api/assets/models/`;
          const modelFile = `${obj.assetStem}.gltf`;
          const container = await SceneLoader.LoadAssetContainerAsync(modelUrl, modelFile, this.scene);
          if (generation !== this.loadGeneration) { container.dispose(); return; }
          container.addAllToScene();

          const rootNode = new TransformNode(`entity_${obj.name}_${obj.assetStem}`, this.scene);
          for (const mesh of container.meshes) {
            if (!mesh.parent) {
              mesh.parent = rootNode;
            }
            mesh.isPickable = false; // Don't interfere with camera or player ground raycast
          }

          rootNode.position.set(obj.position[0], obj.position[1], obj.position[2]);
          rootNode.rotation.y = obj.yaw;

          for (const mat of container.materials) {
            mat.backFaceCulling = false;
          }

          // Add 3D debug wireframe bounding box marker
          const bbox = MeshBuilder.CreateBox(`bbox_${obj.name}`, { size: 2.2 }, this.scene);
          bbox.position.set(obj.position[0], obj.position[1] + 1.1, obj.position[2]);
          const bmat = new StandardMaterial(`bboxMat_${obj.name}`, this.scene);
          bmat.wireframe = true;
          bmat.emissiveColor = obj.category === 'npc'
            ? new Color3(0.2, 0.8, 1.0)
            : obj.category === 'creature'
            ? new Color3(1.0, 0.75, 0.2)
            : new Color3(0.7, 0.4, 1.0);
          bbox.material = bmat;
          bbox.isPickable = true;
          bbox.setEnabled(this.showEntityBounds);
          this.entityBoundingBoxes.push(bbox);

          this.spawnedNpcContainers.push(container);
          this.spawnedNpcRoots.push(rootNode);
          try {
            const animator = await this.animations.attach(obj.assetStem, container, this.scene);
            if (generation !== this.loadGeneration) { animator?.dispose(); return; }
            if (animator) this.npcAnimators.set(obj.name, animator);
          } catch (error) {
            this.npcAnimationErrors.set(obj.name, String(error));
            console.warn(`Animation unavailable for ${obj.name}:`, error);
          }

        } catch (e) {
          console.warn(`Could not spawn object ${obj.name} (${obj.asset}):`, e);
        }
      }

      // 2. Spawn Portal Links (LINK0 .. LINK7)
      for (let i = 0; i < project.links.length; i++) {
        const link = project.links[i];
        const center = new Vector3(link.center[0], link.center[1], link.center[2]);

        // Distinct colors for portals: Link 0 = Cyan, Link 1 = Magenta, Link 2+ = Amber
        const color = i === 0
          ? new Color3(0.1, 0.9, 1.0)
          : i === 1
          ? new Color3(1.0, 0.35, 0.85)
          : new Color3(1.0, 0.85, 0.2);

        // Size scaled to trigger bounding box, clamped to comfortable bubble size
        const radius = Math.max(1.8, Math.min(Math.max(...link.size), 4.5));

        const portalMesh = MeshBuilder.CreateSphere(
          `portal_${link.name}`,
          { diameter: radius, segments: 16 },
          this.scene
        );
        portalMesh.position = center.clone();

        const mat = new StandardMaterial(`portalMat_${link.name}`, this.scene);
        mat.emissiveColor = color;
        mat.diffuseColor = color;
        mat.alpha = 0.68;
        mat.backFaceCulling = false;
        portalMesh.material = mat;
        portalMesh.isPickable = false;

        this.portalMeshes.push(portalMesh);
        this.activePortalLinks.push({ mesh: portalMesh, link });
      }

      // 3. Spawn Waypoint & Patrol Paths (BOX0 .. BOX11)
      for (const box of project.boxes) {
        if (!box.points || box.points.length < 2) continue;

        const points = box.points.map((p) => new Vector3(p[0], p[1], p[2]));
        const lineColor = box.kind === 0
          ? new Color4(0.2, 0.85, 1.0, 0.6)  // Cyan patrol path
          : box.kind === 1
          ? new Color4(1.0, 0.75, 0.2, 0.6)  // Gold flight route
          : new Color4(0.5, 1.0, 0.5, 0.6);  // Green trigger zone

        const linesMesh = MeshBuilder.CreateLines(
          `box_${box.name}`,
          { points, colors: Array(points.length).fill(lineColor) },
          this.scene
        );
        linesMesh.isPickable = false;
        this.waypointLines.push(linesMesh);
      }

      this.applyTextureFiltering();
    } catch (err) {
      console.error('Error spawning project entities:', err);
    }
  }

  private async checkPortalCollisions(): Promise<void> {
    if (this.portalCooldown || this.activePortalLinks.length === 0) return;

    const playerPos = this.player.rootNode.position;

    for (const { mesh, link } of this.activePortalLinks) {
      const dist = Vector3.Distance(playerPos, mesh.position);

      // Trigger if player is close to portal center OR inside its bounding volume with a margin
      const inBounds =
        playerPos.x >= link.min[0] - 1.5 &&
        playerPos.x <= link.max[0] + 1.5 &&
        playerPos.y >= link.min[1] - 2.0 &&
        playerPos.y <= link.max[1] + 2.0 &&
        playerPos.z >= link.min[2] - 1.5 &&
        playerPos.z <= link.max[2] + 1.5;

      if (dist < 3.2 || inBounds) {
        await this.handlePortalEntry(link);
        break;
      }
    }
  }

  private async handlePortalEntry(link: ProjectLink): Promise<void> {
    if (this.portalCooldown) return;
    this.portalCooldown = true;

    this.audio.playPortalSfx();

    const targetSceneStem = link.destSceneStem;
    const destLabel = link.destSceneName || link.destination;

    this.onPortalTransitionNotify?.(`Entering ${link.name} -> ${destLabel}`);
    this.onStatusChange?.(`Entering ${link.name} -> ${destLabel}...`);

    if (targetSceneStem) {
      await this.loadAsset('scenes', `${targetSceneStem}.gltf`);
      const targetSpawn = this.getDefaultSpawnPos(targetSceneStem);
      this.player.respawn(targetSpawn);
    }

    setTimeout(() => {
      this.portalCooldown = false;
    }, 2800);
  }

  public toggleRetroFilter(): boolean {
    return this.setRetroFilter(!this.retroFilterEnabled);
  }

  public setRetroFilter(enabled: boolean): boolean {
    this.retroFilterEnabled = enabled;
    this.graphicsSettings.textureFilter = enabled ? 'nearest' : 'linear';
    this.applyTextureFiltering();
    return this.retroFilterEnabled;
  }

  private applyTextureFiltering(): void {
    const samplingMode = this.retroFilterEnabled
      ? Texture.NEAREST_SAMPLINGMODE
      : Texture.TRILINEAR_SAMPLINGMODE;

    for (const tex of this.scene.textures) {
      if (tex instanceof Texture) {
        tex.updateSamplingMode(samplingMode);
        tex.anisotropicFilteringLevel = this.retroFilterEnabled
          ? 1
          : this.graphicsSettings.anisotropy;
      }
    }
  }

  public getGraphicsSettings(): GraphicsSettings {
    return { ...this.graphicsSettings };
  }

  public applyGraphicsSettings(settings: Partial<GraphicsSettings>): GraphicsSettings {
    this.graphicsSettings = { ...this.graphicsSettings, ...settings };
    this.retroFilterEnabled = this.graphicsSettings.textureFilter === 'nearest';

    const deviceRatio = window.devicePixelRatio || 1;
    const renderScale = Math.min(1.5, Math.max(0.5, this.graphicsSettings.renderScale));
    this.graphicsSettings.renderScale = renderScale;
    this.engine.setHardwareScalingLevel(1 / (deviceRatio * renderScale));

    this.graphicsPipeline.samples = this.graphicsSettings.msaaSamples;
    this.graphicsPipeline.fxaaEnabled = this.graphicsSettings.fxaaEnabled;
    this.graphicsPipeline.sharpen.edgeAmount = this.graphicsSettings.sharpen;
    this.graphicsPipeline.sharpenEnabled = this.graphicsSettings.sharpen > 0;

    const imageProcessing = this.scene.imageProcessingConfiguration;
    imageProcessing.toneMappingEnabled = this.graphicsSettings.toneMappingEnabled;
    imageProcessing.exposure = this.graphicsSettings.exposure;
    imageProcessing.contrast = this.graphicsSettings.contrast;

    this.applyCrtRenderingMode();
    this.applyTextureFiltering();
    this.engine.resize();
    return this.getGraphicsSettings();
  }

  public resetGraphicsSettings(): GraphicsSettings {
    return this.applyGraphicsSettings(DEFAULT_GRAPHICS_SETTINGS);
  }

  private createCrtPostProcesses(cameras: Camera[]): void {
    const uniforms = [
      'screenSize',
      'virtualResolution',
      'scanlineStrength',
      'maskStrength',
      'curvature',
      'glowStrength',
      'noiseStrength',
      'vignetteStrength',
      'overscan',
      'aspectLock',
      'time',
    ];

    for (const camera of cameras) {
      const postProcess = new PostProcess(
        `crt_${camera.name}`,
        'dreamsCrt',
        uniforms,
        null,
        1,
        null,
        Texture.BILINEAR_SAMPLINGMODE,
        this.engine,
        false
      );
      postProcess.onApply = (effect) => {
        const settings = this.graphicsSettings;
        const strength = settings.crtStrength;
        const virtualWidth = settings.crtResolution;
        effect.setFloat2('screenSize', this.engine.getRenderWidth(), this.engine.getRenderHeight());
        effect.setFloat2('virtualResolution', virtualWidth, virtualWidth * 0.75);
        effect.setFloat('scanlineStrength', settings.crtScanlines * strength);
        effect.setFloat('maskStrength', settings.crtMask * strength);
        effect.setFloat('curvature', settings.crtCurvature * strength);
        effect.setFloat('glowStrength', settings.crtGlow * strength);
        effect.setFloat('noiseStrength', settings.crtNoise * strength);
        effect.setFloat('vignetteStrength', settings.crtVignette * strength);
        effect.setFloat('overscan', settings.crtOverscan * strength);
        effect.setFloat('aspectLock', settings.crtAspectLock ? 1 : 0);
        effect.setFloat('time', performance.now());
      };
      this.crtPostProcesses.push({ camera, postProcess });
    }
  }

  private applyCrtRenderingMode(): void {
    const enabled = this.graphicsSettings.renderingMode === 'crt';
    for (const { camera, postProcess } of this.crtPostProcesses) {
      camera.detachPostProcess(postProcess);
      if (enabled) camera.attachPostProcess(postProcess);
    }
  }

  public async toggleInspector(): Promise<void> {
    if (this.scene.debugLayer.isVisible()) {
      this.scene.debugLayer.hide();
    } else {
      await import('@babylonjs/inspector');
      await this.scene.debugLayer.show({
        embedMode: false,
        enablePopup: false,
      });
    }
  }

  public selectEntity(obj: ProjectObject | null): void {
    this.selectedEntity = obj;
    this.onEntitySelected?.(obj);

    if (!obj) {
      if (this.entityHighlightBox) {
        this.entityHighlightBox.setEnabled(false);
      }
      return;
    }

    if (!this.entityHighlightBox) {
      this.entityHighlightBox = MeshBuilder.CreateBox('entityHighlightBox', { size: 3.2 }, this.scene);
      const mat = new StandardMaterial('entityHighlightMat', this.scene);
      mat.wireframe = true;
      mat.emissiveColor = new Color3(1.0, 0.9, 0.1);
      this.entityHighlightBox.material = mat;
      this.entityHighlightBox.isPickable = false;
    }

    this.entityHighlightBox.position.set(obj.position[0], obj.position[1] + 1.2, obj.position[2]);
    this.entityHighlightBox.setEnabled(true);
  }

  public focusOnEntity(obj: ProjectObject): void {
    this.selectEntity(obj);
    const targetPos = new Vector3(obj.position[0], obj.position[1] + 1.2, obj.position[2]);
    this.orbitCamera.target = targetPos;
    this.orbitCamera.radius = 16;
    if (this.currentMode !== 'orbit') {
      const canvas = this.engine.getRenderingCanvas();
      if (canvas) this.setCameraMode('orbit', canvas);
    }
  }

  public toggleEntityBounds(show: boolean): void {
    this.showEntityBounds = show;
    for (const bbox of this.entityBoundingBoxes) {
      bbox.setEnabled(show);
    }
  }

  public togglePortals(show: boolean): void {
    this.showPortals = show;
    for (const pm of this.portalMeshes) {
      pm.setEnabled(show);
    }
  }

  public toggleWaypoints(show: boolean): void {
    this.showWaypoints = show;
    for (const line of this.waypointLines) {
      line.setEnabled(show);
    }
  }

  public entityAnimationStatus(name: string): string {
    const controller = this.npcAnimators.get(name)?.controller;
    if (controller?.clip) return `${controller.clip.name} • frame ${Math.floor(controller.frame)} • ${controller.clip.frameRate} fps base`;
    return this.npcAnimationErrors.get(name) || 'Static / no bound animation';
  }

  public cancelAnimationSelection(): void {
    ++this.inspectionRequest;
    ++this.inspectionBlendRequest;
    this.releaseInspection();
    this.activeClip = null; this.isPlayingAnimation = false;
  }

  private releaseInspection(clearTarget = true): void {
    ++this.inspectionBlendRequest;
    const target = this.inspectionTarget;
    if (target && !target.disposed) {
      this.inspectionPreview = clearTarget ? null : target.controller.snapshot();
      target.showBones(false);
      if (this.inspectionSaved) { target.controller.restore(this.inspectionSaved); target.apply(); }
    }
    this.player.animationInspection = false;
    this.inspectionSaved = null;
    if (clearTarget) { this.inspectionTarget = null; this.inspectionPreview=null; this.isPlayingAnimation=false; }
  }

  private activateInspection(): void {
    const target = this.inspectionTarget;
    if (!this.animationInspection || !target || target.disposed || !this.activeClip) return;
    if (!this.inspectionSaved) this.inspectionSaved = target.controller.snapshot();
    if (this.inspectionPreview?.clip === this.activeClip) target.controller.restore(this.inspectionPreview);
    else {
      target.controller.setClip(this.activeClip, {playing: this.isPlayingAnimation});
      target.controller.seek(this.currentAnimFrame);
    }
    target.controller.speed = this.animSpeed;
    target.showBones(true);
    this.player.animationInspection = target === this.player.animator;
  }

  public async inspectAnimation(model: string, clip: AnimationClipData): Promise<string> {
    const request = ++this.inspectionRequest;
    this.releaseInspection();
    this.activeClip = null; this.isPlayingAnimation = false; this.currentAnimFrame = 0;
    const entry = await this.animations.resolve(model);
    if (!entry) throw new Error(`No rig available for ${model}.`);
    if (clip.bindingStatus !== 'verified-directory') {
      if (this.modelAnimator?.rig.model === entry.model) {
        this.modelAnimator.controller.playing = false;
        this.modelAnimator.controller.clip = null;
        this.modelAnimator.apply();
      }
      throw new Error(clip.bindingError || 'Unresolved bone binding.');
    }
    let target: SkeletalAnimator | null = null;
    let label = '';
    const selected = this.selectedEntity ? this.npcAnimators.get(this.selectedEntity.name) : null;
    if (selected?.rig.model === entry.model) { target = selected; label = this.selectedEntity!.name; }
    if (!target && entry.model === 'xh_' && this.currentMode === 'duncan') {
      await this.player.animationReady;
      target = this.player.animator; label = 'Duncan';
    }
    if (!target && this.modelAnimator?.rig.model === entry.model) {
      target = this.modelAnimator; label = `${entry.assetStem} model preview`;
    }
    if (!target) {
      const npc = [...this.npcAnimators.entries()].find(([, animator]) => animator.rig.model === entry.model);
      if (npc) { [label, target] = npc; }
    }
    if (!target) {
      if (request !== this.inspectionRequest) throw new Error('Animation selection changed.');
      const canvas = this.engine.getRenderingCanvas();
      if (canvas) this.setCameraMode('orbit', canvas);
      await this.loadAsset('models', `${entry.assetStem}.gltf`, true);
      target = this.modelAnimator; label = `${entry.assetStem} model preview`;
    }
    if (request !== this.inspectionRequest) throw new Error('Animation selection changed.');
    if (!target || target.disposed) throw new Error(this.modelAnimationError || `No playable model for ${entry.model}.`);
    this.inspectionTarget = target;
    this.activeClip = clip;
    const entity = this.currentProjectData?.objects.find(obj => this.npcAnimators.get(obj.name) === target);
    if (entity) this.focusOnEntity(entity);
    this.activateInspection();
    this.setAnimFrame(0);
    return label;
  }

  public setAnimationInspection(enabled: boolean): void {
    if (enabled === this.animationInspection) return;
    this.animationInspection = enabled;
    if (enabled) this.activateInspection(); else this.releaseInspection(false);
  }

  public pauseAnimation(): void {
    this.isPlayingAnimation = false;
    if (this.inspectionTarget && this.animationInspection) this.inspectionTarget.controller.playing = false;
  }

  public resumeAnimation(): void {
    if (this.activeClip && this.inspectionTarget) {
      if (this.currentAnimFrame>=this.activeClip.duration) this.setAnimFrame(this.activeClip.playbackStart ?? 0);
      this.isPlayingAnimation = true;
    }
  }

  public setAnimFrame(frame: number): void {
    this.currentAnimFrame = Math.max(0, Math.min(this.activeClip?.duration ?? 0, frame));
    if (this.inspectionTarget && this.animationInspection && this.activeClip) {
      this.inspectionTarget.controller.seek(this.currentAnimFrame);
      this.inspectionTarget.apply();
    }
    if (this.activeClip) this.onAnimFrameUpdate?.(Math.floor(this.currentAnimFrame),
      this.activeClip.duration, this.sampleActivePose(this.currentAnimFrame));
  }

  public setAnimSpeed(speed: number): void {
    if (Number.isFinite(speed) && speed >= 0) this.animSpeed = speed;
  }

  public setRootMode(mode: RootMode): void {
    if (!this.inspectionTarget || !this.animationInspection) return;
    this.inspectionTarget.controller.rootMode=mode;
    this.inspectionTarget.apply();
  }

  public setAnimationLoop(loop: boolean): void {
    if (this.inspectionTarget && this.animationInspection) this.inspectionTarget.controller.loop=loop;
  }

  public async setBlendClip(id: string, weight: number): Promise<void> {
    const request=++this.inspectionBlendRequest, target=this.inspectionTarget;
    if (!target || !this.animationInspection || !this.activeClip) throw new Error('Select a playable primary clip first.');
    const clip=id ? await this.animations.clip(this.activeClip.model,id) : null;
    if (request!==this.inspectionBlendRequest || target!==this.inspectionTarget || target.disposed) return;
    target.controller.setBlend(clip,weight);
    target.apply();
    this.onAnimFrameUpdate?.(Math.floor(this.currentAnimFrame),this.activeClip!.duration,target.controller.pose());
  }

  public setBlendWeight(weight: number): void {
    if (!this.inspectionTarget || !this.animationInspection) return;
    this.inspectionTarget.controller.setBlendWeight(weight);
    this.inspectionTarget.apply();
    if (this.activeClip) this.onAnimFrameUpdate?.(Math.floor(this.currentAnimFrame),
      this.activeClip.duration,this.inspectionTarget.controller.pose());
  }

  public animationRootReadout(): string {
    const controller=this.inspectionTarget?.controller;
    if (!controller || !this.animationInspection) return 'Root offset: —';
    const raw=controller.rootPosition(), bind=controller.rig.nodes[0].translation;
    const offset=raw.map((v,i)=>(v-bind[i])*(i===1 ? -0.01 : 0.01));
    const second=controller.secondaryFrame;
    return `Root offset: ${offset.map(v=>v.toFixed(2)).join(', ')} m${second===null ? '' : ` • B frame ${second.toFixed(1)}`}`;
  }

  public sampleActivePose(frame: number): Record<number, number[]> {
    if (this.inspectionTarget && this.animationInspection) return this.inspectionTarget.controller.pose();
    return this.activeClip ? sampleClipPose(this.activeClip, frame) : {};
  }
}
