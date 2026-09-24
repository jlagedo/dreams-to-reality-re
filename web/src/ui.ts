import { DreamsViewer, CameraMode, ProjectData, ProjectObject, AnimationClipData } from './viewer';
import type { GraphicsSettings } from './viewer';
import type { ClipEntry } from './animation/types';
import type { StartupController } from './boot/startup';

interface AssetEntry {
  id: string;
  filename: string;
  hasTextures: boolean;
}

export class UIManager {
  private viewer: DreamsViewer;
  private canvas: HTMLCanvasElement;

  private playDuncanBtn: HTMLButtonElement;
  private replayBootBtn: HTMLButtonElement | null = null;
  private startupController: StartupController | null = null;
  private musicBtn: HTMLButtonElement;
  private categorySelect: HTMLSelectElement;
  private itemSelect: HTMLSelectElement;
  private camModeBtn: HTMLButtonElement;
  private debugOverlayBtn: HTMLButtonElement;
  private resetCamBtn: HTMLButtonElement;
  private graphicsBtn: HTMLButtonElement;
  public graphicsPanel: HTMLElement;
  private hud: HTMLElement;
  private minimizeHudBtn: HTMLButtonElement;
  private hudNugget: HTMLButtonElement;
  private graphicsDatabase: Promise<IDBDatabase> | null = null;

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
  private currentClipsList: ClipEntry[] = [];
  private selectedEntity: ProjectObject | null = null;
  private animationRequest = 0;

  constructor(viewer: DreamsViewer, canvas: HTMLCanvasElement) {
    this.viewer = viewer;
    this.canvas = canvas;

    this.playDuncanBtn = document.getElementById('playDuncanBtn') as HTMLButtonElement;
    this.replayBootBtn = document.getElementById('replayBootBtn') as HTMLButtonElement | null;
    this.musicBtn = document.getElementById('musicBtn') as HTMLButtonElement;
    this.categorySelect = document.getElementById('categorySelect') as HTMLSelectElement;
    this.itemSelect = document.getElementById('itemSelect') as HTMLSelectElement;
    this.camModeBtn = document.getElementById('camModeBtn') as HTMLButtonElement;
    this.debugOverlayBtn = document.getElementById('debugOverlayBtn') as HTMLButtonElement;
    this.resetCamBtn = document.getElementById('resetCamBtn') as HTMLButtonElement;
    this.graphicsBtn = document.getElementById('graphicsBtn') as HTMLButtonElement;
    this.graphicsPanel = document.getElementById('graphicsPanel') as HTMLElement;
    this.hud = document.getElementById('hud') as HTMLElement;
    this.minimizeHudBtn = document.getElementById('minimizeHudBtn') as HTMLButtonElement;
    this.hudNugget = document.getElementById('hudNugget') as HTMLButtonElement;

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
    this.bindHudEvents();
    this.bindDebugEvents();
    void this.populateAnimationModels();
    this.bindGraphicsEvents();
    this.startFpsLoop();
  }

  public setStartupController(controller: StartupController): void {
    this.startupController = controller;
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
    this.syncAnimationInspection();
  }

  private syncAnimationInspection(): void {
    const animationTab = document.getElementById('debugTabAnimation');
    const animationFocus = !!animationTab?.classList.contains('active');
    this.debugOverlay.classList.toggle('animation-focus', animationFocus);
    this.debugOverlay.classList.toggle(
      'duncan-bones', animationFocus && this.animModelSelect.value === 'xh_'
    );
    this.viewer.setAnimationInspection(
      !this.debugOverlay.classList.contains('hidden') &&
      animationFocus
    );
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
    this.syncAnimationInspection();

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
    this.inspObjAnim0.textContent = this.viewer.entityAnimationStatus(obj.name);
    this.inspObjAnim1.textContent = 'Default clip preview; action selection not recovered';
    this.inspObjRoute.textContent = obj.routeIndex ? `Route #${obj.routeIndex}` : 'None';
    this.inspObjHealth.textContent = obj.health ? `${obj.health} HP` : '--';
  }

  private animationStatus(message: string, ready = false): void {
    document.getElementById('animationStatus')!.textContent = message;
    this.animPlayBtn.disabled = !ready;
    this.animResetBtn.disabled = !ready;
    this.animScrubber.disabled = !ready;
    (document.getElementById('animRootMode') as HTMLSelectElement).disabled = !ready;
    const blend=document.getElementById('animBlendClip') as HTMLSelectElement;
    blend.disabled = !ready;
    (document.getElementById('animBlendWeight') as HTMLInputElement).disabled = !ready || !blend.value;
    (document.getElementById('animLoop') as HTMLInputElement).disabled = !ready;
    this.setStatus(message);
  }

  private async populateAnimationModels(): Promise<void> {
    try {
      const models = await this.viewer.animations.catalog();
      const selected = this.animModelSelect.value || 'xh_';
      this.animModelSelect.replaceChildren();
      const names: Record<string, string> = {xh_: 'Duncan', mhe: 'Heroine', ch0: 'Creature', f07bleu: 'Blue Monk'};
      for (const model of [...models].sort((a,b) => a.model === 'xh_' ? -1 : b.model === 'xh_' ? 1 : a.model.localeCompare(b.model))) {
        const option = document.createElement('option');
        option.value = model.model;
        option.textContent = `${names[model.model] || model.model.toUpperCase()} — ${model.clipCount} clips`;
        this.animModelSelect.appendChild(option);
      }
      this.animModelSelect.value = models.find(m => m.model === selected || m.assetStem === selected)?.model || 'xh_';
    } catch (error) { this.animationStatus(`Animation library unavailable: ${error}`); }
  }

  public async loadModelAnimations(modelStem: string): Promise<void> {
    const request = ++this.animationRequest;
    this.viewer.cancelAnimationSelection();
    this.syncAnimationInspection();
    this.animationStatus(`Loading animations for ${modelStem}...`);
    this.boneMonitorTableBody.replaceChildren();
    try {
      const entry = await this.viewer.animations.resolve(modelStem);
      const clips = await this.viewer.animations.clips(modelStem);
      if (request !== this.animationRequest) return;
      if (entry) this.animModelSelect.value = entry.model;
      this.currentClipsList = clips;
      this.animClipSelect.replaceChildren();
      const blendSelect=document.getElementById('animBlendClip') as HTMLSelectElement;
      blendSelect.replaceChildren(new Option('No second clip',''));
      for (const clip of clips) {
        const option = document.createElement('option');
        option.value = clip.id;
        option.textContent = `${clip.name} (${clip.duration} frames)${clip.playable ? '' : ' — unresolved binding'}`;
        this.animClipSelect.appendChild(option);
        const blendOption=new Option(clip.name,clip.id);
        blendOption.disabled=!clip.playable;
        blendSelect.appendChild(blendOption);
      }
      const first = clips.find(clip => clip.playable) || clips[0];
      if (!first) { this.animationStatus(`No animation clips for ${modelStem}.`); return; }
      this.animClipSelect.value = first.id;
      await this.loadClipData(entry?.model || modelStem, first.id);
    } catch (error) {
      if (request === this.animationRequest) this.animationStatus(`Animation unavailable: ${error}`);
    }
  }

  public async loadClipData(modelStem: string, clipId: string): Promise<void> {
    const request = ++this.animationRequest;
    this.viewer.cancelAnimationSelection();
    (document.getElementById('animBlendClip') as HTMLSelectElement).value='';
    this.animationStatus(`Loading ${clipId}...`);
    this.boneMonitorTableBody.replaceChildren();
    this.animPlayBtn.textContent = '▶️ Play';
    this.animPlayBtn.classList.remove('playing');
    try {
      const clip: AnimationClipData = await this.viewer.animations.clip(modelStem, clipId);
      if (request !== this.animationRequest) return;
      this.animStatDuration.textContent = String(clip.duration);
      this.animStatFps.textContent = `${clip.frameRate} (engine base)`;
      this.animStatTracks.textContent = String(clip.trackCount);
      this.animScrubber.min = '0'; this.animScrubber.max = String(clip.duration);
      this.animScrubber.value = '0'; this.animFrameDisplay.textContent = `Frame 0 / ${clip.duration}`;
      for (const track of clip.tracks) {
        const tr = document.createElement('tr'); tr.id = `boneRow_${track.nodeIndex}`;
        const label = document.createElement('td');
        label.textContent = `${track.boneName || 'Unmapped'} [${track.nodeIndex}]`;
        const keys = document.createElement('td'); keys.textContent = `${track.keyframes.length} keys`;
        const stride = document.createElement('td'); stride.textContent = `${track.keyStride || '?'} bytes`;
        const quaternion = document.createElement('td'); quaternion.className = 'quat-val font-mono';
        quaternion.textContent = `(${track.restRotation.map(v => v.toFixed(3)).join(', ')})`;
        const norm = document.createElement('td'); norm.className = 'norm-val'; norm.textContent = '1.000';
        tr.append(label, keys, stride, quaternion, norm); this.boneMonitorTableBody.appendChild(tr);
      }
      const target = await this.viewer.inspectAnimation(modelStem, clip);
      if (request !== this.animationRequest) return;
      this.viewer.setRootMode((document.getElementById('animRootMode') as HTMLSelectElement).value as 'in-place' | 'animated');
      this.viewer.setAnimationLoop((document.getElementById('animLoop') as HTMLInputElement).checked);
      if (this.viewer.currentAssetCategory === 'models') {
        this.categorySelect.value = 'models'; this.populateItemSelect();
        this.itemSelect.value = `${this.viewer.currentSceneId}.gltf`;
      }
      if (this.viewer.currentMode !== 'duncan') {
        this.playDuncanBtn.textContent = '🎮 Play as Duncan (3rd Person)';
        this.playDuncanBtn.classList.remove('active');
        this.camModeBtn.textContent = this.viewer.currentMode === 'orbit' ? 'Cam: Orbit' : 'Cam: Walk';
      }
      this.animationStatus(`${target} • ${clip.name} • rotation + translation curves`, true);
    } catch (error) {
      if (request === this.animationRequest) this.animationStatus(error instanceof Error ? error.message : String(error));
    }
  }

  public updateAnimScrubber(
    frame: number,
    maxFrame: number,
    pose: Record<number, number[]>
  ): void {
    this.animScrubber.value = String(frame);
    this.animFrameDisplay.textContent = `Frame ${frame} / ${maxFrame}`;
    document.getElementById('animRootReadout')!.textContent=this.viewer.animationRootReadout();
    if (!this.viewer.isPlayingAnimation) {
      this.animPlayBtn.textContent='▶️ Play'; this.animPlayBtn.classList.remove('playing');
    }

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
    const rootMode=document.getElementById('animRootMode') as HTMLSelectElement;
    const blendSelect=document.getElementById('animBlendClip') as HTMLSelectElement;
    const weight=document.getElementById('animBlendWeight') as HTMLInputElement;
    rootMode.addEventListener('change',()=>this.viewer.setRootMode(rootMode.value as 'in-place' | 'animated'));
    document.getElementById('animLoop')!.addEventListener('change',event=>
      this.viewer.setAnimationLoop((event.target as HTMLInputElement).checked));
    blendSelect.addEventListener('change',async()=>{
      const request=this.animationRequest;
      try {
        await this.viewer.setBlendClip(blendSelect.value,Number(weight.value)/100);
        if (request!==this.animationRequest) return;
        weight.disabled=!blendSelect.value;
        this.animationStatus(blendSelect.value ? 'Blending two clips • independent frame clocks' : 'Single clip playback',true);
      } catch (error) {
        if (request===this.animationRequest) this.animationStatus(`Blend unavailable: ${error}`,true);
      }
    });
    weight.addEventListener('input',()=>{
      document.getElementById('animBlendWeightValue')!.textContent=`${weight.value}%`;
      this.viewer.setBlendWeight(Number(weight.value)/100);
    });
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

  public async enterDuncanMode(): Promise<void> {
    if (this.viewer.currentMode === 'duncan') {
      if (this.viewer.player.isSpawned) {
        this.viewer.player.respawn(this.viewer.getDefaultSpawnPos(this.viewer.currentSceneId));
      }
      return;
    }

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
      if (this.viewer.currentSceneId !== 'h18angkr') {
        await this.viewer.loadAsset('scenes', 'h18angkr.gltf');
      }
    }

    this.viewer.setCameraMode('duncan', this.canvas);
    this.animModelSelect.value = 'xh_';
    if (document.getElementById('debugTabAnimation')?.classList.contains('active') &&
        !this.debugOverlay.classList.contains('hidden')) void this.loadModelAnimations('xh_');
    this.showPortalBanner(
      'Spawned Duncan at Temple Altar! Walk into the mouth for the portal.'
    );
  }

  public exitDuncanMode(): void {
    if (this.viewer.currentMode !== 'duncan') return;
    this.viewer.setCameraMode('orbit', this.canvas);
    this.playDuncanBtn.classList.remove('active');
    this.playDuncanBtn.textContent = '🎮 Play as Duncan (3rd Person)';
    this.camModeBtn.textContent = 'Cam: Orbit';
  }

  private bindEvents(): void {
    // Play as Duncan third-person mode toggle
    this.playDuncanBtn.addEventListener('click', async () => {
      if (this.viewer.currentMode === 'duncan') {
        this.exitDuncanMode();
      } else {
        await this.enterDuncanMode();
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

    // Reset camera / Respawn
    this.resetCamBtn.addEventListener('click', () => {
      this.viewer.resetCamera();
    });

    this.replayBootBtn?.addEventListener('click', () => {
      void this.startupController?.startBootSequence();
    });
  }

  private bindGraphicsEvents(): void {
    const closeBtn = document.getElementById('closeGraphicsBtn') as HTMLButtonElement;
    const resetBtn = document.getElementById('resetGraphicsBtn') as HTMLButtonElement;
    const renderingMode = document.getElementById('gfxRenderingMode') as HTMLSelectElement;
    const renderScale = document.getElementById('gfxRenderScale') as HTMLInputElement;
    const renderScaleValue = document.getElementById('gfxRenderScaleValue') as HTMLOutputElement;
    const msaa = document.getElementById('gfxMsaa') as HTMLSelectElement;
    const fxaa = document.getElementById('gfxFxaa') as HTMLInputElement;
    const anisotropy = document.getElementById('gfxAnisotropy') as HTMLSelectElement;
    const textureFilter = document.getElementById('gfxTextureFilter') as HTMLSelectElement;
    const toneMapping = document.getElementById('gfxToneMapping') as HTMLInputElement;
    const exposure = document.getElementById('gfxExposure') as HTMLInputElement;
    const exposureValue = document.getElementById('gfxExposureValue') as HTMLOutputElement;
    const contrast = document.getElementById('gfxContrast') as HTMLInputElement;
    const contrastValue = document.getElementById('gfxContrastValue') as HTMLOutputElement;
    const sharpen = document.getElementById('gfxSharpen') as HTMLInputElement;
    const sharpenValue = document.getElementById('gfxSharpenValue') as HTMLOutputElement;
    const crtControls = document.getElementById('crtControls') as HTMLDetailsElement;
    const crtResolution = document.getElementById('gfxCrtResolution') as HTMLSelectElement;
    const crtStrength = document.getElementById('gfxCrtStrength') as HTMLInputElement;
    const crtStrengthValue = document.getElementById('gfxCrtStrengthValue') as HTMLOutputElement;
    const crtScanlines = document.getElementById('gfxCrtScanlines') as HTMLInputElement;
    const crtScanlinesValue = document.getElementById('gfxCrtScanlinesValue') as HTMLOutputElement;
    const crtMask = document.getElementById('gfxCrtMask') as HTMLInputElement;
    const crtMaskValue = document.getElementById('gfxCrtMaskValue') as HTMLOutputElement;
    const crtCurvature = document.getElementById('gfxCrtCurvature') as HTMLInputElement;
    const crtCurvatureValue = document.getElementById('gfxCrtCurvatureValue') as HTMLOutputElement;
    const crtGlow = document.getElementById('gfxCrtGlow') as HTMLInputElement;
    const crtGlowValue = document.getElementById('gfxCrtGlowValue') as HTMLOutputElement;
    const crtNoise = document.getElementById('gfxCrtNoise') as HTMLInputElement;
    const crtNoiseValue = document.getElementById('gfxCrtNoiseValue') as HTMLOutputElement;
    const crtVignette = document.getElementById('gfxCrtVignette') as HTMLInputElement;
    const crtVignetteValue = document.getElementById('gfxCrtVignetteValue') as HTMLOutputElement;
    const crtOverscan = document.getElementById('gfxCrtOverscan') as HTMLInputElement;
    const crtOverscanValue = document.getElementById('gfxCrtOverscanValue') as HTMLOutputElement;
    const crtAspectLock = document.getElementById('gfxCrtAspectLock') as HTMLInputElement;

    const syncControls = (): void => {
      const settings = this.viewer.getGraphicsSettings();
      renderingMode.value = settings.renderingMode;
      crtControls.classList.toggle('hidden', settings.renderingMode !== 'crt');
      renderScale.value = String(settings.renderScale);
      renderScaleValue.value = `${Math.round(settings.renderScale * 100)}%`;
      msaa.value = String(settings.msaaSamples);
      fxaa.checked = settings.fxaaEnabled;
      anisotropy.value = String(settings.anisotropy);
      textureFilter.value = settings.textureFilter;
      toneMapping.checked = settings.toneMappingEnabled;
      exposure.value = String(settings.exposure);
      exposureValue.value = settings.exposure.toFixed(2);
      contrast.value = String(settings.contrast);
      contrastValue.value = settings.contrast.toFixed(2);
      sharpen.value = String(settings.sharpen);
      sharpenValue.value = settings.sharpen.toFixed(2);
      crtResolution.value = String(settings.crtResolution);
      crtStrength.value = String(settings.crtStrength);
      crtStrengthValue.value = settings.crtStrength.toFixed(2);
      crtScanlines.value = String(settings.crtScanlines);
      crtScanlinesValue.value = settings.crtScanlines.toFixed(2);
      crtMask.value = String(settings.crtMask);
      crtMaskValue.value = settings.crtMask.toFixed(2);
      crtCurvature.value = String(settings.crtCurvature);
      crtCurvatureValue.value = settings.crtCurvature.toFixed(3);
      crtGlow.value = String(settings.crtGlow);
      crtGlowValue.value = settings.crtGlow.toFixed(2);
      crtNoise.value = String(settings.crtNoise);
      crtNoiseValue.value = settings.crtNoise.toFixed(3);
      crtVignette.value = String(settings.crtVignette);
      crtVignetteValue.value = settings.crtVignette.toFixed(2);
      crtOverscan.value = String(settings.crtOverscan);
      crtOverscanValue.value = settings.crtOverscan.toFixed(3);
      crtAspectLock.checked = settings.crtAspectLock;
    };

    const applyControls = (): void => {
      this.viewer.applyGraphicsSettings({
        renderingMode: renderingMode.value === 'crt' ? 'crt' : 'default',
        renderScale: Number(renderScale.value),
        msaaSamples: Number(msaa.value),
        fxaaEnabled: fxaa.checked,
        anisotropy: Number(anisotropy.value),
        textureFilter: textureFilter.value === 'linear' ? 'linear' : 'nearest',
        toneMappingEnabled: toneMapping.checked,
        exposure: Number(exposure.value),
        contrast: Number(contrast.value),
        sharpen: Number(sharpen.value),
        crtResolution: Number(crtResolution.value) === 800 ? 800 : 640,
        crtStrength: Number(crtStrength.value),
        crtScanlines: Number(crtScanlines.value),
        crtMask: Number(crtMask.value),
        crtCurvature: Number(crtCurvature.value),
        crtGlow: Number(crtGlow.value),
        crtNoise: Number(crtNoise.value),
        crtVignette: Number(crtVignette.value),
        crtOverscan: Number(crtOverscan.value),
        crtAspectLock: crtAspectLock.checked,
      });
      syncControls();
      scheduleSave();
    };

    const togglePanel = (show?: boolean): void => {
      const shouldShow = show ?? this.graphicsPanel.classList.contains('hidden');
      this.graphicsPanel.classList.toggle('hidden', !shouldShow);
      this.graphicsBtn.classList.toggle('active', shouldShow);
    };

    this.graphicsBtn.addEventListener('click', () => togglePanel());
    closeBtn.addEventListener('click', () => togglePanel(false));
    resetBtn.addEventListener('click', () => {
      this.viewer.resetGraphicsSettings();
      syncControls();
      void this.saveGraphicsSettings(this.viewer.getGraphicsSettings());
    });

    for (const input of [
      renderScale,
      exposure,
      contrast,
      sharpen,
      crtStrength,
      crtScanlines,
      crtMask,
      crtCurvature,
      crtGlow,
      crtNoise,
      crtVignette,
      crtOverscan,
    ]) {
      input.addEventListener('input', applyControls);
    }
    for (const input of [
      renderingMode,
      msaa,
      fxaa,
      anisotropy,
      textureFilter,
      toneMapping,
      crtResolution,
      crtAspectLock,
    ]) {
      input.addEventListener('change', applyControls);
    }

    window.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && !this.graphicsPanel.classList.contains('hidden')) {
        togglePanel(false);
      }
    });

    let saveTimer: number | undefined;
    const scheduleSave = (): void => {
      if (saveTimer !== undefined) window.clearTimeout(saveTimer);
      saveTimer = window.setTimeout(() => {
        saveTimer = undefined;
        void this.saveGraphicsSettings(this.viewer.getGraphicsSettings());
      }, 150);
    };

    syncControls();
    void this.loadGraphicsSettings().then((saved) => {
      if (saved) this.viewer.applyGraphicsSettings(saved);
      syncControls();
    });
  }

  private bindHudEvents(): void {
    const setMinimized = (minimized: boolean): void => {
      this.hud.classList.toggle('hud-minimized', minimized);
      this.hud.setAttribute('aria-hidden', String(minimized));
      this.hudNugget.hidden = !minimized;

      if (minimized) {
        this.graphicsPanel.classList.add('hidden');
        this.graphicsBtn.classList.remove('active');
        this.hudNugget.focus();
      } else {
        this.minimizeHudBtn.focus();
      }
    };

    this.minimizeHudBtn.addEventListener('click', () => setMinimized(true));
    this.hudNugget.addEventListener('click', () => setMinimized(false));
  }

  private openGraphicsDatabase(): Promise<IDBDatabase> {
    if (this.graphicsDatabase) return this.graphicsDatabase;

    this.graphicsDatabase = new Promise((resolve, reject) => {
      const request = indexedDB.open('dreams-viewer', 1);
      request.onupgradeneeded = () => {
        if (!request.result.objectStoreNames.contains('settings')) {
          request.result.createObjectStore('settings');
        }
      };
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error ?? new Error('Could not open settings database'));
    });
    return this.graphicsDatabase;
  }

  private async loadGraphicsSettings(): Promise<Partial<GraphicsSettings> | null> {
    try {
      const database = await this.openGraphicsDatabase();
      return await new Promise((resolve, reject) => {
        const request = database.transaction('settings', 'readonly')
          .objectStore('settings')
          .get('graphics');
        request.onsuccess = () => resolve(
          request.result && typeof request.result === 'object' ? request.result : null
        );
        request.onerror = () => reject(request.error);
      });
    } catch (error) {
      console.warn('Could not restore graphics settings from IndexedDB:', error);
      return null;
    }
  }

  private async saveGraphicsSettings(settings: GraphicsSettings): Promise<void> {
    try {
      const database = await this.openGraphicsDatabase();
      await new Promise<void>((resolve, reject) => {
        const transaction = database.transaction('settings', 'readwrite');
        transaction.objectStore('settings').put(settings, 'graphics');
        transaction.oncomplete = () => resolve();
        transaction.onerror = () => reject(transaction.error);
        transaction.onabort = () => reject(transaction.error);
      });
    } catch (error) {
      console.warn('Could not save graphics settings to IndexedDB:', error);
    }
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
