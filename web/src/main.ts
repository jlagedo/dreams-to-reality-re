import { DreamsViewer } from './viewer';
import { UIManager } from './ui';

window.addEventListener('DOMContentLoaded', async () => {
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
  });

  ui = new UIManager(viewer, canvas);
  await ui.initData();
});
