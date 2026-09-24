# Engine

## Summary

*Dreams to Reality* runs on Cryo Interactive's own in-house C/C++ engine. It has
no public or marketed name and is **not** the "Omni3D" engine Cryo branded on
their point-and-click adventures (Atlantis, Versailles). It is a real-time
behind-view 3D engine with a custom software rasterizer and an optional Glide
hardware path. **[verified]**

## Provenance marks

Strings recovered from the binaries: **[verified]**

| String | Found in | Meaning |
|---|---|---|
| `X:\CRYO\DREAMS\` | all builds | developer build root |
| `X:\CRYO\DREAMS\DATA\HD.ID` | `WINDREAM.EXE` | hardcoded install probe path |
| `C:\CRYO\DREAMS` | `SETUP.INI` | default install directory offered to the user |
| `CRYO 1997` | all builds | copyright |
| `Cryo Interactive © 1997` | `SETUP.INI` | installer copyright |
| `-Copyright CRYO-` | every HNM6 file | codec header field |
| `DAN_Load3DC` | `WINDREAM.EXE` | internal loader symbol |
| `Debordement dans DAN_Load3DC` | `WINDREAM.EXE` | untranslated French error, "overflow in" |
| `Error, not enough memory in DAN_Load3DC` | `WINDREAM.EXE` | English sibling of the above |
| `Dreams_MSS1` | `DREAMS.EXE` | Miles Sound System handle name |
| `Water surface` | `WINDREAM.EXE` | renderer feature |

The mixed French/English error strings and the leftover 3D Studio files in
`DATA\OBJET\` (`CUBE.ASC`, `3DS.BAK`) indicate the shipped discs were built
straight from the developer tree with no asset cleanup.

## Toolchain

**All four game executables were built with Watcom C/C++ 10.6 — including the
Windows ones.** This is the single most informative finding about the codebase.
The version is pinned down in [toolchain.md](toolchain.md), which also covers the
reference libraries and the runtime symbol names recovered from them.
**[verified]**

Evidence:

- `WINDREAM.EXE` contains the literal string `WATCOM C/C++32 Run-Time system. (c)
  Copyright by WATCOM International Corp. 1988-1995.`
- Its PE section names are `AUTO`, `.idata`, `DGROUP`, `.bss`, `.reloc`, `.rsrc`.
  `AUTO` and `DGROUP` are Watcom linker output. MSVC emits `.text` / `.data` /
  `.rdata`, which is what `SETUP.EXE` has — so the installer was built with a
  different toolchain from the game.
- `DREAMS.EXE` carries both the C/C++16 (1988-1994) and C/C++32 (1988-1995)
  runtime banners.

Watcom was the standard choice for DOS protected-mode games and also targeted
Win32. Cryo therefore maintained **one portable C core compiled three ways**,
with the platform-specific layer swapped underneath. This matters for any
reverse-engineering effort: the DOS and Windows builds should share most of
their logic, and the DOS builds have richer symbol residue.

## The four builds

| Build | Binary | Format | Video backend | Audio backend |
|---|---|---|---|---|
| DOS | `DREAMS.EXE` (1,176,454 B) | LE + DOS/4GW | SciTech UniVBE (VESA SVGA) | Miles Sound System |
| DOS 3dfx | `DREAMSFX.EXE` (1,025,154 B) | LE + DOS/4GW | Glide | Miles Sound System |
| Windows | `WINDREAM.EXE` (864,768 B) | Win32 PE, subsys 3.10 | DirectDraw | DirectSound + MCI |
| Windows GDI | `GDIDREAM.EXE` (864,768 B) | Win32 PE, subsys 3.10 | GDI (windowed) | DirectSound + MCI |

MD5: `WINDREAM.EXE` = `68BC8D526C57A2EBDC083E88325D4E64`,
`GDIDREAM.EXE` = `DA94096442CB66B4241341F20703C943`.

`WINDREAM.EXE` and `GDIDREAM.EXE` are **the same program**: identical byte size,
identical import table, different hash. Both link DirectDraw *and* the GDI path.
The two binaries differ only in which backend they default to. **[verified]**

`DOS4GW.EXE` (265,396 B) is the Rational Systems DOS extender both LE builds
load. `GETKEY.EXE` (4,469 B) is a trivial keypress-to-errorlevel helper used by
the `.BAT` installers.

## Middleware

None of these is the engine — they are licensed components the engine calls into. **[verified]**

| Component | Vendor | Used by | Evidence |
|---|---|---|---|
| **Miles Sound System** | RAD Game Tools | DOS builds | `Copyright (C) 1991-97 RAD Game Tools, Inc.`, `Miles Sound System`, `Dreams Internal Error : can't create Dreams_MSS1`, `can't register Miles Timer` |
| **SciTech UniVBE / MGL** | SciTech Software | DOS build | `Copyright (C) 1993-97 SciTech Software Inc`, `data\univbe`, `Error: Univbe Driver cannot be started...` |
| **Glide** | 3dfx | DOS 3dfx build | `glide2x`, `_GRGLIDEINIT@0`, `_GRGLIDESHUTDOWN@0`, `_GRGLIDEGETVERSION@4`, `_GRGLIDESHAMELESSPLUG@4` |
| **DOS/4GW** | Rational Systems | both DOS builds | `DOS4GW.EXE`, LE binary format |

The Miles installation lives in `DATA\SOUND\` — `MSSDRVR.LST` is the stock Miles
driver-selection message file, and the `.DIG` files there are Miles **sound card
drivers**, not game audio (see `file-formats.md`).

## Renderer

The engine uses its **own software rasterizer**. Neither the DOS nor the Windows
path asks the OS for any 3D capability — both only acquire a linear framebuffer
and write pixels into it. **[verified]**

- Colour depth is **16-bit hi-color**. `.SPR` sprite data is packed RGB555
  (`FF 7F` = 0x7FFF = white), and every HNM6 video header reports `bpp = 16`.
- Supported resolutions are 320x200, 640x480 and 800x600 — all three appear as
  literal strings in `WINDREAM.EXE`. The 3dfx build is locked to 640x480. **[sourced]**
- **Glide is the only hardware-accelerated path in the entire product, and it
  exists only in the DOS 3dfx build.** There is no Direct3D anywhere.

### Windows video API — DirectDraw only

Complete import table of `WINDREAM.EXE` / `GDIDREAM.EXE`: **[verified]**

```
DDRAW.dll     DirectDrawCreate
DSOUND.dll    DirectSoundCreate
GDI32.dll     CreateCompatibleDC, CreateDIBSection, DeleteDC, DeleteObject,
              GetStockObject, SelectObject, SetBkMode, StretchBlt
WINMM.dll     joyGetDevCapsA, joyGetNumDevs, joyGetPosEx,
              mciGetErrorStringA, mciSendCommandA, timeGetTime
USER32.dll    (22 functions)
KERNEL32.dll  (71 functions)
```

Only `DirectDrawCreate` is imported by name; everything past creation goes
through COM vtables, so the import table understates the usage. The error
strings reveal how deep it goes:

```
Can't start Direct Draw
Can't go to Direct Draw 2
Can't set DirectDraw mode
Can't create primary surface under DirectDraw
DIRECT DRAW ERROR
```

`Can't go to Direct Draw 2` means the engine `QueryInterface`s for
**IDirectDraw2** — the DirectX 2 interface — even though `README.TXT` instructs
the user to install DirectX 5. DirectDraw is used purely for 2D surface
management and mode setting; the software rasterizer does all the drawing.

The GDI path is the classic `CreateCompatibleDC` + `CreateDIBSection` +
`StretchBlt` windowed blit, which is why `GDIDREAM.EXE` runs in a window.

PE subsystem version is **3.10**, i.e. targeting Windows NT 3.1 / Win32s era
minimums.

### Consequences for modern systems

Three properties of this import table explain every symptom reported by the
community, and each has a different fix: **[verified]** observation,
**[sourced]** symptom reports.

1. **`mciSendCommandA` on the `cdaudio` device** is how music plays — straight
   off the redbook audio tracks. The `MCI Error` users see is the game's own
   string, raised when no CD audio device is present. Mounting a `.cue` (not a
   `.bin`, not an `.iso`) is what satisfies it.
2. **The animation/simulation delta uses a `timeGetTime`-based counter.**
   The executable also imports `QueryPerformanceCounter`; the earlier claim
   that `timeGetTime` was its only clock was incorrect. Its nominal animation
   scale is 30 frames/second, with timer quantization and delta clamps; see
   [animation-timing.md](animation-timing.md). Community reports describe
   harmful landing damage at high render rates. DOSBox `cycles` throttling or
   DxWnd emulated vsync are reported workarounds.
3. **Palettized DirectDraw mode-setting** is what actually breaks on Windows 11.
   The DWM offers no true 8-bit exclusive-fullscreen palette path, producing
   `Can't set DirectDraw mode` and `Can't create primary surface under DirectDraw`.

### Input — no mouse anywhere, polled keyboard **[verified]**

There is **no mouse-look and no mouse input of any kind**:

- The WndProc (`0x44627b`, window class `"Dreams to Reality"` registered in
  `0x4460cf`) handles exactly five messages: `WM_DESTROY`,
  `WM_SYSKEYDOWN`/`WM_SYSKEYUP` — only to test `wParam == VK_MENU` so holding
  Alt does not open the Windows system menu — and `WM_SYSCOMMAND` (blocks
  `SC_KEYMENU`). Everything else goes straight to `DefWindowProc`. No
  `WM_MOUSEMOVE`, no button messages, and not even `WM_KEYDOWN`.
- The keyboard is **polled**: `0x440757` calls `GetAsyncKeyState(0..255)`
  once per frame into a 256-byte state array at `0x6308d8` (bit 0 = held,
  bit 1 = press edge, bit 2 = release edge); `0x42493b` (called from the
  frame pump before the event queues) posts each new key press as event
  `0x33`.
- The only analog devices are **joysticks**: two polled devices (4-axis and
  3-axis) through the `joyGetPosEx` imports, posted as events `0x39`/`0x3a`
  on change.
- The Windows cursor is hidden immediately after window creation
  (`ShowCursor(0)` in `0x4460cf`).

Controls are therefore arrows + Ctrl/Alt/Space + number keys, or a joypad —
exactly as `README.TXT` §6 documents (also in-game F10 help). Camera views
are `Alt+5..0`, not mouse-driven. See [boot-sequence.md](boot-sequence.md)
for the frame pump that drives all of this.

## Subsystem naming

Recovered symbol fragments suggest a `<MODULE>_<Verb><Type>` convention:

- `DAN_Load3DC` — the `DAN` (animation) module loading a `3DC` geometry chunk.
  Note the cross-module call: animation data references geometry data.
- `Dreams_MSS1` — the audio module's Miles handle.

Format tags follow a matching four-character convention (`F3DC`, `DANF`, `DSNF`,
`DRDF`, `PAK0`, `UBIK`), which is consistent with a single shared chunk-IO layer
underneath all asset loading. See `file-formats.md`.

## External comparison leads [sourced]

- [ScummVM's CryOmni3D detection tables](https://github.com/scummvm/scummvm/blob/master/engines/cryomni3d/detection_tables.h)
  cover *Versailles 1685* and *Atlantis: The Lost Tales* assets such as HNM/UBB.
  This is useful for Cryo media comparison, but does not establish that their
  panoramic adventure engine shares *Dreams*' real-time `.DSN`/`.DAN` runtime.
- [NihAV Game Tool](https://nihav.org/game_tool.html) decodes Cryo HNM variants
  and extracts some Cryo archives. Use it as a reference for shared media
  formats; keep the game's 3D and skeletal formats as separate questions.
- The [1997 Génération 4 issue 100 archive](https://www.abandonware-magazines.org/affiche_mag.php?album=oui&mag=27&num=491)
  indexes its *Dreams to Reality* preview at page 148. Period previews and the
  [original manual archive](https://www.abandonware-france.org/ltf_abandon/ltf_jeu.php?fic=liens&id=1965)
  are useful for validating controls and observed animation states.
- In a [first-hand DxWnd investigation](https://sourceforge.net/p/dxwnd/discussion/general/thread/a2ddabcd22/),
  its maintainer reports that `WINDREAM.EXE` and `GDIDREAM.EXE` use different
  presentation paths and that emulated vsync prevents erroneous landing damage
  on modern hardware. The same thread flags `DATA/3DC/DESCRIPT.ION`; the local
  copies contain filename-level character hints, documented in `models.md`.
- A [2026 first-hand patch report](https://www.abandonware-forums.org/forum/forum-ltf-abandonware-france/aide-de-jeux-probl%C3%A8mes-techniques/919445-dreams-to-reality-%E2%80%93-restauration-am%C3%A9lioration-de-l-ombre-anim%C3%A9e-en-mode-3dfx)
  describes Duncan's animated ground shadow as following his limbs and braid.
  If reproduced in an original build, the shadow could help compare our
  skeleton poses frame by frame. The report does not supply a `.DAN` decoder.

This search did not verify another game using this exact `.DSN`/`.DAN` engine.
Compare format magic, binary imports, and node structures before treating
another Cryo title as an engine sibling.

## Reproducing this analysis

Import table and section dump:

```bash
python - "WINDREAM.EXE" << 'EOF'
# parse e_lfanew -> PE header -> data directory[1] (imports)
# walk IMAGE_IMPORT_DESCRIPTOR chain, resolve RVA->file offset via section table
EOF
```

String mining:

```bash
python -c "
import re,sys
t=open(sys.argv[1],'rb').read().decode('latin1')
for m in sorted(set(re.findall(r'[ -~]{6,}',t))):
    if re.search(r'WATCOM|Glide|Direct|CRYO|Miles|UniVBE',m): print(m)
" WINDREAM.EXE
```
