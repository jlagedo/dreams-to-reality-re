export class AudioManager {
  private musicAudio: HTMLAudioElement;
  private sfxAudio: HTMLAudioElement;
  public isMusicPlaying: boolean = false;

  constructor() {
    this.musicAudio = new Audio('/api/assets/audio/music/d1_track02.flac');
    this.musicAudio.loop = true;
    this.musicAudio.volume = 0.55;

    this.sfxAudio = new Audio('/api/assets/audio/sfx/sfx_001.flac');
    this.sfxAudio.volume = 0.7;
  }

  public toggleMusic(): boolean {
    if (this.isMusicPlaying) {
      this.musicAudio.pause();
      this.isMusicPlaying = false;
    } else {
      this.musicAudio.play().then(() => {
        this.isMusicPlaying = true;
      }).catch((err) => {
        console.warn('Autoplay prevented:', err);
      });
    }
    return this.isMusicPlaying;
  }

  public playPortalSfx(): void {
    this.sfxAudio.currentTime = 0;
    this.sfxAudio.play().catch(() => {});
  }
}
