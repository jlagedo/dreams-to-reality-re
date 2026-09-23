import { DreamsViewer, CameraMode, ProjectData, ProjectObject, AnimationClipData } from './viewer';

interface AssetEntry {
  id: string;
  filename: string;
  hasTextures: boolean;
}

interface AnimationManifestEntry {
  name: string;
  id: string;
  duration: number;
  trackCount: number;
  frameRate: number;
}

export class UIManager {
  private viewer: DreamsViewer;
  private canvas: HTMLCanvasElement;

  private playDuncanBtn: HTMLButtonElement;
  private musicBtn: HTMLButtonElement;
  private categorySelect: HTMLSelectElement;
  private itemSelect: HTMLSelectElement;
  private camModeBtn: HTMLButtonElement;
  private retroFilterBtn: HTMLButtonElement;
  private debugOverlayBtn: HTMLButtonElement;
  private resetCamBtn: HTMLButtonElement;

  private fpsStat: HTMLElement;
  private meshCountStat: HTMLElement;
  private statusMessage: HTMLElement;

  private portalBanner: HTMLElement;
  private portalBannerText: HTMLElement;
  private portalBannerTimeout?: number;

  // Debug Overlay Elements
  private debugOverlay: HTMLElement;
  private closeDebugBtn: HTMLButtonElement;
  private tabButtons: NodeListOf<HTMLButtonElement>;
  private tabPanes: NodeListOf<HTMLElement>;
  private debugEntityCountBadge: HTMLElement;

  // Scene Tab Elements
  private dbgProjectName: HTMLElement;
  private dbgSceneFile: HTMLElement;
  private dbgSceneStem: HTMLElement;
  private dbgCdTrack: HTMLElement;
  private dbgCameraFov: HTMLElement;
  private dbgLightingMode: HTMLElement;
  private dbgSpawnPos: HTMLElement;
  private dbgSpawnHeading: HTMLElement;
  private dbgSpawnYaw: HTMLElement;
  private dbgPlayerPos: HTMLElement;
  private dbgAmbientSwatch: HTMLElement;
  private dbgAmbientRgb: HTMLElement;
  private dbgDirLight1: HTMLElement;
  private dbgDirLight2: HTMLElement;
  private dbgFog: HTMLElement;

  private dbgToggleEntityBounds: HTMLInputElement;
  private dbgTogglePortals: HTMLInputElement;
  private dbgToggleWaypoints: HTMLInputElement;

  // Entities Tab Elements
  private entitiesTableBody: HTMLElement;
  private inspBadgeCategory: HTMLElement;
  private inspProjectName: HTMLElement;
  private inspObjName: HTMLElement;
  private inspObjAsset: HTMLElement;
  private inspObjPos: HTMLElement;
  private inspObjSpeed: HTMLElement;
  private inspObjPhySpeed: HTMLElement;
  private inspObjFlags: HTMLElement;
  private inspObjAngle: HTMLElement;
  private inspObj3dCol: HTMLElement;
  private inspObjAnim0: HTMLElement;
  private inspObjAnim1: HTMLElement;
  private inspObjRoute: HTMLElement;
  private inspObjHealth: HTMLElement;
  private inspFocusBtn: HTMLButtonElement;
  private inspAnimBtn: HTMLButtonElement;

  // Animation Tab Elements
  private animModelSelect: HTMLSelectElement;
  private animClipSelect: HTMLSelectElement;
  private animPlayBtn: HTMLButtonElement;
  private animResetBtn: HTMLButtonElement;
  private animScrubber: HTMLInputElement;
  private animFrameDisplay: HTMLElement;
  private animStatDuration: HTMLElement;
  private animStatFps: HTMLElement;
  private animStatTracks: HTMLElement;
  private boneMonitorTableBody: HTMLElement;
  private speedButtons: NodeListOf<HTMLButtonElement>;

  private scenesList: AssetEntry[] = [];
  private modelsList: AssetEntry[] = [];
  private currentClipsList: AnimationManifestEntry[] = [];
  private selectedEntity: ProjectObject | null = null;

  constructor(viewer: DreamsViewer, canvas: HTMLCanvasElement) {
    this.viewer = viewer;
    this.canvas = canvas;

    this.playDuncanBtn = document.getElementById('playDuncanBtn') as HTMLButtonElement;
    this.musicBtn = document.getElementById('musicBtn') as HTMLButtonElement;
    this.categorySelect = document.getElementById('categorySelect') as HTMLSelectElement;
    this.itemSelect = document.getElementById('itemSelect') as HTMLSelectElement;
    this.camModeBtn = document.getElementById('camModeBtn') as HTMLButtonElement;
    this.retroFilterBtn = document.getElementById('retroFilterBtn') as HTMLButtonElement;
    this.debugOverlayBtn = document.getElementById('debugOverlayBtn') as HTMLButtonElement;
    this.resetCamBtn = document.getElementById('resetCamBtn') as HTMLButtonElement;

    this.fpsStat = document.getElementById('fpsStat') as HTMLElement;
    this.meshCountStat = document.getElementById('meshCountStat') as HTMLElement;
    this.statusMessage = document.getElementById('statusMessage') as HTMLElement;

    this.portalBanner = document.getElementById('portalBanner') as HTMLElement;
    this.portalBannerText = document.getElementById('portalBannerText') as HTMLElement;

    // Debug Overlay DOM
    this.debugOverlay = document.getElementById('debugOverlay') as HTMLElement;
    this.closeDebugBtn = document.getElementById('closeDebugBtn') as HTMLButtonElement;
    this.tabButtons = document.querySelectorAll('.debug-tab-btn');
    this.tabPanes = document.querySelectorAll('.debug-tab-pane');
    this.debugEntityCountBadge = document.getElementById('debugEntityCountBadge') as HTMLElement;

    // Scene fields
    this.dbgProjectName = document.getElementById('dbgProjectName') as HTMLElement;
    this.dbgSceneFile = document.getElementById('dbgSceneFile') as HTMLElement;
    this.dbgSceneStem = document.getElementById('dbgSceneStem') as HTMLElement;
    this.dbgCdTrack = document.getElementById('dbgCdTrack') as HTMLElement;
    this.dbgCameraFov = document.getElementById('dbgCameraFov') as HTMLElement;
    this.dbgLightingMode = document.getElementById('dbgLightingMode') as HTMLElement;
    this.dbgSpawnPos = document.getElementById('dbgSpawnPos') as HTMLElement;
    this.dbgSpawnHeading = document.getElementById('dbgSpawnHeading') as HTMLElement;
    this.dbgSpawnYaw = document.getElementById('dbgSpawnYaw') as HTMLElement;
    this.dbgPlayerPos = document.getElementById('dbgPlayerPos') as HTMLElement;
    this.dbgAmbientSwatch = document.getElementById('dbgAmbientSwatch') as HTMLElement;
    this.dbgAmbientRgb = document.getElementById('dbgAmbientRgb') as HTMLElement;
    this.dbgDirLight1 = document.getElementById('dbgDirLight1') as HTMLElement;
    this.dbgDirLight2 = document.getElementById('dbgDirLight2') as HTMLElement;
    this.dbgFog = document.getElementById('dbgFog') as HTMLElement;

    this.dbgToggleEntityBounds = document.getElementById('dbgToggleEntityBounds') as HTMLInputElement;
    this.dbgTogglePortals = document.getElementById('dbgTogglePortals') as HTMLInputElement;
    this.dbgToggleWaypoints = document.getElementById('dbgToggleWaypoints') as HTMLInputElement;

    // Entities fields
    this.entitiesTableBody = document.getElementById('entitiesTableBody') as HTMLElement;
    this.inspBadgeCategory = document.getElementById('inspBadgeCategory') as HTMLElement;
    this.inspProjectName = document.getElementById('inspProjectName') as HTMLElement;
    this.inspObjName = document.getElementById('inspObjName') as HTMLElement;
    this.inspObjAsset = document.getElementById('inspObjAsset') as HTMLElement;
    this.inspObjPos = document.getElementById('inspObjPos') as HTMLElement;
    this.inspObjSpeed = document.getElementById('inspObjSpeed') as HTMLElement;
    this.inspObjPhySpeed = document.getElementById('inspObjPhySpeed') as HTMLElement;
    this.inspObjFlags = document.getElementById('inspObjFlags') as HTMLElement;
    this.inspObjAngle = document.getElementById('inspObjAngle') as HTMLElement;
    this.inspObj3dCol = document.getElementById('inspObj3dCol') as HTMLElement;
    this.inspObjAnim0 = document.getElementById('inspObjAnim0') as HTMLElement;
    this.inspObjAnim1 = document.getElementById('inspObjAnim1') as HTMLElement;
    this.inspObjRoute = document.getElementById('inspObjRoute') as HTMLElement;
    this.inspObjHealth = document.getElementById('inspObjHealth') as HTMLElement;
    this.inspFocusBtn = document.getElementById('inspFocusBtn') as HTMLButtonElement;
    this.inspAnimBtn = document.getElementById('inspAnimBtn') as HTMLButtonElement;

    // Animation fields
    this.animModelSelect = document.getElementById('animModelSelect') as HTMLSelectElement;
    this.animClipSelect = document.getElementById('animClipSelect') as HTMLSelectElement;
    this.animPlayBtn = document.getElementById('animPlayBtn') as HTMLButtonElement;
    this.animResetBtn = document.getElementById('animResetBtn') as HTMLButtonElement;
    this.animScrubber = document.getElementById('animScrubber') as HTMLInputElement;
    this.animFrameDisplay = document.getElementById('animFrameDisplay') as HTMLElement;
    this.animStatDuration = document.getElementById('animStatDuration') as HTMLElement;
    this.animStatFps = document.getElementById('animStatFps') as HTMLElement;
    this.animStatTracks = document.getElementById('animStatTracks') as HTMLElement;
    this.boneMonitorTableBody = document.getElementById('boneMonitorTableBody') as HTMLElement;
    this.speedButtons = document.querySelectorAll('.speed-btn');

    this.bindEvents();
    this.bindDebugEvents();
    this.startFpsLoop();
  }

  public setStatus(msg: string): void {
    this.statusMessage.textContent = msg;
  }

  public setMeshCount(count: number): void {
    this.meshCountStat.textContent = `Meshes: ${count}`;
  }

  public showPortalBanner(msg: string): void {
    this.portalBannerText.textContent = msg;
    this.portalBanner.classList.remove('hidden');

    if (this.portalBannerTimeout) {
      window.clearTimeout(this.portalBannerTimeout);
    }

    this.portalBannerTimeout = window.setTimeout(() => {
      this.portalBanner.classList.add('hidden');
    }, 3200);
  }

  public syncSelectedScene(sceneId: string): void {
    if (this.categorySelect.value !== 'scenes') {
      this.categorySelect.value = 'scenes';
      this.populateItemSelect();
    }
    const filename = `${sceneId}.gltf`;
    if (this.itemSelect.value !== filename) {
      this.itemSelect.value = filename;
    }
  }

  public toggleDebugOverlay(): void {
    const isHidden = this.debugOverlay.classList.contains('hidden');
    if (isHidden) {
      this.debugOverlay.classList.remove('hidden');
      this.debugOverlayBtn.classList.add('active');
    } else {
      this.debugOverlay.classList.add('hidden');
      this.debugOverlayBtn.classList.remove('active');
    }
  }

  public switchDebugTab(tabName: string): void {
    this.tabButtons.forEach((btn) => {
      btn.classList.toggle('active', btn.getAttribute('data-tab') === tabName);
    });
    this.tabPanes.forEach((pane) => {
      pane.classList.remove('active');
    });

    const targetPane = document.getElementById(
      `debugTab${tabName.charAt(0).toUpperCase() + tabName.slice(1)}`
    );
    if (targetPane) {
      targetPane.classList.add('active');
    }

    if (tabName === 'animation' && (!this.currentClipsList || this.currentClipsList.length === 0)) {
      this.loadModelAnimations(this.animModelSelect.value);
    }
  }

  public updateProjectDebugHUD(project: ProjectData): void {
    this.dbgProjectName.textContent = project.name;
    this.dbgSceneFile.textContent = project.scene;
    this.dbgSceneStem.textContent = project.sceneStem;
    this.dbgCdTrack.textContent = project.cdTrack ? `Track ${project.cdTrack}` : 'None';
    this.dbgCameraFov.textContent = project.cameraFov ? `${project.cameraFov}°` : '64°';
    this.dbgLightingMode.textContent =
      project.lightingMode === 0 ? 'Day (Mode 0)' : `Mode ${project.lightingMode}`;

    if (project.spawnPosition) {
      const [x, y, z] = project.spawnPosition;
      this.dbgSpawnPos.textContent = `(${x.toFixed(2)}, ${y.toFixed(2)}, ${z.toFixed(2)})`;
    } else {
      this.dbgSpawnPos.textContent = '(0.00, 0.00, 0.00)';
    }

    this.dbgSpawnHeading.textContent = `${(project.spawnHeadingDeg ?? 0).toFixed(1)}° (raw ${project.spawnRawHeading ?? 0})`;
    this.dbgSpawnYaw.textContent = `${(project.spawnYaw ?? 0).toFixed(4)}`;

    if (project.ambientRgb) {
      const [r, g, b] = project.ambientRgb;
      this.dbgAmbientRgb.textContent = `${r}, ${g}, ${b}`;
      this.dbgAmbientSwatch.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    }

    if (project.dirLight1) {
      const [x, y, z] = project.dirLight1;
      this.dbgDirLight1.textContent = `(${x}, ${y}, ${z})`;
    }
    if (project.dirLight2) {
      const [x, y, z] = project.dirLight2;
      this.dbgDirLight2.textContent = `(${x}, ${y}, ${z})`;
    }
    if (project.fog) {
      const [r, g, b, d] = project.fog;
      this.dbgFog.textContent = d > 0 ? `Density ${d} (${r}, ${g}, ${b})` : 'None (0, 0, 0, 0)';
    }

    // Populate Entities Table
    const activeObjects = project.objects.filter((o) => o.name !== 'OBJET0');
    this.debugEntityCountBadge.textContent = String(activeObjects.length);
    this.entitiesTableBody.innerHTML = '';

    for (const obj of activeObjects) {
      const tr = document.createElement('tr');
      tr.setAttribute('data-obj-name', obj.name);

      const [x, y, z] = obj.position;
      const deg = ((obj.rawYaw / 4096.0) * 360).toFixed(1);
      const behaviorNames: Record<number, string> = {
        0: 'Static',
        1: 'Patrol',
        3: 'Hostile',
        5: 'Path',
        6: 'Fly',
      };
      const aiLabel = behaviorNames[obj.behaviorType ?? 0] || `Type ${obj.behaviorType}`;

      tr.innerHTML = `
        <td><strong>${obj.name}</strong></td>
        <td>${obj.asset}</td>
        <td><span class="cat-badge ${obj.category}">${obj.category.toUpperCase()}</span></td>
        <td>(${x.toFixed(1)}, ${y.toFixed(1)}, ${z.toFixed(1)})</td>
        <td>${deg}°</td>
        <td>${aiLabel}</td>
        <td>${obj.speed ?? 0}</td>
        <td>0x${(obj.flags ?? 0).toString(16).padStart(4, '0')}</td>
        <td><button class="table-btn" data-focus="${obj.name}">Focus</button></td>
      `;

      tr.addEventListener('click', (e) => {
        const target = e.target as HTMLElement;
        if (target.tagName === 'BUTTON') {
          this.viewer.focusOnEntity(obj);
        } else {
          this.viewer.selectEntity(obj);
        }
      });

      this.entitiesTableBody.appendChild(tr);
    }

    if (activeObjects.length > 0) {
      this.displayEntityInspection(activeObjects[0]);
    }
  }

  public displayEntityInspection(obj: ProjectObject | null): void {
    this.selectedEntity = obj;
    if (!obj) return;

    // Highlight row in table
    const rows = this.entitiesTableBody.querySelectorAll('tr');
    rows.forEach((r) => {
      r.classList.toggle('selected', r.getAttribute('data-obj-name') === obj.name);
    });

    const [x, y, z] = obj.position;
    const deg = ((obj.rawYaw / 4096.0) * 360).toFixed(1);

    this.inspBadgeCategory.textContent = obj.category.toUpperCase();
    this.inspBadgeCategory.className = `badge cat-badge ${obj.category}`;
    this.inspProjectName.textContent = this.viewer.currentProjectData?.name || 'Project0';
    this.inspObjName.textContent = obj.name;
    this.inspObjAsset.textContent = obj.asset;
    this.inspObjPos.textContent = `(${x.toFixed(2)}, ${y.toFixed(2)}, ${z.toFixed(2)})`;
    this.inspObjSpeed.textContent = `${obj.speed ?? 16} (base)`;
    this.inspObjPhySpeed.textContent = `${obj.speed ?? 16}`;
    this.inspObjFlags.textContent = `0x${(obj.flags ?? 0).toString(16).padStart(8, '0')} (${
      obj.isActive ? 'Active' : 'Dormant'
    }${obj.isCharacter ? ', Char' : ''})`;
    this.inspObjAngle.textContent = `Yaw ${deg}° (raw ${obj.rawYaw})`;
    this.inspObj3dCol.textContent = `Radius ${obj.radius ?? 0}`;
    this.inspObjAnim0.textContent = `Track 0 / Speed ${obj.speed ?? 0}`;
    this.inspObjAnim1.textContent = `Track 1 / Standby`;
    this.inspObjRoute.textContent = obj.routeIndex ? `Route #${obj.routeIndex}` : 'None';
    this.inspObjHealth.textContent = obj.health ? `${obj.health} HP` : '--';
  }

  public async loadModelAnimations(modelStem: string): Promise<void> {
    try {
      this.setStatus(`Loading animation manifest for ${modelStem}...`);
      const res = await fetch(`/api/animations/${modelStem}`);
      if (!res.ok) return;

      this.currentClipsList = await res.json();
      this.animClipSelect.innerHTML = '';

      if (this.currentClipsList.length === 0) {
        const opt = document.createElement('option');
        opt.textContent = 'No animations found';
        this.animClipSelect.appendChild(opt);
        return;
      }

      for (const clip of this.currentClipsList) {
        const opt = document.createElement('option');
        opt.value = clip.id;
        opt.textContent = `${clip.name} (${clip.duration} frames, ${clip.trackCount} tracks)`;
        this.animClipSelect.appendChild(opt);
      }

      if (this.currentClipsList.length > 0) {
        await this.loadClipData(modelStem, this.currentClipsList[0].id);
      }
    } catch (e) {
      console.error('Error loading animations:', e);
    }
  }

  public async loadClipData(modelStem: string, clipId: string): Promise<void> {
    try {
      this.setStatus(`Loading animation ${clipId}...`);
      const res = await fetch(`/api/animation/${modelStem}/${clipId}`);
      if (!res.ok) return;

      const clip: AnimationClipData = await res.json();
      this.viewer.activeClip = clip;
      this.viewer.currentAnimFrame = 0;

      this.animStatDuration.textContent = String(clip.duration);
      this.animStatFps.textContent = String(clip.frameRate);
      this.animStatTracks.textContent = String(clip.trackCount);

      this.animScrubber.min = '0';
      this.animScrubber.max = String(clip.duration);
      this.animScrubber.value = '0';
      this.animFrameDisplay.textContent = `Frame 0 / ${clip.duration}`;

      // Populate bone monitor table
      this.boneMonitorTableBody.innerHTML = '';
      for (const trk of clip.tracks) {
        const tr = document.createElement('tr');
        tr.id = `boneRow_${trk.nodeIndex}`;
        const [rx, ry, rz, rw] = trk.restRotation;
        tr.innerHTML = `
          <td><strong>Node ${trk.nodeIndex}</strong></td>
          <td>${trk.keyframes.length} keys</td>
          <td>${trk.interpType === 4 ? 'Hermite Spline (60B)' : 'Linear (20B)'}</td>
          <td class="quat-val font-mono">(${rx.toFixed(3)}, ${ry.toFixed(3)}, ${rz.toFixed(3)}, ${rw.toFixed(3)})</td>
          <td class="norm-val">1.000</td>
        `;
        this.boneMonitorTableBody.appendChild(tr);
      }

      this.setStatus(`Ready animation ${clip.name}`);
    } catch (e) {
      console.error('Error loading clip data:', e);
    }
  }

  public updateAnimScrubber(
    frame: number,
    maxFrame: number,
    pose: Record<number, number[]>
  ): void {
    this.animScrubber.value = String(frame);
    this.animFrameDisplay.textContent = `Frame ${frame} / ${maxFrame}`;

    // Update live quaternion table values
    for (const [nodeIndex, quat] of Object.entries(pose)) {
      const row = document.getElementById(`boneRow_${nodeIndex}`);
      if (row && quat.length === 4) {
        const quatCell = row.querySelector('.quat-val');
        const normCell = row.querySelector('.norm-val');
        const [x, y, z, w] = quat;
        if (quatCell) {
          quatCell.textContent = `(${x.toFixed(3)}, ${y.toFixed(3)}, ${z.toFixed(3)}, ${w.toFixed(3)})`;
        }
        if (normCell) {
          const norm = Math.sqrt(x * x + y * y + z * z + w * w);
          normCell.textContent = norm.toFixed(3);
        }
      }
    }
  }

  private bindDebugEvents(): void {
    // F3 toggle shortcut
    window.addEventListener('keydown', (e) => {
      if (e.key === 'F3') {
        e.preventDefault();
        this.toggleDebugOverlay();
      }
    });

    this.debugOverlayBtn.addEventListener('click', () => {
      this.toggleDebugOverlay();
    });

    this.closeDebugBtn.addEventListener('click', () => {
      this.toggleDebugOverlay();
    });

    // Tab buttons
    this.tabButtons.forEach((btn) => {
      btn.addEventListener('click', () => {
        const tab = btn.getAttribute('data-tab');
        if (tab) this.switchDebugTab(tab);
      });
    });

    // Toggles
    this.dbgToggleEntityBounds.addEventListener('change', (e) => {
      const checked = (e.target as HTMLInputElement).checked;
      this.viewer.toggleEntityBounds(checked);
    });

    this.dbgTogglePortals.addEventListener('change', (e) => {
      const checked = (e.target as HTMLInputElement).checked;
      this.viewer.togglePortals(checked);
    });

    this.dbgToggleWaypoints.addEventListener('change', (e) => {
      const checked = (e.target as HTMLInputElement).checked;
      this.viewer.toggleWaypoints(checked);
    });

    // Inspector Focus & Anim buttons
    this.inspFocusBtn.addEventListener('click', () => {
      if (this.selectedEntity) {
        this.viewer.focusOnEntity(this.selectedEntity);
      }
    });

    this.inspAnimBtn.addEventListener('click', () => {
      if (this.selectedEntity) {
        const assetStem = this.selectedEntity.assetStem;
        this.switchDebugTab('animation');
        this.animModelSelect.value = assetStem;
        this.loadModelAnimations(assetStem);
      }
    });

    // Animation Player Events
    this.animModelSelect.addEventListener('change', () => {
      this.loadModelAnimations(this.animModelSelect.value);
    });

    this.animClipSelect.addEventListener('change', () => {
      this.loadClipData(this.animModelSelect.value, this.animClipSelect.value);
    });

    this.animPlayBtn.addEventListener('click', () => {
      if (this.viewer.isPlayingAnimation) {
        this.viewer.pauseAnimation();
        this.animPlayBtn.textContent = '▶️ Play';
        this.animPlayBtn.classList.remove('playing');
      } else {
        this.viewer.resumeAnimation();
        this.animPlayBtn.textContent = '⏸️ Pause';
        this.animPlayBtn.classList.add('playing');
      }
    });

    this.animResetBtn.addEventListener('click', () => {
      this.viewer.setAnimFrame(0);
    });

    this.animScrubber.addEventListener('input', (e) => {
      const val = parseFloat((e.target as HTMLInputElement).value);
      this.viewer.setAnimFrame(val);
    });

    this.speedButtons.forEach((btn) => {
      btn.addEventListener('click', () => {
        this.speedButtons.forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        const speed = parseFloat(btn.getAttribute('data-speed') || '1.0');
        this.viewer.setAnimSpeed(speed);
      });
    });
  }

  private bindEvents(): void {
    // Play as Duncan third-person mode toggle
    this.playDuncanBtn.addEventListener('click', async () => {
      if (this.viewer.currentMode === 'duncan') {
        this.viewer.setCameraMode('orbit', this.canvas);
        this.playDuncanBtn.classList.remove('active');
        this.playDuncanBtn.textContent = '🎮 Play as Duncan (3rd Person)';
        this.camModeBtn.textContent = 'Cam: Orbit';
      } else {
        this.playDuncanBtn.classList.add('active');
        this.playDuncanBtn.textContent = '🛑 Exit Duncan Mode';
        this.camModeBtn.textContent = 'Cam: Duncan 3rd Person';

        if (
          this.categorySelect.value !== 'scenes' ||
          (this.itemSelect.value !== 'h18angkr.gltf' &&
            this.itemSelect.value !== 'f08_gpic.gltf')
        ) {
          this.categorySelect.value = 'scenes';
          this.populateItemSelect();
          this.itemSelect.value = 'h18angkr.gltf';
          await this.viewer.loadAsset('scenes', 'h18angkr.gltf');
        }

        this.viewer.setCameraMode('duncan', this.canvas);
        this.showPortalBanner(
          'Spawned Duncan at Temple Altar! Walk into the mouth for the portal.'
        );
      }
    });

    // 1997 CD Audio Music toggle
    this.musicBtn.addEventListener('click', () => {
      const playing = this.viewer.audio.toggleMusic();
      this.musicBtn.textContent = playing ? '🎵 Music: Playing' : '🎵 Music: Off';
    });

    // Category switch (scenes vs models)
    this.categorySelect.addEventListener('change', () => {
      this.populateItemSelect();
      this.loadSelectedItem();
    });

    // Asset selection
    this.itemSelect.addEventListener('change', () => {
      this.loadSelectedItem();
    });

    // Camera mode cycle
    this.camModeBtn.addEventListener('click', () => {
      let nextMode: CameraMode = 'orbit';
      if (this.viewer.currentMode === 'orbit') nextMode = 'walk';
      else if (this.viewer.currentMode === 'walk') nextMode = 'duncan';
      else nextMode = 'orbit';

      this.viewer.setCameraMode(nextMode, this.canvas);

      if (nextMode === 'duncan') {
        this.playDuncanBtn.classList.add('active');
        this.playDuncanBtn.textContent = '🛑 Exit Duncan Mode';
        this.camModeBtn.textContent = 'Cam: Duncan 3P';
      } else {
        this.playDuncanBtn.classList.remove('active');
        this.playDuncanBtn.textContent = '🎮 Play as Duncan (3rd Person)';
        this.camModeBtn.textContent =
          nextMode === 'orbit' ? 'Cam: Orbit' : 'Cam: Walk (WASD)';
      }
    });

    // Retro filter toggle
    this.retroFilterBtn.addEventListener('click', () => {
      const isRetro = this.viewer.toggleRetroFilter();
      this.retroFilterBtn.textContent = isRetro
        ? 'Filter: Nearest'
        : 'Filter: Linear';
    });

    // Reset camera / Respawn
    this.resetCamBtn.addEventListener('click', () => {
      this.viewer.resetCamera();
    });
  }

  public async initData(): Promise<void> {
    try {
      this.setStatus('Fetching asset lists...');
      const [scenesRes, modelsRes] = await Promise.all([
        fetch('/api/scenes'),
        fetch('/api/models'),
      ]);

      this.scenesList = await scenesRes.json();
      this.modelsList = await modelsRes.json();

      this.populateItemSelect();

      // Default to h18angkr [textured]
      const angkor = this.scenesList.find((s) => s.id === 'h18angkr');
      if (angkor) {
        this.itemSelect.value = angkor.filename;
        await this.loadSelectedItem();
      } else if (this.scenesList.length > 0) {
        this.itemSelect.value = this.scenesList[0].filename;
        await this.loadSelectedItem();
      }
    } catch (err) {
      console.error('Error fetching asset list:', err);
      this.setStatus('Could not connect to asset server');
    }
  }

  private populateItemSelect(): void {
    const isScenes = this.categorySelect.value === 'scenes';
    const list = isScenes ? this.scenesList : this.modelsList;

    this.itemSelect.innerHTML = '';

    for (const item of list) {
      const option = document.createElement('option');
      option.value = item.filename;
      const tag = item.hasTextures ? ' [textured]' : '';
      option.textContent = `${item.id}${tag}`;
      this.itemSelect.appendChild(option);
    }
  }

  private async loadSelectedItem(): Promise<void> {
    const category = this.categorySelect.value as 'scenes' | 'models';
    const filename = this.itemSelect.value;
    if (!filename) return;

    await this.viewer.loadAsset(category, filename);
  }

  private startFpsLoop(): void {
    setInterval(() => {
      const fps = this.viewer.engine.getFps().toFixed(0);
      this.fpsStat.textContent = `FPS: ${fps}`;

      // Update live player coordinates in debug HUD if Duncan is active
      if (this.viewer.currentMode === 'duncan' && this.viewer.player.isSpawned) {
        const p = this.viewer.player.rootNode.position;
        this.dbgPlayerPos.textContent = `(${p.x.toFixed(2)}, ${p.y.toFixed(2)}, ${p.z.toFixed(2)})`;
      }
    }, 500);
  }
}
