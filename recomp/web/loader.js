/* Dreams to Reality, browser shell. No framework, no build step.
 *
 * On load: check the browser, fetch the demo pack described by manifest.json
 * (chunks in parallel, sha256 checked, kept in the Cache API keyed by the
 * manifest), and wait for the click that unlocks audio and fullscreen. On
 * click it loads the engine (dreams.js, createDreams), writes the pack into
 * the Emscripten file system at /dreams in preRun, mounts IDBFS for saves at
 * /dreams/DATA/GAME and starts the game. Contract: recomp/web/CONTRACT.md.
 *
 * URL options: ?demo=<manifest or pack base url>  ?debug  ?autostart  ?mute=1  ?conn=N
 * Globals (config.js or the page): DREAMS_DEMO_BASE, DREAMS_ENGINE_BASE.
 * Test and debug hook: window.dreamsPage.
 */
(() => {
'use strict';

const qs = new URLSearchParams(location.search);
const DEBUG = qs.has('debug');
const MIB = 1024 * 1024;
const MOUNT = '/dreams';
const SAVE_DIR = MOUNT + '/DATA/GAME';
const CACHE_PREFIX = 'dreams-pack-';
const META_CACHE = 'dreams-meta';

const $ = (id) => document.getElementById(id);
const el = {
  canvas: $('canvas'), overlay: $('overlay'), bar: $('bar'), bytes: $('bytes'), rate: $('rate'),
  status: $('status'), play: $('play'), hint: $('hint'), error: $('error'), errorTitle: $('error-title'),
  errorText: $('error-text'), retry: $('retry'), hud: $('hud'), mute: $('mute'), fs: $('fs'),
  log: $('log'), filebox: $('filebox'), filesum: $('filesum'), files: $('files'),
  player: $('player'), volume: $('volume'),
};
// Presentation hooks (site/ui.js owns the look): a button's text lives in its .lbl child, if it has one.
function setLabel(node, text) { const l = node.querySelector('.lbl'); (l || node).textContent = text; }

const page = window.dreamsPage = {
  phase: 'init',          // init, checking, manifest, downloading, ready, starting, running, ended, error
  isolated: false,
  cacheHit: false,        // every file came from the cache, nothing was downloaded
  cacheKey: '',
  manifestUrl: '',
  bytesTotal: 0, bytesDone: 0, bytesFetched: 0,
  filesTotal: 0, filesDone: 0,
  downloaded: false, clicked: false,
  error: null,
  mounted: [],            // files written into /dreams
  saveSyncs: 0,
  persistSaves: false,
  log: [],
  Module: null,
  audio: null,
  start: () => userStart(),
  // Presentation hooks. site/ui.js replaces captureKeys: false hands the keys back to the browser
  // (a dialog is open, the canvas is not focused). Events: document 'dreams:phase', 'dreams:error'.
  captureKeys: () => true,
  // true asks the engine for the game's own 640 x 480 drawing buffer instead of the canvas's device
  // pixels (site/ui.js: pixel-perfect); call page.updateView() after it changes.
  nativeResolution: () => false,
  updateView: () => updateView(),
  view: null,             // [w, h] of the drawing buffer last asked of the engine
  setVolume: (v) => setVolume(v),
  toggleMute: () => { audio.muted = !audio.muted; applyMute(); },
  toggleFullscreen: () => toggleFullscreen(),
};
function emit(type, detail) { document.dispatchEvent(new CustomEvent('dreams:' + type, { detail })); }

function log(...a) {
  const line = ((performance.now() / 1000).toFixed(2) + ' ' + a.join(' '));
  page.log.push(line);
  if (page.log.length > 500) page.log.shift();
  if (DEBUG) { el.log.textContent += line + '\n'; el.log.scrollTop = el.log.scrollHeight; }
  console.log('[dreams]', ...a);
}
if (DEBUG) document.body.classList.add('debug');

function setPhase(p) { page.phase = p; document.body.dataset.phase = p; log('phase', p); emit('phase', { phase: p }); }
function setStatus(t) { el.status.textContent = t; }
const mib = (n) => (n / MIB >= 10 ? (n / MIB).toFixed(0) : (n / MIB).toFixed(1)) + ' MiB';

function fail(title, detail, canRetry = true) {
  page.error = { title, detail: String(detail) };
  setPhase('error');
  el.errorTitle.textContent = title;
  el.errorText.textContent = String(detail);
  el.error.classList.add('show');
  el.retry.style.display = canRetry ? '' : 'none';
  el.overlay.classList.remove('hidden');
  el.hud.classList.remove('show');
  el.play.disabled = true;
  setStatus('');
  log('ERROR', title, detail);
  emit('error', { title, detail: String(detail), canRetry });
}
el.retry.addEventListener('click', () => location.reload());

// The player chose Quit in the game's menu (or the program returned): the start screen comes back.
function ended(text) {
  if (page.phase === 'error' || page.phase === 'ended') return;
  syncSaves('exit');
  setPhase('ended');
  el.overlay.classList.remove('hidden');
  el.hud.classList.remove('show');
  el.play.disabled = false;
  setLabel(el.play, 'Play again');
  setStatus('You quit the game. Your autosave is kept.');
  log('ended', text || '');
  el.play.focus({ preventScroll: true });
}

/* ---- audio: capture contexts so the page can unlock and mute them -------- */

const audio = page.audio = { ctxs: [], masters: new Map(), muted: false, volume: 1 };
audio.muted = qs.get('mute') === '1' || localStorage.getItem('dreams.mute') === '1';
{ const v = parseFloat(localStorage.getItem('dreams.volume')); if (v >= 0 && v <= 1) audio.volume = v; }
const masterGain = () => (audio.muted ? 0 : audio.volume);
(function captureAudio() {
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC || !window.AudioNode) return;
  const origConnect = AudioNode.prototype.connect;
  const masterFor = (ctx) => {
    let g = audio.masters.get(ctx);
    if (!g) {
      g = ctx.createGain();
      g.gain.value = masterGain();
      origConnect.call(g, ctx.destination);
      audio.masters.set(ctx, g);
    }
    return g;
  };
  class Captured extends AC {
    constructor(...a) { super(...a); audio.ctxs.push(this); }
  }
  window.AudioContext = Captured;
  if (window.webkitAudioContext) window.webkitAudioContext = Captured;
  // Everything the game routes to the destination goes through one gain node, so
  // mute is a gain change and the game's audio keeps being consumed.
  AudioNode.prototype.connect = function (dest, ...rest) {
    if (typeof AudioDestinationNode !== 'undefined' && dest instanceof AudioDestinationNode &&
        this.context === dest.context) {
      origConnect.call(this, masterFor(dest.context), ...rest);
      return dest;
    }
    return origConnect.call(this, dest, ...rest);
  };
})();
function applyMute() {
  for (const g of audio.masters.values()) g.gain.value = masterGain();
  el.mute.setAttribute('aria-pressed', String(audio.muted));
  setLabel(el.mute, audio.muted ? 'Muted' : 'Mute');
  localStorage.setItem('dreams.mute', audio.muted ? '1' : '0');
  if (el.volume) el.volume.value = String(Math.round(audio.volume * 100));
  emit('audio', { muted: audio.muted, volume: audio.volume });
}
function setVolume(v) {
  audio.volume = Math.max(0, Math.min(1, v));
  localStorage.setItem('dreams.volume', String(audio.volume));
  if (audio.volume > 0 && audio.muted) audio.muted = false;   // moving the slider up is asking for sound
  applyMute();
}
function resumeAudio() {
  for (const c of audio.ctxs) if (c.state === 'suspended') c.resume().catch(() => {});
}
el.mute.addEventListener('click', () => { audio.muted = !audio.muted; applyMute(); el.canvas.focus(); });
if (el.volume) el.volume.addEventListener('input', () => setVolume(Number(el.volume.value) / 100));
applyMute();

/* ---- environment checks --------------------------------------------------- */

function checkEnvironment() {
  const problems = [];
  page.isolated = !!window.crossOriginIsolated;
  if (typeof WebAssembly !== 'object') problems.push('WebAssembly is not available in this browser.');
  if (!page.isolated || typeof SharedArrayBuffer === 'undefined') {
    let why = 'SharedArrayBuffer is not available: the page is not cross-origin isolated.';
    if (!window.isSecureContext) why += ' It must be served over https (or from localhost).';
    else why += ' The server must send the headers\n  Cross-Origin-Opener-Policy: same-origin\n  Cross-Origin-Embedder-Policy: require-corp\n' +
      'on the page (Cloudflare Pages: the _headers file written by recomp/web/package.py) and ' +
      'every cross-origin asset needs CORS or Cross-Origin-Resource-Policy: cross-origin.';
    problems.push(why);
  }
  if (!(window.crypto && crypto.subtle)) problems.push('The Web Crypto API is missing (needs https or localhost).');
  try {
    const c = document.createElement('canvas');
    if (!c.getContext('webgl2')) problems.push('WebGL 2 is not available in this browser.');
  } catch (e) { problems.push('WebGL 2 is not available in this browser.'); }
  return problems;
}

/* ---- demo pack ------------------------------------------------------------ */

function manifestUrlFromConfig() {
  let base = qs.get('demo') || window.DREAMS_DEMO_BASE || 'demo/';
  if (!/\.json(\?|$)/.test(base)) base = base.replace(/\/?$/, '/') + 'manifest.json';
  return new URL(base, location.href).href;
}
function engineBase() {
  const b = window.DREAMS_ENGINE_BASE || './';
  return new URL(b.replace(/\/?$/, '/'), location.href).href;
}
const hex = (buf) => Array.from(new Uint8Array(buf), (b) => b.toString(16).padStart(2, '0')).join('');
async function sha256(data) { return hex(await crypto.subtle.digest('SHA-256', data)); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function loadManifest(url) {
  const meta = await caches.open(META_CACHE).catch(() => null);
  let text;
  try {
    const r = await fetch(url, { cache: 'no-cache', credentials: 'omit' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    text = await r.text();
    if (meta) meta.put(new Request(url), new Response(text)).catch(() => {});
  } catch (e) {
    const old = meta && await meta.match(new Request(url));
    if (!old) throw new Error('Cannot fetch the demo manifest ' + url + ' (' + e.message + ')');
    log('manifest fetch failed, using the cached copy:', e.message);
    text = await old.text();
  }
  let m;
  try { m = JSON.parse(text); } catch (e) { throw new Error('The demo manifest is not valid JSON: ' + e.message); }
  if (!Array.isArray(m.files)) throw new Error('The demo manifest has no "files" list.');
  const key = 'v' + (m.version ?? 0) + '-' + (await sha256(new TextEncoder().encode(text))).slice(0, 16);
  return { manifest: m, key };
}

const store = {
  cache: null,
  name: '',
  keyUrl(path) { return new URL('__dreams_pack__/' + encodeURIComponent(this.name) + '/' + path, location.href).href; },
  async open(key) {
    this.name = key;
    try { this.cache = await caches.open(CACHE_PREFIX + key); } catch (e) { this.cache = null; log('Cache API unavailable:', e.message); }
  },
  async has(path, size) {
    if (!this.cache) return false;
    const r = await this.cache.match(this.keyUrl(path));
    return !!r && Number(r.headers.get('content-length')) === size;
  },
  async put(path, buf) {
    if (!this.cache) return false;
    try {
      await this.cache.put(this.keyUrl(path), new Response(buf, { headers: { 'content-length': String(buf.byteLength) } }));
      return true;
    } catch (e) { log('cannot cache', path, e.message); return false; }
  },
  async get(path) {
    const r = this.cache && await this.cache.match(this.keyUrl(path));
    return r ? new Uint8Array(await r.arrayBuffer()) : null;
  },
  async dropOthers(key) {
    for (const n of await caches.keys()) if (n.startsWith(CACHE_PREFIX) && n !== CACHE_PREFIX + key) await caches.delete(n);
  },
  async dropThis() { await caches.delete(CACHE_PREFIX + this.name); },
};

const memFiles = new Map(); // files that could not be cached stay in memory
let pack = null;            // {manifest, key, url}

function fileRow(f) {
  const li = document.createElement('li');
  li.innerHTML = '<span></span><span></span>';
  li.firstChild.textContent = f.path;
  li.lastChild.textContent = mib(f.size);
  el.files.appendChild(li);
  f.row = li;
}

let lastTick = { t: performance.now(), b: 0 }, rateSmooth = 0;
function renderProgress() {
  const total = page.bytesTotal || 1;
  const pct = Math.min(100, (page.bytesDone / total) * 100);
  el.bar.firstElementChild.style.width = pct.toFixed(1) + '%';
  el.bar.setAttribute('aria-valuenow', pct.toFixed(0));
  el.bytes.textContent = mib(page.bytesDone) + ' / ' + mib(page.bytesTotal) + '  (' + page.filesDone + '/' + page.filesTotal + ' files)';
  const now = performance.now();
  if (now - lastTick.t > 500) {
    const inst = (page.bytesFetched - lastTick.b) / ((now - lastTick.t) / 1000);
    rateSmooth = rateSmooth ? rateSmooth * 0.6 + inst * 0.4 : inst;
    lastTick = { t: now, b: page.bytesFetched };
    el.rate.textContent = page.phase === 'downloading' && rateSmooth > 0 ? (rateSmooth / MIB).toFixed(1) + ' MiB/s' : '';
  }
}

async function fetchChunk(url, size, dst, offset, onBytes) {
  const r = await fetch(url, { credentials: 'omit' });
  if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + url);
  let got = 0;
  if (r.body && r.body.getReader) {
    const rd = r.body.getReader();
    for (;;) {
      const { done, value } = await rd.read();
      if (done) break;
      if (got + value.length > size) throw new Error('Chunk longer than the manifest says: ' + url);
      dst.set(value, offset + got);
      got += value.length;
      onBytes(value.length);
    }
  } else {
    const b = new Uint8Array(await r.arrayBuffer());
    if (b.length > size) throw new Error('Chunk longer than the manifest says: ' + url);
    dst.set(b, offset); got = b.length; onBytes(got);
  }
  if (got !== size) throw new Error('Chunk is ' + got + ' bytes, the manifest says ' + size + ': ' + url);
}

async function downloadPack() {
  setPhase('manifest');
  setStatus('Reading the manifest...');
  page.manifestUrl = manifestUrlFromConfig();
  log('manifest', page.manifestUrl);
  const { manifest, key } = await loadManifest(page.manifestUrl);
  pack = { manifest, key, url: page.manifestUrl };
  page.cacheKey = key;
  await store.open(key);

  const files = manifest.files.map((f) => {
    const chunks = (f.chunks && f.chunks.length ? f.chunks : [{ url: f.path, size: f.size }]);
    const sum = chunks.reduce((s, c) => s + c.size, 0);
    if (sum !== f.size) throw new Error(f.path + ': chunk sizes add up to ' + sum + ', the manifest says ' + f.size);
    return { path: f.path, size: f.size, sha256: f.sha256, chunks, done: false };
  });
  page.filesTotal = files.length;
  page.bytesTotal = files.reduce((s, f) => s + f.size, 0);
  el.filesum.textContent = 'Files (' + files.length + ', ' + mib(page.bytesTotal) + ')';
  el.filebox.hidden = false;
  files.forEach(fileRow);
  pack.files = files;
  renderProgress();

  // Files already in the cache count as done; an interrupted download resumes here.
  const missing = [];
  for (const f of files) {
    if (await store.has(f.path, f.size)) {
      f.done = true; f.row.className = 'ok';
      page.bytesDone += f.size; page.filesDone++;
    } else missing.push(f);
  }
  page.cacheHit = missing.length === 0;
  renderProgress();

  if (missing.length) {
    setPhase('downloading');
    setStatus('Downloading the demo data...');
    navigator.storage && navigator.storage.persist && navigator.storage.persist().catch(() => {});
    await download(missing, key);
  } else {
    log('pack found in the cache', key);
    setStatus('Demo data loaded from the cache.');
  }
  store.dropOthers(key).catch(() => {});
  page.downloaded = true;
  el.bar.classList.add('done');
  el.rate.textContent = '';
  renderProgress();
}

async function download(missing, key) {
  const conn = Math.max(1, Math.min(8, Number(qs.get('conn')) || 4));
  const queue = [];
  for (const f of missing) {
    f.buf = null; f.left = f.chunks.length;
    let off = 0;
    f.chunks.forEach((c, i) => {
      const u = new URL(c.url, page.manifestUrl);
      if (!u.search) u.search = '?v=' + key;   // new manifest, new URLs: safe to cache them forever
      queue.push({ f, i, url: u.href, size: c.size, offset: off });
      off += c.size;
    });
  }
  let failed = null;
  const worker = async () => {
    while (!failed) {
      const t = queue.shift();
      if (!t) return;
      const f = t.f;
      if (!f.buf) f.buf = new Uint8Array(f.size);
      let ok = false;
      for (let attempt = 1; attempt <= 3 && !ok; attempt++) {
        let counted = 0;
        try {
          await fetchChunk(t.url, t.size, f.buf, t.offset, (n) => {
            counted += n; page.bytesDone += n; page.bytesFetched += n; renderProgress();
          });
          ok = true;
        } catch (e) {
          page.bytesDone -= counted;
          log('chunk failed', t.url, e.message, 'attempt', attempt);
          if (attempt === 3) { failed = e; return; }
          await sleep(400 * attempt);
        }
      }
      if (--f.left === 0) {
        try { await finishFile(f); } catch (e) { failed = e; return; }
      }
    }
  };
  await Promise.all(Array.from({ length: conn }, worker));
  if (failed) throw failed;
}

async function finishFile(f) {
  setStatus('Verifying ' + f.path + '...');
  if (f.sha256) {
    const got = await sha256(f.buf);
    if (got !== String(f.sha256).toLowerCase()) {
      throw new Error('Checksum mismatch for ' + f.path + '\n  expected ' + f.sha256 + '\n  got      ' + got);
    }
  } else log('no sha256 in the manifest for', f.path);
  if (!(await store.put(f.path, f.buf))) memFiles.set(f.path, f.buf);
  f.buf = null; f.done = true; f.row.className = 'ok';
  page.filesDone++;
  setStatus('Downloading the demo data...');
  renderProgress();
}

/* ---- file system: the pack, saves ----------------------------------------- */

function mkdirp(FS, dir) {
  if (FS.mkdirTree) { FS.mkdirTree(dir); return; }
  let cur = '';
  for (const part of dir.split('/').filter(Boolean)) {
    cur += '/' + part;
    try { FS.mkdir(cur); } catch (e) { /* exists */ }
  }
}
function writePackFile(FS, f, data) {
  const dest = MOUNT + '/' + f.path;
  mkdirp(FS, dest.slice(0, dest.lastIndexOf('/')));
  FS.writeFile(dest, data, { canOwn: true });
  page.mounted.push(f.path);
}

let saveFS = null, saveBusy = false, saveAgain = false;
function syncSaves(reason) {
  if (!saveFS || !page.persistSaves) return;
  if (saveBusy) { saveAgain = true; return; }
  saveBusy = true;
  try {
    saveFS.syncfs(false, (err) => {
      saveBusy = false;
      if (err) log('save sync failed:', err); else page.saveSyncs++;
      if (saveAgain) { saveAgain = false; syncSaves('again'); }
    });
  } catch (e) { saveBusy = false; log('save sync error:', e.message); }
}
function watchSaves() {
  setInterval(() => syncSaves('timer'), 5000);
  addEventListener('pagehide', () => syncSaves('pagehide'));
  document.addEventListener('visibilitychange', () => { if (document.hidden) syncSaves('hidden'); });
}

async function readPackFiles() {
  const out = new Map();
  for (const f of pack.files) {
    let d = memFiles.get(f.path) || await store.get(f.path);
    if (!d || d.length !== f.size) {
      await store.dropThis().catch(() => {});
      throw new Error('The cached copy of ' + f.path + ' is missing or damaged; reload to download it again.');
    }
    out.set(f.path, d);
  }
  return out;
}

function populate(M, files) {
  const FS = M.FS;
  if (!FS) throw new Error('The engine does not export the Emscripten FS (needs -sEXPORTED_RUNTIME_METHODS=FS).');
  // WD_INSTALL_ROOT must be in the environment before main runs.
  if (M.ENV) { M.ENV.WD_INSTALL_ROOT = MOUNT; } else {
    log('warning: Module.ENV is not exported by the engine, WD_INSTALL_ROOT cannot be set from the page');
  }
  mkdirp(FS, MOUNT);
  const saves = [];
  for (const f of pack.files) {
    const data = files.get(f.path);
    if (f.path.startsWith('DATA/GAME/')) { saves.push([f, data]); continue; }
    writePackFile(FS, f, data);
  }
  files.clear();

  mkdirp(FS, SAVE_DIR);
  const IDBFS = (FS.filesystems && FS.filesystems.IDBFS) || M.IDBFS;
  const addDep = M.addRunDependency, delDep = M.removeRunDependency;
  const seed = () => {
    for (const [f, data] of saves) {
      try { FS.stat(MOUNT + '/' + f.path); } catch (e) { writePackFile(FS, f, data); }   // keep the player's save
    }
  };
  if (IDBFS && addDep && delDep) {
    FS.mount(IDBFS, {}, SAVE_DIR);
    addDep('dreams-saves');
    FS.syncfs(true, (err) => {
      if (err) log('cannot load saves from IndexedDB:', err); else page.persistSaves = true;
      seed();
      saveFS = FS;
      watchSaves();
      log('saves mounted (IDBFS), persistence', page.persistSaves);
      delDep('dreams-saves');
    });
  } else {
    seed();
    log('warning: IDBFS or addRunDependency is not exported by the engine; saves are not persisted');
  }
}

/* ---- start ---------------------------------------------------------------- */

function loadScript(url) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = url; s.async = true;
    s.onload = resolve;
    s.onerror = () => reject(new Error('Cannot load the engine script ' + url));
    document.head.appendChild(s);
  });
}

// Gamepads: WD_PAD=keys makes a pad press the game's keys (docs/research/running.md layout), so it
// works without pressing J; ?pad=winmm|off overrides. The host turns WD_* arguments into environment.
function padArguments() {
  const m = qs.get('pad') || 'keys';
  return /^(keys|winmm|off)$/.test(m) ? ['WD_PAD=' + m] : [];
}

let started = false;
async function startGame() {
  if (started) return;
  started = true;
  setPhase('starting');
  el.play.disabled = true;
  setStatus('Starting the game...');
  el.hint.textContent = '';
  try {
    const files = await readPackFiles();
    const base = engineBase();
    if (typeof window.createDreams !== 'function') await loadScript(base + 'dreams.js');
    if (typeof window.createDreams !== 'function') throw new Error('dreams.js did not define createDreams.');
    let running = false, gotStatus = false;
    const markRunning = (why) => {
      if (running) return;
      running = true;
      log('running (' + why + ')');
      setPhase('running');
      el.overlay.classList.add('hidden');
      el.hud.classList.add('show');
      el.canvas.focus();
    };
    const M = await window.createDreams({
      canvas: el.canvas,
      arguments: padArguments(),
      locateFile: (p) => base + p,
      print: (t) => log('out:', t),
      printErr: (t) => log('err:', t),
      preRun: [function (mod) { populate(mod && mod.FS ? mod : this, files); }],
      // A fatal error the host already reported (its text is the useful one) aborts next; so does a quit.
      onAbort: (what) => { if (page.phase !== 'error' && page.phase !== 'ended') fail('The game stopped', 'The engine aborted: ' + what, true); },
      onDreamsStatus: (kind, text) => {
        log('status', kind, text || '');
        gotStatus = true;
        if (kind === 'boot') setStatus(text || 'Starting the game...');
        else if (kind === 'running') markRunning('onDreamsStatus');
        else if (kind === 'fatal') { if (page.phase !== 'error') fail('The game reported a fatal error', text || '(no details)', true); }
        else if (kind === 'exit') ended(text);
        if (typeof page.onStatus === 'function') page.onStatus(kind, text);
      },
    });
    page.Module = M;
    updateView();
    // An engine without onDreamsStatus support still starts: assume it runs.
    setTimeout(() => { if (page.phase === 'starting' && !gotStatus) markRunning('timeout'); }, 4000);
  } catch (e) {
    fail('Cannot start the game', (e && e.message) || e);
  }
}

function userStart() {
  if (page.phase === 'ended') { location.reload(); return; }
  page.clicked = true;
  resumeAudio();
  el.canvas.focus();
  if (page.downloaded) startGame();
  else { setStatus('Waiting for the download to finish, then the game starts...'); el.play.disabled = true; }
}
el.play.addEventListener('click', userStart);

/* ---- the engine's drawing buffer ------------------------------------------ */

// The engine draws the 3D scene at its drawing buffer's size and the page shows that buffer at the
// canvas's CSS size, so the buffer follows the canvas in device pixels (Module._wd_web_view_size,
// host/web/web_glue.c; the engine caps it at 4096). Without it the game draws 640 x 480 and the
// browser stretches it.
// (ResizeObserver's device-pixel-content-box is not used: headless Chrome reports it in CSS pixels
// under an emulated device scale factor.)
function updateView() {
  const M = page.Module;
  if (!M || typeof M._wd_web_view_size !== 'function') return;
  let w = 640, h = 480;
  if (!page.nativeResolution()) {
    const r = el.canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    w = Math.round(r.width * dpr); h = Math.round(r.height * dpr);
  }
  if (w < 64 || h < 64) return;                                       // not laid out
  if (page.view && page.view[0] === w && page.view[1] === h) return;
  page.view = [w, h];
  M._wd_web_view_size(w, h);
  log('drawing buffer', w + 'x' + h);
}
if (window.ResizeObserver) new ResizeObserver(() => updateView()).observe(el.canvas);
// Zoom and moving to another screen change the device pixel ratio, not always the CSS size.
(function watchRatio() {
  if (!window.matchMedia) return;
  matchMedia('(resolution: ' + (window.devicePixelRatio || 1) + 'dppx)').addEventListener('change', () => {
    updateView();
    watchRatio();
  }, { once: true });
})();

/* ---- input, fullscreen ---------------------------------------------------- */

addEventListener('keydown', (e) => {
  if (page.phase !== 'running' && page.phase !== 'starting') return;
  if (!page.captureKeys()) return;                                     // a dialog or the toolbar has the keys
  const k = e.key;
  if (k === 'F5' || k === 'F11' || k === 'F12') return;               // reload, fullscreen, devtools
  if ((e.ctrlKey || e.metaKey) && k.length === 1) return;              // Ctrl+R, Ctrl+W ...: the browser's
  e.preventDefault();                                                  // arrows, Space, Alt, F10, Tab, Esc ...
}, true);
addEventListener('keyup', (e) => { if (page.phase === 'running' && page.captureKeys()) e.preventDefault(); }, true);
el.canvas.addEventListener('contextmenu', (e) => e.preventDefault());
el.canvas.addEventListener('wheel', (e) => e.preventDefault(), { passive: false });
el.canvas.addEventListener('pointerdown', () => { el.canvas.focus(); resumeAudio(); });
addEventListener('pointerdown', resumeAudio);
addEventListener('keydown', resumeAudio);
addEventListener('focus', () => { if (page.phase === 'running') el.canvas.focus(); });

async function toggleFullscreen() {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else {
      await (el.player || document.documentElement).requestFullscreen({ navigationUI: 'hide' });
      // Esc is the game's menu key: ask the browser not to leave fullscreen on it.
      if (navigator.keyboard && navigator.keyboard.lock) navigator.keyboard.lock(['Escape']).catch(() => {});
    }
  } catch (e) { log('fullscreen:', e.message); }
  el.canvas.focus();
}
el.fs.addEventListener('click', toggleFullscreen);
document.addEventListener('fullscreenchange', () => {
  setLabel(el.fs, document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen');
  el.fs.setAttribute('aria-pressed', String(!!document.fullscreenElement));
  if (!document.fullscreenElement && navigator.keyboard && navigator.keyboard.unlock) navigator.keyboard.unlock();
});

/* ---- main ----------------------------------------------------------------- */

addEventListener('error', (e) => log('window error:', e.message));
addEventListener('unhandledrejection', (e) => log('unhandled rejection:', (e.reason && e.reason.message) || e.reason));

(async function main() {
  setPhase('checking');
  const problems = checkEnvironment();
  if (problems.length) {
    el.bar.style.display = 'none';
    el.play.style.display = 'none';
    fail('This browser or page cannot run the game', problems.join('\n\n'), false);
    return;
  }
  el.play.disabled = false;   // the click may come while the data downloads
  setLabel(el.play, 'Play demo');
  try {
    await downloadPack();
  } catch (e) {
    fail('Cannot load the demo data', (e && e.message) || e);
    return;
  }
  setPhase('ready');
  el.play.disabled = false;
  el.play.focus({ preventScroll: true });
  setStatus('Ready when you are.');
  if (page.clicked || qs.has('autostart')) startGame();
})();
})();
