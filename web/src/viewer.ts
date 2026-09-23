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
} from '@babylonjs/core';
import '@babylonjs/loaders/glTF';
import { DuncanPlayer } from './player';
import { AudioManager } from './audio';

export type CameraMode = 'orbit' | 'walk' | 'duncan';

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

  public player: DuncanPlayer;
  public audio: AudioManager;

  private currentContainer: AssetContainer | null = null;
  private currentAssetMeshes: AbstractMesh[] = [];
  public currentSceneId: string = '';

  private spawnedNpcContainers: AssetContainer[] = [];
  private spawnedNpcRoots: TransformNode[] = [];
  private waypointLines: LinesMesh[] = [];
  private portalMeshes: Mesh[] = [];
  private activePortalLinks: { mesh: Mesh; link: ProjectLink }[] = [];
  public currentProjectData: ProjectData | null = null;

  private portalPulseTime: number = 0;
  private portalCooldown: boolean = false;

  private onStatusChange?: (msg: string) => void;
  private onStatsChange?: (meshCount: number) => void;
  private onPortalTransitionNotify?: (targetScene: string) => void;
  private onSceneChange?: (sceneId: string) => void;

  constructor(
    canvas: HTMLCanvasElement,
    callbacks?: {
      onStatusChange?: (msg: string) => void;
      onStatsChange?: (meshCount: number) => void;
      onPortalTransitionNotify?: (targetScene: string) => void;
      onSceneChange?: (sceneId: string) => void;
    }
  ) {
    this.onStatusChange = callbacks?.onStatusChange;
    this.onStatsChange = callbacks?.onStatsChange;
    this.onPortalTransitionNotify = callbacks?.onPortalTransitionNotify;
    this.onSceneChange = callbacks?.onSceneChange;

    this.audio = new AudioManager();

    this.engine = new Engine(canvas, true, {
      preserveDrawingBuffer: true,
      stencil: true,
    });

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
    this.player = new DuncanPlayer(this.scene, this.orbitCamera);

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

    window.addEventListener('resize', () => {
      this.engine.resize();
    });

    // Render loop
    this.engine.runRenderLoop(() => {
      const dt = this.engine.getDeltaTime() / 1000;

      if (this.currentMode === 'duncan') {
        this.player.update(dt);
        this.checkPortalCollisions();
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

  public async loadAsset(category: 'scenes' | 'models', filename: string): Promise<void> {
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
        await this.spawnProjectEntities(this.currentSceneId);
        this.onSceneChange?.(this.currentSceneId);
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
    // Dispose spawned NPC containers
    for (const c of this.spawnedNpcContainers) {
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

  private async spawnProjectEntities(sceneStem: string): Promise<void> {
    try {
      const res = await fetch(`/api/project/scene/${sceneStem}`);
      if (!res.ok) return;

      const project: ProjectData | null = await res.json();
      if (!project) return;

      this.currentProjectData = project;
      console.log(`[Dreams] Spawning entities for ${project.name} (${sceneStem}):`, project);

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

          this.spawnedNpcContainers.push(container);
          this.spawnedNpcRoots.push(rootNode);
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
    this.retroFilterEnabled = !this.retroFilterEnabled;
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
      }
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
}
