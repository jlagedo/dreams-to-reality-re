// WINDREAM browser build: main-thread side of the frame relay (runs in the page
// and, harmlessly, in every worker). The guest runs on a pthread, and a
// blocking loop never lets an OffscreenCanvas present, so the host renders on
// an OffscreenCanvas of its own and, after each swap, posts the frame here as
// an ImageBitmap (web_glue.c wd_web_present). The page's canvas has a
// bitmaprenderer context and shows each frame as it arrives.
Module['dreamsFrame'] = function (bmp) {
  var canvas = Module['canvas'];
  if (!canvas) { bmp.close(); return; }
  var ctx = Module['dreamsCtx'] || (Module['dreamsCtx'] = canvas.getContext('bitmaprenderer'));
  if (!ctx) { bmp.close(); return; }
  if (canvas.width !== bmp.width || canvas.height !== bmp.height) {
    canvas.width = bmp.width;
    canvas.height = bmp.height;
  }
  ctx.transferFromImageBitmap(bmp);
  Module['dreamsFrames'] = (Module['dreamsFrames'] | 0) + 1;
};

// The guest thread's canvas. PROXY_TO_PTHREAD would transfer the page's canvas
// to the guest thread, whose frames then never reach the screen; instead an
// OffscreenCanvas of its own is registered under the page canvas's name, so the
// runtime hands that one to the guest thread (SDL's "#canvas" is found there),
// and the page's canvas stays here, a bitmaprenderer target.
var dreamsPrevInit = Module['onRuntimeInitialized'];
Module['onRuntimeInitialized'] = function () {
  if (dreamsPrevInit) dreamsPrevInit();
  if (typeof OffscreenCanvas === 'undefined') {
    err('no OffscreenCanvas: this browser cannot run the game');
    return;
  }
  var id = (Module['canvas'] && Module['canvas'].id) || 'canvas';
  var oc = new OffscreenCanvas(640, 480);
  GL.offscreenCanvases[id] = { offscreenCanvas: oc, canvasSharedPtr: _malloc(12), id: id };
};
