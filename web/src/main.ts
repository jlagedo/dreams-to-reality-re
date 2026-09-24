import { DreamsViewer } from './viewer';
import { UIManager } from './ui';
import { StartupController } from './boot/startup';

async function startApp(): Promise<void> {
  const canvas = document.getElementById('renderCanvas') as HTMLCanvasElement;
  if (!canvas) {
    console.error('Could not find #renderCanvas');
    return;
  }

  let ui: UIManager | null = null;

  const viewer = new DreamsViewer(canvas, {
    onStatusChange: (msg) => ui?.setStatus(msg),
    onStatsChange: (count) => ui?.setMeshCount(count),
    onPortalTransitionNotify: (msg) => ui?.showPortalBanner(msg),
    onSceneChange: (sceneId) => ui?.syncSelectedScene(sceneId),
    onEntitySelected: (entity) => ui?.displayEntityInspection(entity),
    onAnimFrameUpdate: (frame, maxFrame, pose) => ui?.updateAnimScrubber(frame, maxFrame, pose),
    onProjectDataLoaded: (project) => ui?.updateProjectDebugHUD(project),
  });

  ui = new UIManager(viewer, canvas);
  const initPromise = ui.initData();

  const boot = new StartupController(viewer, ui);
  ui.setStartupController(boot);
  void boot.startBootSequence(initPromise);
}

if (document.readyState === 'loading') {
  window.addEventListener('DOMContentLoaded', () => void startApp());
} else {
  void startApp();
}

if (import.meta.hot) {
  import.meta.hot.accept(() => {
    window.location.reload();
  });
}
