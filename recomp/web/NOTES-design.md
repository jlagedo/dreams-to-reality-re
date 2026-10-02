# DESIGN notes: the page's look, copy and in-game key handling

Owner: DESIGN stream (`recomp/web/index.html`, `recomp/web/site/`). Behaviour stays
in `loader.js` (PAGE stream's API: `window.dreamsPage`, the ids, the contract).
One static page, no framework, no build step, no external request (the page is
COEP `require-corp`): system fonts, inline SVG. Weight: `index.html` 32 KB,
`style.css` 27 KB, `ui.js` 19 KB, `loader.js` 26 KB (about 104 KB raw, well under
the 150 KB target; screenshots are optional and extra).

## Files

| File | What |
|---|---|
| `index.html` | Markup and all copy: header, stage (poster, loading, error, veil, toolbar), controls dialog, side panel, About, Controls, Tips, This port, FAQ, credits and legal footer |
| `site/style.css` | Tokens (`:root`), layout, components. Responsive; reduced motion; forced colors |
| `site/ui.js` | Presentation behaviour only: toolbar status, controls dialog, key shield, veil, scale mode, fullscreen toolbar, loading tips, plain-language errors, optional screenshots |
| `loader.js` | Unchanged behaviour. UI hooks added: `dreams:phase`, `dreams:error`, `dreams:audio` events on `document`; `page.captureKeys()` (replaced by `ui.js`); `page.setVolume`, `page.toggleMute`, `page.toggleFullscreen`; button text goes into a `.lbl` child; fullscreen target is `#player`; `?pad=keys|winmm|off` (default `keys`) passed to the host as `WD_PAD=...` |
| `package.py` | Ships `site/` (without `shots/` or `*.md`); `--shots DIR` adds game screenshots (optional, game-derived, `out/` only); `/site/*` is `no-cache` in `_headers` |
| `browser_check.py` | `run_design_checks`: 4 viewports (1920x1080, 1366x768, 1024x768, 390x844), screenshots `out/recomp/web/check/d-<WxH>-*.png`; `--design-only`, `--design` (real build) |

## Structure of the page

```
body[data-phase]            loader.js sets data-phase: init checking manifest downloading ready starting running error
  .sky                      fixed backdrop: stars (2 layers), mist, floating islands. Stands still while running
  header.top                brand + section links (sticky)
  main
    p.notice.phone          "best on desktop with a keyboard": width <= 760 px or (pointer: coarse) and (hover: none)
    section.play
      #player               container (cqw), fullscreen target
        .bezel              the frame
          #stage.screen     4:3, --stage-w wide
            #canvas         the engine's canvas (CSS size forced to the stage)
            #overlay        poster SVG + card: wordmark, .panel (progress, status, #play, hint, tip, files, #error)
            #veil           "click to take control" (running, keys not going to the game)
            #keyhint        "Press ? for controls", fades after 7 s
          #hud.toolbar      state text, Menu(Esc), Controls, Pixel-perfect, Mute + volume, Fullscreen, Reload
        #controls-dialog    native <dialog>, modal: the controls, filled from #controls-src
      aside.side            the same controls, at >= 1200 px, sticky and scrollable
    .wrap > #game #controls #tips #port #faq     below the fold
  footer.foot               credits, legal
```

The ids `canvas overlay bar bytes rate status play hint error error-title error-text retry hud mute fs log filebox
filesum files` are the loader's contract and `browser_check.py`'s selectors; `#overlay.hidden` means the game runs.
`#hud` is always visible (not only `.show`); `[data-needs-game]` buttons are dimmed until `running`.

## Stage and scaling

* The game is **640 x 480, 4:3** (retail window `640x480`, default `WD_SCALE=2` gives a 1280x960 window; the
  resolution table is F1..F6, F3 default; `recomp/windream/host/sdl/user.c`, `docs/research/running.md`).
  `#stage` keeps `aspect-ratio: 4/3` and `--stage-w = min(100cqw - 2*bezel, max(340px, (100svh - --chrome) * 4/3))`
  so the whole stage plus toolbar fits the viewport at 1024x768 and up (checked by `browser_check`). `--chrome` is the
  vertical space the stage shares with header, bezel and toolbar.
* `#canvas` is `100% !important` of the stage: the engine owns the pixel size, the page the display size. If the
  engine resizes the canvas by CSS, the stage still wins.
* **Pixel-perfect** (toolbar, remembered in `localStorage dreams.scale`): `image-rendering: pixelated` and, when
  it does not shrink the picture below 75% of the fitted size, a whole multiple of 640 device pixels (`--stage-w`
  set by `fit()`); otherwise the fitted size, still sharp (`body[data-pixel-size] = integer|fit`). On a 1080p screen
  the fit size is about 1200 px, so integer scaling means 1x (640) only at 1x DPR, which is too small: it falls back
  to the fitted size. Note that a smooth/sharp choice only matters if the engine's canvas backing store is
  smaller than the display size; if SDL resizes the backing store to the CSS size, the engine's own filtering
  decides (open question for the GL stream).
* **Fullscreen** fullscreens `#player` (not the document): black letterbox, stage `min(100vw, 133.33vh)`, toolbar
  floats at the bottom and hides after 2.6 s without pointer movement. Chromium also gets Keyboard Lock for Esc
  (loader.js); elsewhere Esc leaves fullscreen, hence the **Menu** button, which taps Esc into the game.
* While `running` the backdrop animations are off (`.sky * { animation: none }`) so the page does not take frames from
  the game. The headless software GL made a blur-heavy backdrop take 5 to 30 s per screenshot; no blur filters and no
  `backdrop-filter` sit over the canvas or animated content.

## Tokens (`:root` in `style.css`)

Taken from the game's own scenes (3dfx reference captures, the demo's first scene) and its HUD:

| Token | Value | From |
|---|---|---|
| `--night-0/1/2/3` | `#060914 #0a1020 #111a33 #1a2547` | page and panels: the dark of the dream sky |
| `--sky`, `--mist` | `#5fa8e6`, `#c3e6f6` | the game's open sky and clouds |
| `--sand` | `#d9a85a` | the sandstone plateaus (card headings) |
| `--jade` | `#7fcfb8` | Duncan's skin tone (poster figure) |
| `--ember`, `--ember-hi` | `#f08a3c`, `#ffb067` | Duncan's orange trousers: the primary action |
| `--mana`, `--life` | `#47d4e0`, `#e5484d` | the blue and red of the pyramid HUD gauge: links, focus, headings; mute and errors |
| `--ink`, `--ink-dim` | `#eceff8`, `#a3adca` | text (the dim one is 7.6:1 on `--night-1`) |
| `--serif`, `--sans`, `--mono` | Iowan/Palatino/Georgia, system-ui, ui-monospace | no downloaded fonts; wide-tracked caps for the wordmark echo 1990s Cryo box art |

The emblem (`#emblem` symbol) is original: a ring, a pupil, and the red and blue bars of the HUD gauge. The poster is
original inline SVG, a stone slab in the clouds and a small figure (it is **not** game art). To change the palette
edit `:root`; the poster's hexes are in `index.html` (`#p-*` gradients) and the starfield in `.stars`.

## Changing copy

All copy is plain HTML in `index.html`; sections have ids `game controls tips port faq`. Facts and where they come
from:

| Copy | Source |
|---|---|
| Cryo Interactive, 1997; builds (Windows 95/DOS/3dfx); Duncan "the chosen one" | `SETUP.INI` copyright, `docs/research/engine.md`, the demo's own dialogue line |
| 95 distinct scenes; island names (Angkor, Easter Island heads, hammam, cinema, factory, spider, shark) | `docs/research/game-content.md` |
| 30 items: 14 spells and abilities, 16 objects; manna; sword and crystal bow | `game-content.md` (the compiled-in English item texts) |
| Controls; the hints in "Tips" and the loading tips | `README.TXT` on disc 1, section 6 "Game controls" and 7 "Hints and Tips" |
| Autosave on each level, no manual save | `docs/research/running.md` (PCGamingWiki notes) |
| Port: lifted to C, Emscripten, SDL3 runtime, WebGL 2 renderer after the 3dfx look, IDBFS saves | `AGENTS.md`, `recomp/README.md`, `CONTRACT.md` |

Unverified or hedged on purpose: music in the demo (the original played CD audio tracks; the page says it "may be
missing"), what the demo slice contains (the page says "a small cut"), gamepad (see below). Edit these when the demo
pack is final. The French title is not used: the data names only "Dreams to Reality" (window class and docs).

## Controls and key conflicts

Sources: `README.TXT` section 6; `docs/research/engine.md` (polled keyboard, no mouse); `GAME_HandleHotkeys`
(`0x415aa7`, decompiled with `re/tools/ghidra_headless.py`), where the key-state array at `0x6308d8` is indexed by
virtual key (`+0x09` Tab, `+0x11` Ctrl, `+0x12` Alt, `+0x30..0x39` digits, `+0x4c` L, `+0x53` S, `+0x70..0x7b`
F1..F12); `recomp/windream/host/sdl/winmm.c` and `input_map.h` for the pad layout.

Keys the game reads (so the page must not use them for itself): arrows, Ctrl, Alt, Space, Esc, Tab (spells page),
`1 2 3`, `0 5 6 7 8 9` (Alt cameras), `F1..F6` (resolution), **`F10` (the game's help)**, `F11`/`F12` (the game
switches a display mode with them; F11 is also the browser's fullscreen, which `loader.js` lets through), `J` `K`
(joypad/keyboard mode), `L` (load page), `Q`, `Ctrl+S` (real/2D shadow). `F1` is therefore not usable for page help.

The page's keys and why they are safe:

| Key | Action | Why it does not conflict |
|---|---|---|
| `?` (Shift + `/`, by `e.key`) | controls dialog | the game does not read `/` (OEM_2); the page also hides the event from the engine |
| `Shift+Tab` | leave the game, focus the toolbar | the game uses neither Shift nor Shift+Tab; needed because Tab is the game's and is otherwise prevented, which would trap keyboard-only users |
| `Esc` on the toolbar or in the dialog | back to the game / close | handled only while the page owns the keys; in the game Esc is the game's menu |

**Key shield** (`ui.js`): keys belong to the game only while `#canvas` has focus, the window has focus and the dialog
is closed (`page.captureKeys()`). Otherwise `loader.js` stops calling `preventDefault` (so Space presses a button,
arrows scroll) and `ui.js` stops the event at the window capture phase (SDL listens on `window`), after sending
synthetic `keyup` for keys still held so the engine never keeps a stuck key. Synthetic events carry `__dreams` and
pass the shield: the Menu button uses them to tap Esc (140 ms) into the game.

The veil appears when the game is running and the keys are elsewhere (click outside, tab in the background, window
blur); the engine pauses without focus anyway (`GetFocus` follows the window). Clicking it focuses the canvas.

macOS: Ctrl+arrows are Mission Control shortcuts and never reach the page; the controls note says so.

Gamepad: `loader.js` passes `WD_PAD=keys` (the host's pad-as-keys mode, `docs/research/running.md` layout: stick and
d-pad arrows, A Ctrl, X Alt, Y Space, B Down, LB/RB/LT `1 2 3`, Start Esc, Back Enter) so a pad works without pressing
`J`. **Not tested in a browser**: it depends on SDL3's Gamepad API support in the Emscripten build; the panel says
"experimental". `?pad=winmm` gives the original path (press `J`), `?pad=off` none.

## Error states

`dreams:error` fills `#error-help` from a list in `ui.js` (`HELP`): no cross-origin isolation, no WebGL 2, no
WebAssembly, out of memory, checksum mismatch, download failure, missing engine, game stopped. The raw technical
text stays visible in `#error-text` (the checks read it), with "Try again", "Clear downloaded data and retry"
(deletes `dreams-*` caches and the IDBFS database `/dreams/DATA/GAME`) and "Copy technical details" (log + user
agent). No `Module` abort is classified beyond the patterns; add patterns there as real failures show up.

## Testing

```
uv run --with playwright python recomp/web/browser_check.py --mock --design-only      # fast, mock engine
uv run --with playwright python recomp/web/browser_check.py --real --design           # with the real build
uv run --with playwright pytest tests/recomp/test_web_page.py
```

Checks per viewport: 4:3 stage, no horizontal overflow, stage and toolbar inside the viewport (>= 1024 wide), phone
notice only on narrow screens, `?` opens the dialog and takes the keys, Esc returns them, Shift+Tab / Esc on the
toolbar, the veil and its click, pixel-perfect sizes, the Menu button's Esc, fullscreen (1366), no console errors.
Screenshots (`d-*.png`) are for looking at, not compared.

To see the optional game screenshots: `uv run --with pillow python recomp/web/package.py --shots <folder of PNGs>`
(game-derived: keep the folder under `out/`, never commit it; the dist folder then holds game images, so deploy it
only if you may).

## Open points

* The demo pack's content is not known to the page; copy is generic about it.
* Whether the engine's canvas backing store follows the CSS size (affects what pixel-perfect really does).
* Gamepad in the browser is untested; `F10` (the game's own help) is not wired to a button.
* Safari and Firefox were not run (only headless Chromium).
