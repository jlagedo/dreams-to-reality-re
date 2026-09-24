import type { DreamsViewer } from '../viewer';
import type { UIManager } from '../ui';

export type BootStep =
  | 'unstarted'
  | 'intro'
  | 'warp'
  | 'menu'
  | 'save_browser'
  | 'quit_prompt'
  | 'transition'
  | 'elder'
  | 'loading'
  | 'in_game';

export class StartupController {
  private viewer: DreamsViewer;
  private ui: UIManager;
  public currentStep: BootStep = 'unstarted';

  // DOM Elements
  private bootContainer!: HTMLElement;
  private videoEl!: HTMLVideoElement;
  private bannerEl!: HTMLElement;
  private menuEl!: HTMLElement;
  private menuItems!: HTMLElement[];
  private saveBrowserEl!: HTMLElement;
  private quitModalEl!: HTMLElement;
  private transitionEl!: HTMLElement;
  private transitionTimerEl!: HTMLElement;
  private loadingEl!: HTMLElement;

  private selectedMenuIndex: number = 0; // 0: New Game, 1: Load, 2: Options, 3: Quit
  private transitionRemaining: number = 15.0;
  private transitionRaf?: number;
  private isDestroyed: boolean = false;
  private isInGame: boolean = false;
  private initPromise?: Promise<void>;

  constructor(viewer: DreamsViewer, ui: UIManager) {
    this.viewer = viewer;
    this.ui = ui;
    this.createDom();
    this.bindEvents();
    (window as unknown as { __bootController: StartupController }).__bootController = this;
  }

  private createDom(): void {
    // Check if #bootScreen already exists and remove previous instance
    let container = document.getElementById('bootScreen');
    if (container) {
      container.remove();
    }
    container = document.createElement('div');
    container.id = 'bootScreen';
    container.className = 'boot-screen';
    document.body.appendChild(container);
    this.bootContainer = container;

    this.bootContainer.innerHTML = `
      <div class="boot-viewport">
        <!-- 4:3 Vintage Display Container -->
        <div class="boot-screen-box">
          <video id="bootVideo" class="boot-video" playsinline webkit-playsinline></video>

          <!-- Notification / Control Hint Bar -->
          <div id="bootBanner" class="boot-banner">
            <span class="unmute-badge">🔇 Muted • Click anywhere to unmute</span>
            <span class="skip-badge">Press <kbd>ESC</kbd> to skip</span>
          </div>

          <!-- Interactive 2x2 Main Menu -->
          <div id="bootMenu" class="boot-menu hidden">
            <div class="boot-menu-title">
              <h2>DREAMS TO REALITY</h2>
              <span class="menu-sub">CRYO INTERACTIVE ENTERTAINMENT • 1997</span>
            </div>

            <div class="menu-grid-2x2">
              <!-- Item 0: NEW GAME (Top-Left) -->
              <div class="menu-cell" data-index="0">
                <div class="bracket-frame">
                  <img class="bracket tl" src="/ui/menu/bracket_uplf.png" alt="" />
                  <img class="bracket tr" src="/ui/menu/bracket_uprg.png" alt="" />
                  <img class="bracket bl" src="/ui/menu/bracket_dnlf.png" alt="" />
                  <img class="bracket br" src="/ui/menu/bracket_dnrg.png" alt="" />
                </div>
                <div class="menu-label-wrapper">
                  <img class="menu-sprite" src="/ui/menu/title_new_game_active.png" alt="NEW GAME" />
                  <span class="menu-text">NEW GAME</span>
                </div>
              </div>

              <!-- Item 1: LOAD A GAME (Top-Right) -->
              <div class="menu-cell" data-index="1">
                <div class="bracket-frame">
                  <img class="bracket tl" src="/ui/menu/bracket_uplfna.png" alt="" />
                  <img class="bracket tr" src="/ui/menu/bracket_uprgna.png" alt="" />
                  <img class="bracket bl" src="/ui/menu/bracket_dnlfna.png" alt="" />
                  <img class="bracket br" src="/ui/menu/bracket_dnrgna.png" alt="" />
                </div>
                <div class="menu-label-wrapper">
                  <img class="menu-sprite" src="/ui/menu/title_load_game_normal.png" alt="LOAD A GAME" />
                  <span class="menu-text">LOAD A GAME</span>
                </div>
              </div>

              <!-- Item 2: OPTIONS (Bottom-Left) -->
              <div class="menu-cell" data-index="2">
                <div class="bracket-frame">
                  <img class="bracket tl" src="/ui/menu/bracket_uplfna.png" alt="" />
                  <img class="bracket tr" src="/ui/menu/bracket_uprgna.png" alt="" />
                  <img class="bracket bl" src="/ui/menu/bracket_dnlfna.png" alt="" />
                  <img class="bracket br" src="/ui/menu/bracket_dnrgna.png" alt="" />
                </div>
                <div class="menu-label-wrapper">
                  <img class="menu-sprite" src="/ui/menu/title_options_normal.png" alt="OPTIONS" />
                  <span class="menu-text">OPTIONS</span>
                </div>
              </div>

              <!-- Item 3: QUIT (Bottom-Right) -->
              <div class="menu-cell" data-index="3">
                <div class="bracket-frame">
                  <img class="bracket tl" src="/ui/menu/bracket_uplfna.png" alt="" />
                  <img class="bracket tr" src="/ui/menu/bracket_uprgna.png" alt="" />
                  <img class="bracket bl" src="/ui/menu/bracket_dnlfna.png" alt="" />
                  <img class="bracket br" src="/ui/menu/bracket_dnrgna.png" alt="" />
                </div>
                <div class="menu-label-wrapper">
                  <img class="menu-sprite" src="/ui/menu/title_quit_normal.png" alt="QUIT" />
                  <span class="menu-text">QUIT</span>
                </div>
              </div>
            </div>

            <div class="menu-controls-hint">
              <span><kbd>▲</kbd><kbd>▼</kbd><kbd>◄</kbd><kbd>►</kbd> Navigate</span>
              <span><kbd>ENTER</kbd> / <kbd>SPACE</kbd> Select</span>
              <span>Mouse Hover & Click supported</span>
            </div>
          </div>

          <!-- Save Game Browser Submenu -->
          <div id="bootSaveBrowser" class="boot-save-browser hidden">
            <div class="save-browser-header">
              <h3>LOAD A GAME</h3>
              <p>Select saved reality slot</p>
            </div>
            <div class="save-slot-list">
              <div class="save-slot empty"><span class="slot-num">1.</span> Empty</div>
              <div class="save-slot empty"><span class="slot-num">2.</span> Empty</div>
              <div class="save-slot empty"><span class="slot-num">3.</span> Empty</div>
              <div class="save-slot empty"><span class="slot-num">4.</span> Empty</div>
              <div class="save-slot empty"><span class="slot-num">5.</span> Empty</div>
              <div class="save-slot empty"><span class="slot-num">6.</span> Empty</div>
              <div class="save-slot empty"><span class="slot-num">7.</span> Empty</div>
            </div>
            <button id="closeSaveBrowserBtn" class="boot-action-btn secondary">Back to Menu (ESC)</button>
          </div>

          <!-- Quit Confirmation Dialog -->
          <div id="bootQuitModal" class="boot-quit-modal hidden">
            <div class="quit-box">
              <h3>QUIT DREAMS TO REALITY?</h3>
              <p>Would you like to enter 3D Viewer Mode or resume the boot menu?</p>
              <div class="quit-actions">
                <button id="resumeMenuBtn" class="boot-action-btn primary">Resume Menu</button>
                <button id="enterViewerBtn" class="boot-action-btn danger">Enter 3D Viewer</button>
              </div>
            </div>
          </div>

          <!-- In-Engine 15s Transition Countdown -->
          <div id="bootTransition" class="boot-transition hidden">
            <div class="transition-fx"></div>
            <div class="transition-content">
              <span class="transition-title">DIMENSIONAL TRANSITION</span>
              <p class="transition-desc">Shifting consciousness into the Dreamworld...</p>
              <div id="transitionTimer" class="transition-timer">15.0s</div>
              <span class="transition-hint">Press <kbd>ESC</kbd> to skip transition</span>
            </div>
          </div>

          <!-- Authentic 1997 Loading Screen -->
          <div id="bootLoading" class="boot-loading hidden">
            <div class="loading-content">
              <div class="loading-spinner"></div>
              <h2 class="loading-prompt">Please wait while loading ...</h2>
              <span class="loading-sub">Manifest: LISTL1.TXT • H18ANGKR.DSN</span>
            </div>
          </div>
        </div>
      </div>
    `;

    this.videoEl = this.bootContainer.querySelector('#bootVideo') as HTMLVideoElement;
    this.bannerEl = this.bootContainer.querySelector('#bootBanner') as HTMLElement;
    this.menuEl = this.bootContainer.querySelector('#bootMenu') as HTMLElement;
    this.menuItems = Array.from(this.bootContainer.querySelectorAll('.menu-cell'));
    this.saveBrowserEl = this.bootContainer.querySelector('#bootSaveBrowser') as HTMLElement;
    this.quitModalEl = this.bootContainer.querySelector('#bootQuitModal') as HTMLElement;
    this.transitionEl = this.bootContainer.querySelector('#bootTransition') as HTMLElement;
    this.transitionTimerEl = this.bootContainer.querySelector('#transitionTimer') as HTMLElement;
    this.loadingEl = this.bootContainer.querySelector('#bootLoading') as HTMLElement;
  }

  private bindEvents(): void {
    // Unmute on first click/key
    const handleUnmute = () => {
      this.viewer.audio.unmute();
      this.videoEl.muted = false;
      this.syncBannerState();
    };
    window.addEventListener('pointerdown', handleUnmute);
    window.addEventListener('keydown', handleUnmute);

    // ESC key handling
    window.addEventListener('keydown', (e) => {
      if (this.currentStep === 'in_game' || this.isDestroyed) return;

      if (e.key === 'Escape') {
        e.preventDefault();
        this.handleEscKey();
      } else if (this.currentStep === 'menu') {
        this.handleMenuKeyboard(e);
      }
    });

    // Mouse navigation for 2x2 menu
    this.menuItems.forEach((cell) => {
      const idx = Number(cell.getAttribute('data-index') ?? 0);
      cell.addEventListener('mouseenter', () => {
        if (this.currentStep === 'menu' && this.selectedMenuIndex !== idx) {
          this.setMenuSelection(idx);
          this.viewer.audio.playMenuMove();
        }
      });

      cell.addEventListener('click', () => {
        if (this.currentStep === 'menu') {
          this.setMenuSelection(idx);
          this.confirmMenuSelection();
        }
      });
    });

    // Submenu Buttons
    const closeSaveBtn = this.bootContainer.querySelector('#closeSaveBrowserBtn');
    closeSaveBtn?.addEventListener('click', () => {
      this.hideSubmenus();
    });

    const resumeBtn = this.bootContainer.querySelector('#resumeMenuBtn');
    resumeBtn?.addEventListener('click', () => {
      this.hideSubmenus();
    });

    const enterViewerBtn = this.bootContainer.querySelector('#enterViewerBtn');
    enterViewerBtn?.addEventListener('click', () => {
      this.quitToViewer();
    });
  }

  public async startBootSequence(initPromise?: Promise<void>): Promise<void> {
    this.isDestroyed = false;
    this.isInGame = false;
    if (initPromise) {
      this.initPromise = initPromise;
    }
    this.viewer.audio.stopMusic();
    this.bootContainer.classList.remove('hidden');
    // Hide main HUD until in-game
    const hud = document.getElementById('hud');
    if (hud) hud.style.display = 'none';

    // Start with Intro Video
    await this.stepIntro();
  }

  private syncBannerState(): void {
    const isMuted = this.viewer.audio.isMuted;
    const unmuteBadge = this.bannerEl.querySelector('.unmute-badge') as HTMLElement;
    if (unmuteBadge) {
      unmuteBadge.style.display = isMuted ? 'inline-block' : 'none';
    }
  }

  // Phase 1: Intro Movie
  private async stepIntro(): Promise<void> {
    this.currentStep = 'intro';
    this.hideAllOverlays();
    this.syncBannerState();
    this.bannerEl.classList.remove('hidden');

    this.videoEl.src = '/api/assets/video/cutscenes/intro_d1.mp4';
    this.videoEl.loop = false;
    this.videoEl.muted = this.viewer.audio.isMuted;

    try {
      await this.videoEl.play();
    } catch {
      // Autoplay fallback: muted
      this.videoEl.muted = true;
      void this.videoEl.play().catch(() => {});
    }

    this.videoEl.onended = () => {
      if (this.currentStep === 'intro') {
        void this.stepWarp();
      }
    };
  }

  // Phase 2: Warp Animation
  private async stepWarp(): Promise<void> {
    this.currentStep = 'warp';
    this.videoEl.src = '/api/assets/video/cutscenes/generic.mp4';
    this.videoEl.loop = false;
    this.videoEl.muted = this.viewer.audio.isMuted;

    try {
      await this.videoEl.play();
    } catch {
      this.videoEl.muted = true;
      void this.videoEl.play().catch(() => {});
    }

    this.videoEl.onended = () => {
      if (this.currentStep === 'warp') {
        void this.stepMenu();
      }
    };
  }

  // Phase 3: Main Menu
  private async stepMenu(): Promise<void> {
    this.currentStep = 'menu';
    this.hideAllOverlays();

    // Loop generic.mp4 under menu
    this.videoEl.src = '/api/assets/video/cutscenes/generic.mp4';
    this.videoEl.loop = true;
    this.videoEl.muted = true; // Video muted, music plays
    void this.videoEl.play().catch(() => {});

    // Start CD Track 13
    void this.viewer.audio.playTrack('/api/assets/audio/music/d2_track13.opus', true, 0.6);

    this.menuEl.classList.remove('hidden');
    this.bannerEl.classList.remove('hidden');
    this.setMenuSelection(0);
  }

  private setMenuSelection(index: number): void {
    this.selectedMenuIndex = (index + 4) % 4;

    const titles = ['new_game', 'load_game', 'options', 'quit'];

    this.menuItems.forEach((cell, idx) => {
      const isSelected = idx === this.selectedMenuIndex;
      cell.classList.toggle('selected', isSelected);

      // Brackets
      const tl = cell.querySelector('.bracket.tl') as HTMLImageElement;
      const tr = cell.querySelector('.bracket.tr') as HTMLImageElement;
      const bl = cell.querySelector('.bracket.bl') as HTMLImageElement;
      const br = cell.querySelector('.bracket.br') as HTMLImageElement;

      if (tl && tr && bl && br) {
        tl.src = isSelected ? '/ui/menu/bracket_uplf.png' : '/ui/menu/bracket_uplfna.png';
        tr.src = isSelected ? '/ui/menu/bracket_uprg.png' : '/ui/menu/bracket_uprgna.png';
        bl.src = isSelected ? '/ui/menu/bracket_dnlf.png' : '/ui/menu/bracket_dnlfna.png';
        br.src = isSelected ? '/ui/menu/bracket_dnrg.png' : '/ui/menu/bracket_dnrgna.png';
      }

      // Title sprite
      const sprite = cell.querySelector('.menu-sprite') as HTMLImageElement;
      if (sprite) {
        const state = isSelected ? 'active' : 'normal';
        sprite.src = `/ui/menu/title_${titles[idx]}_${state}.png`;
      }
    });
  }

  private handleMenuKeyboard(e: KeyboardEvent): void {
    if (this.currentStep !== 'menu') return;

    if (e.key === 'ArrowUp' || e.key === 'KeyW') {
      e.preventDefault();
      // Up/Down: 0 <-> 2, 1 <-> 3
      const next = this.selectedMenuIndex === 2 ? 0 : this.selectedMenuIndex === 3 ? 1 : this.selectedMenuIndex === 0 ? 2 : 3;
      this.setMenuSelection(next);
      this.viewer.audio.playMenuMove();
    } else if (e.key === 'ArrowDown' || e.key === 'KeyS') {
      e.preventDefault();
      const next = this.selectedMenuIndex === 0 ? 2 : this.selectedMenuIndex === 1 ? 3 : this.selectedMenuIndex === 2 ? 0 : 1;
      this.setMenuSelection(next);
      this.viewer.audio.playMenuMove();
    } else if (e.key === 'ArrowLeft' || e.key === 'KeyA') {
      e.preventDefault();
      // Left/Right: 0 <-> 1, 2 <-> 3
      const next = this.selectedMenuIndex === 1 ? 0 : this.selectedMenuIndex === 3 ? 2 : this.selectedMenuIndex === 0 ? 1 : 3;
      this.setMenuSelection(next);
      this.viewer.audio.playMenuMove();
    } else if (e.key === 'ArrowRight' || e.key === 'KeyD') {
      e.preventDefault();
      const next = this.selectedMenuIndex === 0 ? 1 : this.selectedMenuIndex === 2 ? 3 : this.selectedMenuIndex === 1 ? 0 : 2;
      this.setMenuSelection(next);
      this.viewer.audio.playMenuMove();
    } else if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      this.confirmMenuSelection();
    }
  }

  private confirmMenuSelection(): void {
    this.viewer.audio.playMenuConfirm();

    // Flash pressed title sprite
    const titles = ['new_game', 'load_game', 'options', 'quit'];
    const activeCell = this.menuItems[this.selectedMenuIndex];
    const sprite = activeCell?.querySelector('.menu-sprite') as HTMLImageElement;
    if (sprite) {
      sprite.src = `/ui/menu/title_${titles[this.selectedMenuIndex]}_pressed.png`;
    }

    setTimeout(() => {
      switch (this.selectedMenuIndex) {
        case 0: // NEW GAME
          void this.stepTransition();
          break;
        case 1: // LOAD A GAME
          this.openSaveBrowser();
          break;
        case 2: // OPTIONS
          this.openOptions();
          break;
        case 3: // QUIT
          this.openQuitModal();
          break;
      }
    }, 180);
  }

  private openSaveBrowser(): void {
    this.currentStep = 'save_browser';
    this.saveBrowserEl.classList.remove('hidden');
  }

  private openOptions(): void {
    // Open Graphics Lab panel via UIManager
    this.ui.graphicsPanel.classList.remove('hidden');
    document.getElementById('graphicsBtn')?.classList.add('active');
  }

  private openQuitModal(): void {
    this.currentStep = 'quit_prompt';
    this.quitModalEl.classList.remove('hidden');
  }

  private hideSubmenus(): void {
    this.saveBrowserEl.classList.add('hidden');
    this.quitModalEl.classList.add('hidden');
    this.currentStep = 'menu';
    this.setMenuSelection(this.selectedMenuIndex);
  }

  // Phase 4: 15s In-Engine Transition
  private async stepTransition(): Promise<void> {
    this.currentStep = 'transition';
    this.hideAllOverlays();
    this.viewer.audio.stopMusic(800); // fade out track 13

    this.transitionEl.classList.remove('hidden');
    this.transitionRemaining = 15.0;

    const startTime = performance.now();

    const updateTick = () => {
      if (this.currentStep !== 'transition') return;

      const elapsed = (performance.now() - startTime) / 1000;
      this.transitionRemaining = Math.max(0, 15.0 - elapsed);
      this.transitionTimerEl.textContent = `${this.transitionRemaining.toFixed(1)}s`;

      if (this.transitionRemaining <= 0) {
        void this.stepElder();
      } else {
        this.transitionRaf = requestAnimationFrame(updateTick);
      }
    };

    this.transitionRaf = requestAnimationFrame(updateTick);
  }

  // Phase 5: Elder Talking Head Briefing
  private async stepElder(): Promise<void> {
    if (this.transitionRaf) {
      cancelAnimationFrame(this.transitionRaf);
      this.transitionRaf = undefined;
    }

    this.currentStep = 'elder';
    this.hideAllOverlays();
    this.bannerEl.classList.remove('hidden');

    this.videoEl.src = '/api/assets/video/cutscenes/tete_e~1.mp4';
    this.videoEl.loop = false;
    this.videoEl.muted = this.viewer.audio.isMuted;

    try {
      await this.videoEl.play();
    } catch {
      this.videoEl.muted = true;
      void this.videoEl.play().catch(() => {});
    }

    this.videoEl.onended = () => {
      if (this.currentStep === 'elder') {
        void this.stepLoading();
      }
    };
  }

  // Phase 6: Loading Screen
  private async stepLoading(): Promise<void> {
    if (this.currentStep === 'loading' || this.isInGame) return;
    this.currentStep = 'loading';
    this.hideAllOverlays();
    this.loadingEl.classList.remove('hidden');

    // Stop elder video immediately so it doesn't keep running in background
    this.videoEl.pause();
    this.videoEl.removeAttribute('src');
    this.videoEl.load();

    // Prepare scene loading: await background initData if in progress, or load h18angkr if missing
    const loadPromise = (async () => {
      if (this.initPromise) {
        try {
          await this.initPromise;
        } catch (e) {
          console.warn('Initial data load error:', e);
        }
      }
      if (this.viewer.currentSceneId !== 'h18angkr' || this.viewer.currentAssetMeshes.length === 0) {
        try {
          await this.viewer.loadAsset('scenes', 'h18angkr.gltf');
        } catch (err) {
          console.warn('Could not load h18angkr.gltf:', err);
        }
      }
    })();

    // Authentic retro loading screen display duration (1.5s)
    const minDisplayPromise = new Promise<void>((resolve) => {
      setTimeout(resolve, 1500);
    });

    // Timeout guard so the screen NEVER hangs indefinitely (5s max)
    const timeoutPromise = new Promise<void>((resolve) => {
      setTimeout(resolve, 5000);
    });

    try {
      await Promise.race([
        Promise.all([loadPromise, minDisplayPromise]),
        timeoutPromise,
      ]);
    } catch (err) {
      console.warn('Error during loading step:', err);
    }

    if (this.currentStep === 'loading' && !this.isInGame) {
      await this.stepInGame();
    }
  }

  // Phase 7: First Map (Ile d'Angkor) In-Game
  public async stepInGame(): Promise<void> {
    if (this.isInGame) return;
    this.isInGame = true;
    this.currentStep = 'in_game';
    this.hideAllOverlays();
    this.bootContainer.classList.add('hidden');

    // Stop video
    this.videoEl.pause();
    this.videoEl.removeAttribute('src');
    this.videoEl.load();

    // Start CD track 2 for Angkor
    void this.viewer.audio.playTrack('/api/assets/audio/music/d1_track02.opus', true, 0.55);

    // Reveal main HUD
    const hud = document.getElementById('hud');
    if (hud) hud.style.display = 'block';

    // Enter Duncan mode cleanly
    await this.ui.enterDuncanMode();
  }

  private handleEscKey(): void {
    switch (this.currentStep) {
      case 'intro':
        // Skip intro to warp
        void this.stepWarp();
        break;
      case 'warp':
        // Skip warp directly to menu
        void this.stepMenu();
        break;
      case 'menu':
        // ESC in menu brings up quit prompt
        this.openQuitModal();
        break;
      case 'save_browser':
      case 'quit_prompt':
        this.hideSubmenus();
        break;
      case 'transition':
        // Skip 15s transition directly to Elder briefing
        if (this.transitionRaf) cancelAnimationFrame(this.transitionRaf);
        void this.stepElder();
        break;
      case 'elder':
        // Skip elder briefing to loading
        void this.stepLoading();
        break;
      case 'loading':
        // Skip loading directly to in-game
        void this.stepInGame();
        break;
    }
  }

  private quitToViewer(): void {
    void this.stepInGame();
  }

  private hideAllOverlays(): void {
    this.menuEl.classList.add('hidden');
    this.saveBrowserEl.classList.add('hidden');
    this.quitModalEl.classList.add('hidden');
    this.transitionEl.classList.add('hidden');
    this.loadingEl.classList.add('hidden');
    this.bannerEl.classList.add('hidden');
  }

  public destroy(): void {
    this.isDestroyed = true;
    if (this.transitionRaf) cancelAnimationFrame(this.transitionRaf);
    this.videoEl.pause();
    this.videoEl.removeAttribute('src');
    this.bootContainer.remove();
  }
}
