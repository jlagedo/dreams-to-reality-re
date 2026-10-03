/* Dreams to Reality, browser demo: page behaviour that is only presentation.
 *
 * loader.js owns the game (download, start, audio, saves). This file reads
 * window.dreamsPage and the 'dreams:phase' / 'dreams:error' / 'dreams:audio'
 * events, and owns: the toolbar and its status text, the controls dialog (the
 * "?" key), the scale mode (fit or pixel-perfect), the "click to take control"
 * veil, the key shield (keys go to the game only while it has focus), fullscreen
 * toolbar hiding, loading tips, plain-language error help, the start screen's
 * optional picture and the optional screenshot strip. No framework. Notes: recomp/web/NOTES-design.md.
 */
(() => {
'use strict';
const page = window.dreamsPage;
if (!page) return;

const $ = (id) => document.getElementById(id);
const el = {
  player: $('player'), stage: $('stage'), canvas: $('canvas'), overlay: $('overlay'), veil: $('veil'),
  veilTitle: $('veil-title'), veilSub: $('veil-sub'), keyhint: $('keyhint'), hud: $('hud'),
  state: $('state-text'), tip: $('tip'), play: $('play'), status: $('status'), scale: $('scale'),
  esc: $('esc'), help: $('help'), reload: $('reload'), dialog: $('controls-dialog'),
  dialogBody: $('controls-dialog-body'), errorHelp: $('error-help'), errorText: $('error-text'),
  errorTitle: $('error-title'), clearRetry: $('clear-retry'), copyLog: $('copylog'), clearData: $('clear-data'),
};
const MIB = 1024 * 1024;
const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
const mib = (n) => (n / MIB >= 10 ? (n / MIB).toFixed(0) : (n / MIB).toFixed(1)) + ' MiB';

/* ---- environment ------------------------------------------------------------------------------ */
if (matchMedia('(pointer: coarse) and (hover: none)').matches) document.body.classList.add('touch');

/* ---- controls: one source, three places ------------------------------------------------------- */
(function buildControls() {
  const src = $('controls-src');
  // Arrow keys get a spoken name; the glyph itself is hidden from assistive tech.
  const names = { '↑': 'Up arrow', '↓': 'Down arrow', '←': 'Left arrow', '→': 'Right arrow' };
  src.querySelectorAll('kbd.arr').forEach((k) => {
    const g = k.textContent.trim();
    k.innerHTML = '<span aria-hidden="true">' + g + '</span><span class="sr">' + (names[g] || g) + '</span>';
  });
  const side = $('side-controls');
  const mk = (into) => {
    const c = src.cloneNode(true);
    c.removeAttribute('id');
    c.querySelectorAll('[id]').forEach((n) => n.removeAttribute('id'));
    into.appendChild(c);
  };
  mk(side);
  mk(el.dialogBody);
  const notes = $('controls-notes');
  if (notes) el.dialogBody.appendChild(notes.cloneNode(true)).removeAttribute('id');
})();

/* ---- key capture and shield ------------------------------------------------------------------- */
// Keys belong to the game only while the canvas has focus and no dialog is open. Otherwise the page
// gets them back: they are not preventDefault'ed (loader.js asks page.captureKeys) and are hidden
// from the engine (SDL listens on window), so arrows scroll the page and Space presses a button.
const held = new Map();   // code -> {key, code, keyCode}: keys the engine thinks are down
const inHud = () => el.hud.contains(document.activeElement);
const dialogOpen = () => el.dialog.open;
const gameFocused = () => document.activeElement === el.canvas && document.hasFocus();
page.captureKeys = () => gameFocused() && !dialogOpen();

function releaseHeld() {
  // The engine must not keep a key down that the page swallowed the release of.
  for (const k of held.values()) {
    const e = new KeyboardEvent('keyup', { key: k.key, code: k.code, keyCode: k.keyCode, which: k.keyCode, bubbles: true, cancelable: true, composed: true });
    e.__dreams = true;
    el.canvas.dispatchEvent(e);
  }
  held.clear();
}
const running = () => page.phase === 'running' || page.phase === 'starting';
addEventListener('keydown', (e) => {
  if (e.__dreams) return;
  const typing = e.target instanceof HTMLInputElement && e.target.type !== 'range';
  // "?" opens the controls (never reaches the game; the game does not use it).
  if (e.key === '?' && !typing && !e.ctrlKey && !e.metaKey && !e.altKey) {
    e.preventDefault(); e.stopImmediatePropagation();
    if (dialogOpen()) closeDialog(); else openDialog();
    return;
  }
  if (!running()) return;
  // Shift+Tab: the way out of the game for keyboard-only users (the game itself uses neither Shift nor Shift+Tab).
  if (e.key === 'Tab' && e.shiftKey && gameFocused() && !dialogOpen()) {
    e.preventDefault(); e.stopImmediatePropagation();
    releaseHeld();
    el.esc.focus();
    return;
  }
  // Esc on the toolbar goes back to the game instead of reaching it.
  if (e.key === 'Escape' && inHud() && !dialogOpen()) {
    e.preventDefault(); e.stopImmediatePropagation();
    el.canvas.focus();
    return;
  }
  if (!page.captureKeys()) { e.stopImmediatePropagation(); return; }
  held.set(e.code, { key: e.key, code: e.code, keyCode: e.keyCode });
}, true);
addEventListener('keyup', (e) => {
  if (e.__dreams) return;
  if (running() && !page.captureKeys() && !held.has(e.code)) { e.stopImmediatePropagation(); return; }
  held.delete(e.code);
}, true);

/* A click on a toolbar button tapping a key into the game (the Menu button sends Esc). */
function tapKey(key, code, keyCode, ms = 140) {
  el.canvas.focus();
  const mk = (type) => {
    const e = new KeyboardEvent(type, { key, code, keyCode, which: keyCode, bubbles: true, cancelable: true, composed: true });
    e.__dreams = true;
    return e;
  };
  el.canvas.dispatchEvent(mk('keydown'));
  setTimeout(() => el.canvas.dispatchEvent(mk('keyup')), ms);
}
el.esc.addEventListener('click', () => { if (page.phase === 'running') tapKey('Escape', 'Escape', 27); });

/* ---- the controls dialog ---------------------------------------------------------------------- */
let returnFocus = null;
function openDialog() {
  if (el.dialog.open) return;
  returnFocus = document.activeElement;
  releaseHeld();
  el.dialog.showModal();
  update();
}
function closeDialog() { if (el.dialog.open) el.dialog.close(); }
el.dialog.addEventListener('close', () => {
  const back = page.phase === 'running' || page.phase === 'starting' ? el.canvas : returnFocus;
  if (back && back.focus) back.focus();
  update();
});
el.dialog.addEventListener('click', (e) => { if (e.target === el.dialog) closeDialog(); });   // backdrop click
$('controls-close').addEventListener('click', closeDialog);
el.help.addEventListener('click', () => (el.dialog.open ? closeDialog() : openDialog()));

/* ---- status line, veil ------------------------------------------------------------------------ */
function stateText() {
  const p = page;
  switch (p.phase) {
    case 'init': case 'checking': return 'Checking your browser';
    case 'manifest': return 'Reading the demo manifest';
    case 'downloading': {
      const pct = p.bytesTotal ? Math.floor((p.bytesDone / p.bytesTotal) * 100) : 0;
      return 'Downloading the demo ' + pct + '% (' + mib(p.bytesDone) + ' of ' + mib(p.bytesTotal) + ')';
    }
    case 'ready': return 'Ready. Press play';
    case 'starting': return el.status.textContent || 'Starting the game';
    case 'running':
      if (document.hidden || !document.hasFocus()) return 'Paused while the tab is in the background';
      if (dialogOpen()) return 'Controls open';
      if (inHud()) return 'Toolbar has the keys. Esc returns to the game';
      return gameFocused() ? 'Playing. Keys go to the game' : 'Click the game to take control';
    case 'ended': return 'You quit the game';
    case 'error': return 'Stopped';
    default: return '';
  }
}
function update() {
  const t = stateText();
  el.state.textContent = t;
  const showVeil = page.phase === 'running' && !dialogOpen() && !inHud() && !gameFocused();
  if (showVeil) {
    const away = document.hidden || !document.hasFocus();
    el.veilTitle.textContent = away ? 'Paused. Click to resume' : 'Click to take control';
    el.veilSub.textContent = away
      ? 'The game pauses while this window is in the background.'
      : 'Your keys are going to the page, not to the game.';
  }
  el.veil.hidden = !showVeil;
  el.stage.classList.toggle('veiled', showVeil);
  if (page.phase === 'running' && !gameFocused()) {
    // keep the engine from seeing keys that the page now owns
    if (held.size) releaseHeld();
  }
}
el.veil.addEventListener('click', () => { el.canvas.focus(); update(); });
for (const t of ['focusin', 'focusout']) document.addEventListener(t, () => setTimeout(update, 0));
addEventListener('focus', () => setTimeout(update, 0));
addEventListener('blur', () => setTimeout(update, 0));
document.addEventListener('visibilitychange', update);
el.stage.addEventListener('pointerdown', () => { if (page.phase === 'running') el.canvas.focus(); });
setInterval(() => { if (page.phase === 'downloading') update(); }, 400);

/* ---- phase changes ---------------------------------------------------------------------------- */
const tips = [
  'The small flying blue bubbles give you a lot of manna.',
  'When you jump, trust the shadow you throw: it shows where you land.',
  'Read the dialogue properly. It holds the answers to the riddles.',
  'Your magical energy is your most precious possession. Do not waste it.',
  'Explore every place: spells, bonuses and secret passages are hidden in them.',
  'The game saves by itself each time you enter a new level.',
];
let tipTimer = 0, tipIdx = Math.floor(Math.random() * tips.length);
function showTip() {
  el.tip.textContent = 'Tip: ' + tips[tipIdx++ % tips.length];
  if (!reduced) { el.tip.classList.remove('fade'); void el.tip.offsetWidth; el.tip.classList.add('fade'); }
}
function tipsOn(on) {
  clearInterval(tipTimer);
  if (!on) { el.tip.textContent = ''; return; }
  showTip();
  tipTimer = setInterval(showTip, 7000);
}
let hintTimer = 0;
function onPhase() {
  const p = page.phase;
  tipsOn(p === 'downloading' || p === 'manifest' || p === 'ready' || p === 'starting');
  // the polite live region is the overlay's #status (role=status); keep the toolbar text in step too
  if (p === 'running') {
    el.keyhint.classList.add('show');
    clearTimeout(hintTimer);
    hintTimer = setTimeout(() => el.keyhint.classList.remove('show'), 7000);
    if (!document.fullscreenElement) keepInView();
    el.canvas.focus();
  }
  const lbl = el.play.querySelector('.lbl');
  if (lbl) {
    if (p === 'starting') lbl.textContent = 'Starting...';
    else if (p === 'running') lbl.textContent = 'Playing';
    else if (p === 'ended') lbl.textContent = 'Play again';
    else if (page.clicked && p !== 'ready') lbl.textContent = 'Starting when the data is ready...';
    else lbl.textContent = 'Play demo';
  }
  update();
  fit();
}
document.addEventListener('dreams:phase', onPhase);

function keepInView() {
  const r = el.player.getBoundingClientRect();
  if (r.top < 0 || r.bottom > innerHeight) {
    const top = el.player.getBoundingClientRect().top + scrollY - 66;
    scrollTo({ top: Math.max(0, top), behavior: reduced ? 'auto' : 'smooth' });
  }
}
el.play.addEventListener('click', () => { if (!document.fullscreenElement) keepInView(); onPhase(); });

/* ---- errors ----------------------------------------------------------------------------------- */
const HELP = [
  [/cross-origin isolated|SharedArrayBuffer/i, 'This page cannot use threads here',
    ['Open the demo directly in its own tab, not inside another page.',
     'Use a current Chrome, Edge or Firefox on a desktop or laptop.',
     'If you host this page yourself: it must be served over https (or localhost) with the Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy headers that recomp/web/package.py writes.']],
  // before /WebGL/: the engine's text names the WebGL context
  [/context was lost/i, 'The browser reset the graphics card',
    ['Reload the page; your autosave is kept.',
     'If it happens again, close other tabs that use 3D graphics and update your graphics driver.',
     'Pixel-perfect draws the game at its original 640 x 480, which asks less of the graphics card.']],
  [/WebGL/i, 'Your browser has no WebGL 2',
    ['Turn on hardware acceleration in the browser settings and restart it.',
     'Update your graphics driver, then try Chrome, Edge or Firefox.',
     'In Chrome, open chrome://gpu and look for "WebGL2: Hardware accelerated".']],
  [/WebAssembly/i, 'This browser is too old',
    ['Update your browser, or use a current Chrome, Edge or Firefox.']],
  [/out of memory|not enough memory|Cannot enlarge memory|allocation failed|RangeError/i, 'The game ran out of memory',
    ['Close other tabs and heavy programs, then try again.',
     'Use a 64-bit desktop browser; the game needs a few hundred MiB.']],
  [/Checksum|damaged|missing or damaged/i, 'The downloaded data did not verify',
    ['Try again: the file will be downloaded afresh.',
     'If it keeps failing, use "Clear downloaded data and retry". A proxy or flaky connection can damage a download.']],
  [/manifest|HTTP \d|fetch|download|Failed to load|NetworkError|Chunk/i, 'The demo data could not be downloaded',
    ['Check your connection and press Try again. Downloads resume where they stopped.',
     'If you opened the page from another site, the demo host may be down or blocking this page.']],
  [/engine script|dreams\.js|createDreams|engine does not/i, 'The game program is missing or did not load',
    ['Reload the page. If it persists, the site was deployed without the game program.']],
  [/aborted|stopped|fatal|cannot load the game program/i, 'The game stopped',
    ['Reload the page; your autosave is kept.',
     'If it happens again, open the page with ?debug and copy the technical details when reporting it.']],
];
function onError(d) {
  const text = (d.title + ' ' + d.detail);
  const hit = HELP.find((h) => h[0].test(text));
  const frag = document.createDocumentFragment();
  const p = document.createElement('p');
  p.textContent = hit ? hit[1] + '. What you can try:' : 'Something went wrong. What you can try:';
  frag.appendChild(p);
  const ul = document.createElement('ul');
  for (const s of (hit ? hit[2] : ['Reload the page.', 'Use a current Chrome, Edge or Firefox.'])) {
    const li = document.createElement('li'); li.textContent = s; ul.appendChild(li);
  }
  frag.appendChild(ul);
  el.errorHelp.replaceChildren(frag);
  el.clearRetry.style.display = page.cacheKey || /Checksum|damaged|manifest|download/i.test(text) ? '' : 'none';
  if (!d.canRetry) { el.clearRetry.style.display = 'none'; }
  update();
}
document.addEventListener('dreams:error', (e) => onError(e.detail));
el.copyLog.addEventListener('click', async () => {
  const txt = 'Dreams to Reality browser demo\n' + navigator.userAgent + '\nphase: ' + page.phase +
    '\nerror: ' + JSON.stringify(page.error) + '\n\n' + page.log.join('\n');
  try { await navigator.clipboard.writeText(txt); el.copyLog.textContent = 'Copied'; }
  catch (e) { el.copyLog.textContent = 'Copy failed (use ?debug)'; }
});

async function clearData() {
  try {
    for (const n of await caches.keys()) if (n.startsWith('dreams-')) await caches.delete(n);
    await new Promise((res) => { const r = indexedDB.deleteDatabase('/dreams/DATA/GAME'); r.onsuccess = r.onerror = r.onblocked = res; });
  } catch (e) { /* best effort */ }
}
el.clearRetry.addEventListener('click', async () => { await clearData(); location.reload(); });
if (el.clearData) {
  el.clearData.addEventListener('click', async () => {
    if (!confirm('Delete the downloaded demo data and your saved game from this browser?')) return;
    await clearData();
    location.reload();
  });
}
el.reload.addEventListener('click', () => location.reload());

/* ---- scale mode: fit (smooth) or pixel-perfect (whole multiples of 640 x 480) ----------------- */
const GAME_W = 640;
let pixel = localStorage.getItem('dreams.scale') === 'pixel';
page.nativeResolution = () => pixel;   // pixel-perfect shows the game's own 640 x 480 pixels
// Pixel-perfect: nearest-neighbour sampling, and a whole multiple of 640 device pixels when that
// does not shrink the picture below 75% of what fits; otherwise the fitted size, still sharp.
function fit() {
  document.body.classList.toggle('pixel', pixel);
  el.scale.setAttribute('aria-pressed', String(pixel));
  el.player.style.removeProperty('--stage-w');
  delete document.body.dataset.pixelSize;
  if (!pixel) return;
  const w = el.stage.getBoundingClientRect().width;          // what "fit" gives
  const dpr = window.devicePixelRatio || 1;
  const n = Math.floor((w * dpr) / GAME_W + 1e-6);
  if (n >= 1 && (GAME_W * n) / dpr >= 0.75 * w) {
    el.player.style.setProperty('--stage-w', (GAME_W * n) / dpr + 'px');
    document.body.dataset.pixelSize = 'integer';
  } else document.body.dataset.pixelSize = 'fit';
}
el.scale.addEventListener('click', () => {
  pixel = !pixel;
  localStorage.setItem('dreams.scale', pixel ? 'pixel' : 'fit');
  fit();
  page.updateView();
  if (page.phase === 'running') el.canvas.focus();
});
new ResizeObserver(() => { if (pixel) fit(); }).observe(document.documentElement);
document.addEventListener('fullscreenchange', () => { fit(); update(); });

/* ---- fullscreen: hide the toolbar when idle; keep the keys with the game ---------------------- */
let idleTimer = 0;
function wake() {
  el.player.classList.remove('idle');
  clearTimeout(idleTimer);
  if (document.fullscreenElement) idleTimer = setTimeout(() => { if (!inHud()) el.player.classList.add('idle'); }, 2600);
}
el.player.addEventListener('pointermove', wake);
document.addEventListener('fullscreenchange', wake);
el.hud.addEventListener('focusin', wake);
// a pointer drag on the volume slider must not leave the keys on the slider
$('volume').addEventListener('pointerup', () => { if (page.phase === 'running') el.canvas.focus(); });

/* ---- optional screenshots (package.py --shots writes DREAMS_SHOTS into config.js) -------------- */
(function gallery() {
  const shots = window.DREAMS_SHOTS;
  const box = $('shots');
  if (!Array.isArray(shots) || !shots.length || !box) return;
  for (const s of shots) {
    const f = document.createElement('figure');
    const i = document.createElement('img');
    i.src = s.src; i.alt = s.alt || 'Screenshot of the demo'; i.loading = 'lazy'; i.decoding = 'async';
    f.appendChild(i);
    if (s.caption) { const c = document.createElement('figcaption'); c.textContent = s.caption; f.appendChild(c); }
    box.appendChild(f);
  }
  box.hidden = false;
})();

/* ---- the start screen's picture (package.py --poster writes DREAMS_POSTER into config.js) -------- */
(function poster() {
  const box = $('poster');
  if (!box || typeof window.DREAMS_POSTER !== 'string' || !window.DREAMS_POSTER) return;
  // an absolute URL: one in a custom property would resolve against the stylesheet
  box.style.setProperty('--poster', 'url(' + JSON.stringify(new URL(window.DREAMS_POSTER, location.href).href) + ')');
  box.classList.add('photo');
})();

/* ---- start: catch up with what loader.js already did before this file ran --------------------- */
onPhase();
if (page.error) onError({ title: page.error.title, detail: page.error.detail, canRetry: true });
})();
