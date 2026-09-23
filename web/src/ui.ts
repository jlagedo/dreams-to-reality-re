import { DreamsViewer, CameraMode } from './viewer';

interface AssetEntry {
  id: string;
  filename: string;
  hasTextures: boolean;
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
  private inspectorBtn: HTMLButtonElement;
  private resetCamBtn: HTMLButtonElement;

  private fpsStat: HTMLElement;
  private meshCountStat: HTMLElement;
  private statusMessage: HTMLElement;

  private portalBanner: HTMLElement;
  private portalBannerText: HTMLElement;
  private portalBannerTimeout?: number;

  private scenesList: AssetEntry[] = [];
  private modelsList: AssetEntry[] = [];

  constructor(viewer: DreamsViewer, canvas: HTMLCanvasElement) {
    this.viewer = viewer;
    this.canvas = canvas;

    this.playDuncanBtn = document.getElementById('playDuncanBtn') as HTMLButtonElement;
    this.musicBtn = document.getElementById('musicBtn') as HTMLButtonElement;
    this.categorySelect = document.getElementById('categorySelect') as HTMLSelectElement;
    this.itemSelect = document.getElementById('itemSelect') as HTMLSelectElement;
    this.camModeBtn = document.getElementById('camModeBtn') as HTMLButtonElement;
    this.retroFilterBtn = document.getElementById('retroFilterBtn') as HTMLButtonElement;
    this.inspectorBtn = document.getElementById('inspectorBtn') as HTMLButtonElement;
    this.resetCamBtn = document.getElementById('resetCamBtn') as HTMLButtonElement;

    this.fpsStat = document.getElementById('fpsStat') as HTMLElement;
    this.meshCountStat = document.getElementById('meshCountStat') as HTMLElement;
    this.statusMessage = document.getElementById('statusMessage') as HTMLElement;

    this.portalBanner = document.getElementById('portalBanner') as HTMLElement;
    this.portalBannerText = document.getElementById('portalBannerText') as HTMLElement;

    this.bindEvents();
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

  private bindEvents(): void {
    // Play as Duncan third-person mode toggle
    this.playDuncanBtn.addEventListener('click', async () => {
      if (this.viewer.currentMode === 'duncan') {
        // Exit Duncan mode
        this.viewer.setCameraMode('orbit', this.canvas);
        this.playDuncanBtn.classList.remove('active');
        this.playDuncanBtn.textContent = '🎮 Play as Duncan (3rd Person)';
        this.camModeBtn.textContent = 'Cam: Orbit';
      } else {
        // Enter Duncan mode
        this.playDuncanBtn.classList.add('active');
        this.playDuncanBtn.textContent = '🛑 Exit Duncan Mode';
        this.camModeBtn.textContent = 'Cam: Duncan 3rd Person';

        // If not already in Angkor or Cave, load Angkor first
        if (this.categorySelect.value !== 'scenes' || (this.itemSelect.value !== 'h18angkr.gltf' && this.itemSelect.value !== 'f08_gpic.gltf')) {
          this.categorySelect.value = 'scenes';
          this.populateItemSelect();
          this.itemSelect.value = 'h18angkr.gltf';
          await this.viewer.loadAsset('scenes', 'h18angkr.gltf');
        }

        this.viewer.setCameraMode('duncan', this.canvas);
        this.showPortalBanner('Spawned Duncan at Temple Altar! Walk into the mouth for the portal.');
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
        this.camModeBtn.textContent = nextMode === 'orbit' ? 'Cam: Orbit' : 'Cam: Walk (WASD)';
      }
    });

    // Retro filter toggle
    this.retroFilterBtn.addEventListener('click', () => {
      const isRetro = this.viewer.toggleRetroFilter();
      this.retroFilterBtn.textContent = isRetro ? 'Filter: Nearest' : 'Filter: Linear';
    });

    // Inspector toggle
    this.inspectorBtn.addEventListener('click', () => {
      this.viewer.toggleInspector();
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
    }, 500);
  }
}
