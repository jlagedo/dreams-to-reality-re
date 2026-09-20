# Running the game

**Nothing here has been verified on this machine yet.** The rankings and configs
below come from community reports plus the binary analysis in `engine.md`.
**[sourced]** unless marked otherwise.

## Why it is hard

Three properties of the Windows build, all confirmed from its import table
(`engine.md`), account for essentially every failure mode:

1. Palettized **DirectDraw** mode-setting, which Windows 11's DWM cannot provide
   in true exclusive fullscreen.
2. **`timeGetTime` with no frame limiter** — above ~30 FPS the physics break and
   a routine landing can kill the player.
3. **MCI `cdaudio`** for music, which needs a real CD device with audio tracks.

Plus a disc check (`disc-layout.md`) and a hardcoded `C:\CRYO\DREAMS` install path.

---

## 1. DOS build in DOSBox — easiest

`DREAMS.EXE`. DOSBox neutralises three of the four problems for free:

- **`imgmount` with the `.cue`** presents the data track *and* all audio tracks as
  a genuine CD. The disc check passes, redbook music plays, no MCI involvement.
- **`cycles` throttling** is the clean fix for the frame-rate physics bug — it
  slows the emulated CPU rather than fighting the game's timing.
- **No DirectDraw exists** in this path at all.

What remains is VESA. The DOS build drives SciTech UniVBE, which is the fussy
part.

### Starting config

```ini
[sdl]
fullscreen=false
output=opengl

[dosbox]
machine=svga_s3          ; reported necessary; S3 Trio64 emulation
memsize=32

[cpu]
core=dynamic
cycles=fixed 20000       ; tune DOWN if the player dies on landing

[autoexec]
imgmount d "E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\Dreams to Reality (Europe) (Disc 1).cue" -t iso
mount c "E:\games\dreams"
c:
cd \CRYO\DREAMS
dreams.exe
```

Install first by running `INSTALL.BAT` from the mounted CD; it is interactive and
asks for a destination drive letter via `GETKEY.EXE`.

**Cycle tuning is the key variable.** Start low (10000-20000) and raise until the
game feels right. If the character dies instantly on the intro landing, cycles
are too high.

### Which DOSBox

| Build | Verdict |
|---|---|
| **DOSBox-X** | try first — broadest VESA/hardware emulation, actively maintained |
| **DOSBox Daum** | known-good fallback. eXoDOS ships *this specific fork* for this title because it carries fixes absent from upstream, X, Pure and Staging |
| DOSBox Staging | [issue #2888](https://github.com/dosbox-staging/dosbox-staging/issues/2888) tracked whether the Daum fixes were still needed; closed, but the page did not state the outcome |
| DOSBox 0.74 | weakest option |

Reported symptoms on unsuitable builds: flickering main menu and in-game icons,
dialogs that lock up, and crashes when loading new stages.

---

## 2. DOS 3dfx build — best looking

`DREAMSFX.EXE`. The **only hardware-accelerated path in the entire product**.
VOGONS reports it "runs very well" with gulikoza's patched DOSBox plus dgVoodoo.
Locked to 640x480.

Extra setup: the Glide runtime needs `glide2x.ovl`, which is packed inside
`3DFX\GRTVGR.EXE` — a PKZIP archive despite the `.exe` extension.

```bash
# either
7z x "3DFX/GRTVGR.EXE" -o./glide
# or, the PCGamingWiki incantation
pkunzip d:\3dfx\grtvgr.exe glide\drivers\voodoo\dos\glide2x.ovl
```

Then install with `INSTALFX.BAT` instead of `INSTALL.BAT`.

Requires a Glide-capable DOSBox (gulikoza's build, or DOSBox ECE) with dgVoodoo
as the wrapper. GLIDOS is an alternative wrapper for the same executable.

---

## 3. Windows build — hardest

`WINDREAM.EXE`, or `GDIDREAM.EXE` for the windowed GDI path, which is the
least-bad native variant. The DxWnd thread author's verdict was "a total mess."

What was eventually made to work:

- **Merge both CDs into one directory** (see `disc-layout.md` for the 10-file
  conflict map).
- **DxWnd CD-directory emulation** — plays with no physical CD, everything on the
  hard disk. The DxWnd author added `.flac` music support for this case.
- **Enable emulated vsync** — without it the protagonist dies instantly on landing
  during the intro.
- Expect `MCI Error` dialogs otherwise; that string comes from the game itself.

Additional notes:

- Set Properties → Compatibility → **Windows 95** or **98**.
- The 32-bit executable may need the **LAA flag** applied on modern machines.
- `SETUP.EXE` expects to install to `C:\CRYO\DREAMS`. The DxWnd reporter found the
  installer simply failed and extracted files manually using `SETUP.INI` as the
  manifest — that manifest is transcribed in `disc-layout.md`.
- Audio track switching between CDs remained problematic even after merging.

---

## Known behaviours

From [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Dreams_to_Reality):

- Resolutions **320x200 / 640x480 / 800x600** in the DOS and Windows builds; 3dfx
  locked to 640x480. (All three strings are present in `WINDREAM.EXE` — **[verified]**)
- **Above 30 FPS breaks physics.** A simple landing can cost most of the player's
  health or kill them.
- **Autosave only** — the game saves on entering each new section, to a separate
  slot. There is no manual save.
- **Game options reset on every run.**
- Hold **F10** in game to view the control scheme.
- DirectDraw is the only Windows API; Glide 1 in DOS; software renderer otherwise.

## Original system requirements

| | Minimum | Recommended |
|---|---|---|
| OS | Windows NT 4.0 or DOS 6.22 | Windows 95 |
| CPU | Intel Pentium | — |
| RAM | 16 MB | 32 MB |
| Disk | 500 MB | 1 GB |
| GPU | SVGA/VESA/Glide | Direct3D/Glide |

`README.TXT` recommends running the DOS version under *real* DOS rather than an
MS-DOS session inside Windows.

## Discs

`README.TXT` describes three separate installers, one per build — Windows
(`SETUP.EXE`), DOS (`INSTALL.BAT`), 3dfx (`INSTALFX.BAT`) — and says to use the
matching one. Mini installs read almost everything from CD; maxi installs copy
more to disk and "also offers video-maps in the game".

Both discs are present locally. The game prompts `Insérer le CD 2.` when it needs
the second one.
