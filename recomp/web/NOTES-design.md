# Browser tribute: presentation and sources

The page is an illustrated tribute to *Dreams to Reality*, with the browser
demo embedded between the manual's world imagery and the original team credits.
Hand-written markup and styles live in index.html and site/style.css.
The existing loader.js and site/ui.js own the game, controls, audio, saves,
focus, scaling and fullscreen.

## Layout

White paper (#fafaf8), charcoal type, Georgia headings and system UI text.
The original cover supplies the opening colour. Original monochrome manual
images are kept as scans, with their print texture. The player has a dark
surround; it preserves the game's 4:3 aspect ratio.

1. Cover: original manual artwork, dedication, links to the story and demo.
2. Story: an English adaptation of the manual's Duncan legend, with original
   illustrations of Duncan as a child and as an adult.
3. World: large manual images, labelled with their original page numbers.
4. Player: the original retail box beside the title and Play button on a pale
   start screen. The complete portrait scan is contained within its column;
   it is never stretched or cropped into a background. The existing loader,
   canvas and toolbar remain, with controls and help in
   disclosure sections below. The side-controls DOM remains for ui.js's
   cloning contract but is not displayed.
5. Creators: original team photo and all credits from the manual's two credit
   pages, grouped by role.
6. Preservation: a short tribute note, original manual link and expandable
   technical description of the browser port.

The header links jump directly to the story, world, creators and demo.
Artwork links open the larger image in a new tab. The page works without
JavaScript as a document; the game requires JavaScript.

## Original material

Source: the 33-page Spanish PC manual, locally
E:/dev_game/Dreams-to-Reality_Manual_Win_es.pdf.
This copy's SHA-256 is
c6c862a03cffceaffb0c40af52eed242baf5dff6f1b3f6be28fc8d14a4613f39.
PDF page numbers are one ahead of printed page numbers.

| Material | Printed pages |
|---|---|
| Story adaptation | 4-5 |
| Description of the dream universe | 6-7 |
| Young Duncan and adult Duncan | 5, 7 |
| World gallery | 8, 14, 16 |
| Player's waiting-screen image | Separate original PC + PC3Dfx box scan supplied by the user |
| Team photo | 28 |
| Full credits | 29-30 |

The story is labelled as an adaptation; the gallery captions are original
editorial text, not quotations. The 1988/1998 dates refer to the fiction,
not the development history. Credit role headings are translated; names
are transcribed from the Spanish edition. Guillaume Lairan's surname follows
the clearer spelling in the German manual (printed p. 30, PDF p. 32).
Source credit scans
are linked beside the credits. People in the group photograph are not
individually identified.

manual_assets.py extracts the original embedded scan images with pypdf
and Pillow. Its explicit crops remove margins or adjacent text, and
larger images are resized to at most 2000 x 1600. It never upscales.
sources.json records the PDF hash, source pages, crops and dimensions.
The optional --box argument copies the original JPEG unchanged to box-front.jpg
and records its hash and dimensions. The working source scan is kept at
E:/dev_game/Dreams-to-Reality_Box-front.jpg. Later manual extraction runs retain
the existing box scan when --box is omitted.
The PDF and extracted images are game-derived: they stay under out/,
never in tracked source.

~~~powershell
uv run --with pypdf --with pillow python recomp/web/manual_assets.py E:/dev_game/Dreams-to-Reality_Manual_Win_es.pdf --box E:/dev_game/Dreams-to-Reality_Box-front.jpg
uv run python recomp/web/package.py --engine out/recomp/windream/build-web-release
uv run python recomp/web/serve.py
~~~

package.py --manual DIR overrides the default out/recomp/web/manual
asset folder. Packaging copies it to dist/site/manual, including the
original PDF. The existing --poster option can override the player's
waiting-screen image with a game capture. Images are local to the page's
origin and covered by the existing isolation headers.

## Game contract and accessibility

The loader's IDs and events are preserved: canvas, overlay, play,
status, bar, bytes, rate, error, hud, mute, fs, filebox,
files, log, and the richer controls/error IDs used by site/ui.js.
Controls come from #controls-src and are cloned into the native dialog.

The game only receives keys while the canvas is focused. ? opens the
controls; Escape closes them; Shift+Tab moves focus to the toolbar.
Menu sends an Escape key to the game. Focus loss displays the return veil.
The page retains the existing mute, volume, scale and fullscreen behaviour.

The game's drawing buffer follows the canvas: loader.js asks the engine for
the canvas's size in device pixels (Module._wd_web_view_size, on canvas
resize, fullscreen and device-pixel-ratio changes; the engine caps it at
4096), so the 3D scene is drawn at the screen's resolution instead of
640 x 480 stretched by the browser. Pixel-perfect asks for the game's own
640 x 480 and shows it with image-rendering: pixelated
(page.nativeResolution, page.updateView in ui.js).
Game controls follow the retail README and the existing host evidence,
including autosaves on level entry.

The layout stacks on narrow screens, the credits use two columns, and
the keyboard requirement is shown at 760px and below. Reduced-motion
preferences, visible keyboard focus and a skip-to-game link are provided.
Images have descriptions, intrinsic dimensions and source captions.

## Verification

browser_check.py --mock --design-only exercises the existing controls
at 1920x1080, 1366x768, 1024x768 and 390x844. It scrolls to the player
before testing that the stage and toolbar fit the viewport: the tribute's
opening is intentionally above the game.
It also captures the ready-to-play screen and checks the artwork, title and
button fit inside it, and that Play retains a usable size. This catches the
missing panel/button styles that previously left browser-default controls
over the oversized manual image. File download details are visible with
?debug; the normal ready screen shows a short status and the Play button.

tests/recomp/test_web_page.py covers packaging, isolation headers, Range
responses, the mock engine and the real engine when available.
Screenshots and check output belong under out/recomp/web/.
