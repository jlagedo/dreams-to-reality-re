import { audioFile, loadIndex } from './content';

export class AudioManager {
  private musicAudio: HTMLAudioElement;
  private sfxAudio: HTMLAudioElement;
  private menuMoveAudio: HTMLAudioElement;
  private menuConfirmAudio: HTMLAudioElement;

  public isMusicPlaying: boolean = false;
  public isMuted: boolean = true; // Requirement: initiate muted, unmute on click/interaction
  private targetMusicVolume: number = 0.55;
  private fadeInterval?: number;

  constructor() {
    this.musicAudio = new Audio();
    this.musicAudio.loop = true;
    this.musicAudio.volume = 0; // muted initially

    this.sfxAudio = new Audio();
    this.sfxAudio.volume = 0;

    this.menuMoveAudio = new Audio();
    this.menuMoveAudio.volume = 0;

    this.menuConfirmAudio = new Audio();
    this.menuConfirmAudio.volume = 0;

    // FSB.DAT clips: 1 for the portal (a viewer choice); 9/10 for menu move and
    // confirm, assuming the menu's sound events 9 and 10 (docs/boot-sequence.md)
    // index the bank directly - not verified.
    void loadIndex().then((index) => {
      const sfx = (stem: string) => audioFile(index.audio.sfx, stem);
      for (const [element, stem] of [
        [this.sfxAudio, 'sfx_001'], [this.menuMoveAudio, 'sfx_009'], [this.menuConfirmAudio, 'sfx_010'],
      ] as const) {
        const url = sfx(stem);
        if (url) element.src = url;
      }
    }).catch((error) => console.warn('Sound effects unavailable:', error));

    // Listen for first user click / key to unmute audio automatically
    const unmuteHandler = () => {
      this.unmute();
      window.removeEventListener('pointerdown', unmuteHandler);
      window.removeEventListener('keydown', unmuteHandler);
    };
    window.addEventListener('pointerdown', unmuteHandler);
    window.addEventListener('keydown', unmuteHandler);
  }

  public unmute(): void {
    if (!this.isMuted) return;
    this.isMuted = false;
    this.musicAudio.volume = this.targetMusicVolume;
    this.sfxAudio.volume = 0.7;
    this.menuMoveAudio.volume = 0.8;
    this.menuConfirmAudio.volume = 0.8;
  }

  public mute(): void {
    this.isMuted = true;
    this.musicAudio.volume = 0;
    this.sfxAudio.volume = 0;
    this.menuMoveAudio.volume = 0;
    this.menuConfirmAudio.volume = 0;
  }

  public playTrack(url: string, loop: boolean = true, volume: number = 0.55): Promise<void> {
    if (this.fadeInterval) {
      clearInterval(this.fadeInterval);
      this.fadeInterval = undefined;
    }

    this.targetMusicVolume = volume;
    const currentSrc = this.musicAudio.getAttribute('src');
    if (currentSrc !== url) {
      this.musicAudio.pause();
      this.musicAudio.src = url;
      this.musicAudio.load();
    }

    this.musicAudio.loop = loop;
    this.musicAudio.volume = this.isMuted ? 0 : this.targetMusicVolume;

    return this.musicAudio.play()
      .then(() => {
        this.isMusicPlaying = true;
      })
      .catch((err) => {
        console.warn('Audio play prevented or deferred:', err);
      });
  }

  public stopMusic(fadeDurationMs: number = 0): void {
    if (this.fadeInterval) {
      clearInterval(this.fadeInterval);
      this.fadeInterval = undefined;
    }

    if (fadeDurationMs <= 0 || this.isMuted || !this.isMusicPlaying) {
      this.musicAudio.pause();
      this.isMusicPlaying = false;
      return;
    }

    const startVol = this.musicAudio.volume;
    const stepInterval = 50;
    const steps = fadeDurationMs / stepInterval;
    const decrement = startVol / steps;

    this.fadeInterval = window.setInterval(() => {
      if (this.musicAudio.volume > decrement) {
        this.musicAudio.volume -= decrement;
      } else {
        this.musicAudio.pause();
        this.musicAudio.volume = this.isMuted ? 0 : this.targetMusicVolume;
        this.isMusicPlaying = false;
        clearInterval(this.fadeInterval);
        this.fadeInterval = undefined;
      }
    }, stepInterval);
  }

  public toggleMusic(): boolean {
    if (this.isMusicPlaying) {
      this.stopMusic(200);
      this.isMusicPlaying = false;
    } else {
      this.unmute();
      const track = this.musicAudio.getAttribute('src') || this.defaultTrack;
      if (track) this.playTrack(track, true);
    }
    return this.isMusicPlaying;
  }

  /** What the music toggle plays when nothing has played yet: the level's track. */
  public defaultTrack: string | null = null;

  public playMenuMove(): void {
    if (this.isMuted) return;
    this.menuMoveAudio.currentTime = 0;
    this.menuMoveAudio.play().catch(() => {});
  }

  public playMenuConfirm(): void {
    if (this.isMuted) return;
    this.menuConfirmAudio.currentTime = 0;
    this.menuConfirmAudio.play().catch(() => {});
  }

  public playPortalSfx(): void {
    if (this.isMuted) return;
    this.sfxAudio.currentTime = 0;
    this.sfxAudio.play().catch(() => {});
  }
}
