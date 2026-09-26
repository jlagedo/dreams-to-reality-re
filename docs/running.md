# Running the game

**The 3dfx build (section 2) runs on this machine — [verified] 2026-09-25.**
Everything else still comes from community reports plus the binary analysis in
`engine.md`. **[sourced]** unless marked otherwise.

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

## 2. DOS 3dfx build — best looking, verified

`DREAMSFX.EXE`. The **only hardware-accelerated path in the entire product**.
Locked to 640x480.

**[verified]** Runs under **DOSBox Staging 0.83.0** using its built-in Voodoo 1
emulation — no Glide wrapper, no patched DOSBox. Install, 3dfx logo, in-game
Voodoo rendering (bilinear-filtered textures) and CD music from the `.cue`
audio tracks all work. Not yet checked: the landing-physics bug at this cycle
count, disc 2 swapping, save/load.

### Setup

1. **Install DOSBox Staging to an ASCII-only path.** 0.83.0 crashes at startup
   (exit `0xC0000409` in `ucrtbase.dll`, with any config or none) when its own
   directory contains non-ASCII characters — `winget install
   DOSBoxStaging.DOSBoxStaging` lands in `%LOCALAPPDATA%\Programs\DOSBox
   Staging`, which breaks under a profile like `C:\Users\João ...`. Copying the
   install folder to e.g. `E:\games\dosbox-staging` fixes it. An empty
   `dosbox-staging.conf` next to `dosbox.exe` enables portable mode, keeping
   its config there too.
2. **Extract `glide2x.ovl`** from `3DFX\GRTVGR.EXE` (a PKZIP archive despite
   the extension) — the member is `Glide/Drivers/Voodoo/Dos/glide2x.ovl`
   (348,791 bytes); not the `Vrush` or Win95 ones. Any unzip works, including
   Python's `zipfile`.
3. **Copy it into the install directory yourself.** `INSTALFX.BAT` ->
   `INST_SFX.BAT` never copies it. Pre-creating `C:\CRYO\DREAMS\GLIDE2X.OVL`
   before installing works; `xcopy` leaves it alone.
4. **Run `INSTALFX.BAT`** from the CD: press `C` for the drive, configure
   `SETSOUND.EXE` as Sound Blaster 16 (Staging's defaults: 220 / IRQ 7 / DMA 1 /
   HDMA 5), then answer the "Minimum install?" prompt. Both answers copy the
   same files; **No** only adds `DATA\FULL.ID` (the maxi marker, see
   `disc-layout.md`). The script then launches `DREAMSFX.EXE`.

### Config

```ini
[sdl]
fullscreen = false

[render]
shader = crt-auto        ; Staging's default CRT look; 'sharp' removes the scanlines

[dosbox]
machine = svga_s3
memsize = 32

[cpu]
core = dynamic
cputype = pentium_mmx    ; newest type Staging offers; the game needs only a Pentium
cpu_cycles = 200000      ; ~Pentium II 266-300; tune DOWN if the player dies on landing

[voodoo]
voodoo = true
voodoo_memsize = 4

[autoexec]
imgmount d "...\Dreams to Reality (Europe) (Disc 1).cue" "...\Dreams to Reality (Europe) (Disc 2).cue" -t cdrom
mount c "E:\games\dreams"
c:
if exist C:\CRYO\DREAMS\DREAMSFX.EXE goto play
d:
INSTALFX.BAT
goto end
:play
cd \CRYO\DREAMS
DREAMSFX.EXE
:end
```

Both discs are mounted on D:; **Ctrl+F4** swaps to the next image. The same
config installs on first run and plays afterwards. In 0.83 `glshader` is a
deprecated alias for `shader`; `cycles` is now `cpu_cycles`.

At 20000 cycles the game ran slowly; 200000 on an i9 is smooth, but has not
yet been checked against the landing bug. **Ctrl+F11/F12** lower/raise cycles
live. The same install also runs the software build: copy the config, set
`voodoo = false` and launch `DREAMS.EXE`. It works, but it is point-sampled
and far blockier than the Voodoo output.

What the output looks like: filtered textures and Gouraud shading from the
emulated Voodoo, but hard aliased polygon edges and visible 16-bit dithering at
640x480 — that is authentic Voodoo 1 output, not a misconfiguration.

### Alternatives

For higher internal resolution, Glide passthrough to **dgVoodoo2** on the host
GPU is the route: DOSBox-X (`glide=true`), or the older gulikoza build / DOSBox
ECE that VOGONS reports "runs very well". Passthrough uses the emulator's own
`glide2x.ovl` stub, not the game's. GLIDOS is another wrapper for the same
executable. None of these are tested here.

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

## Controllers

**The DOS and 3dfx builds cannot use a joystick** — they contain no joystick
code at all (`engine.md`, *DOS builds: no joystick code*). DOSBox Staging
detects a PS5 DualSense fine (`MAPPER: Initialised DualSense Wireless
Controller with 6 axes, 17 buttons, and 0 hat(s)`), and the game still offers a
`J` joypad mode, but nothing reads it. **[verified]**

Under DOSBox, map the pad to keys instead (**Ctrl+F1**, click a key, **Add**,
press the pad button, **Save**). A layout for the README controls:

| DualSense | Key | Action |
|---|---|---|
| Left stick / D-pad | arrows | move |
| Cross | Ctrl | jump / kick |
| Square | Alt | punch / walk / fly / sword |
| Triangle | Space | combat mode |
| Circle | Down | defend |
| L1 / R1 / L2 | 1 / 2 / 3 | magic pre-select |
| Options | Esc | menu |

**The Windows build does read a joystick** through WinMM `joyGetPosEx`. Press
`J` in game; the stick is centred on its position at launch, so leave it at
rest while the game starts. Only X/Y and buttons are used on that path. Not yet
tried on this machine.

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
- DirectDraw is the only Windows API; Glide in DOS; software renderer otherwise.
  (PCGamingWiki says "Glide 1"; the binary loads `glide2x.ovl` and uses the
  Glide 2 API — `engine.md`. **[verified]**)

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
