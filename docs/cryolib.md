# CryoLib (`CRYO.DLL`)

**This resolves the project's highest-priority open question, and it resolves it
better than expected: a working HNM6 decoder ships on disc 2.**

> **Update — CryoLib is no longer the best route to the video.** Open-source
> decoders exist (NihAV, ScummVM) and decode all 113 files on these discs today,
> and Cryo's own `CM6_*x16.dll` is publicly archived with only 36 KB of `.text` —
> a far cleaner RE target than this 644 KB DLL. See
> [hnm-video.md](hnm-video.md). CryoLib remains the best available guide to
> Cryo's naming and structure conventions, and the only one of the three that
> also documents the `.UBB` and BigFile APIs.

`DEMOS2\CRYO.DLL`, 644,608 bytes. **[verified]**

## Identity

The PDB path left in the binary names the library outright:

```
E:\visual\CryoLib\debug\cryo.pdb
```

So this is **CryoLib**, Cryo's reusable platform/multimedia library. Two further
things fall out of that string:

- It was built with **Microsoft Visual C++**, not Watcom. The strings `Microsoft
  Visual C++ Debug Library` and `Microsoft Visual C++ Runtime Library` are both
  present.
- The path segment is `debug`. **A debug build of CryoLib shipped on the retail
  disc.** Symbol-rich and far easier to reverse than a release build.

165 exported functions, all prefixed `GL_`.

## CryoLib is *not* the game engine

This is the important structural distinction. `WINDREAM.EXE` does **not** import
`CRYO.DLL` — its only imports are `DDRAW`, `DSOUND`, `WINMM`, `GDI32`, `USER32`
and `KERNEL32` (see [engine.md](engine.md)). The only consumers of `CRYO.DLL` on
these discs are `PLAYUBB.EXE` and `PLAYTGA.EXE`, the two bonus-content players,
and both import it **by ordinal** rather than by name.

So the discs carry **two independent codebases**: **[verified]**

| | Game engine | CryoLib |
|---|---|---|
| Binary | `WINDREAM.EXE` / `DREAMS.EXE` / `DREAMSFX.EXE` | `CRYO.DLL` |
| Compiler | Watcom C/C++ | Microsoft Visual C++ (debug) |
| Linkage | static, self-contained | shared DLL |
| Used by | the game | `PLAYUBB.EXE`, `PLAYTGA.EXE` |
| Naming | `DAN_Load3DC` | `GL_*` |

CryoLib is the library Cryo standardised on for their later Windows titles. Its
presence here is incidental — it ships to run the bonus demos — but it is by far
the most useful binary on either disc, because it implements the same media
formats the game uses.

## The HNM6 decoder

Exported by name: **[verified]**

```
_GL_HNM6_Decompression_Warp@8
_GL_HNM6_Init_All_15@0
_GL_HNM6_Init_All_16@0
```

`_15` and `_16` are the RGB555 and RGB565 output paths, matching the engine's
16-bit hi-color rendering. The `@8` / `@0` suffixes are stdcall decorations, so
`Decompression_Warp` takes 8 bytes of arguments — most likely two pointers or a
pointer and a length.

Note **only the WARP variant is exported**. Per the
[HNM6 spec](hnm6-spec.md) WARP is the prototype codec used by "a very little
number of games circa 1997" — which is exactly this game's vintage. Whether the
cutscenes on these discs are WARP or Normal is **[unverified]**; if Normal, the
decoder for it is present but internal, not exported.

A stray string `PlayHnm5` also appears, implying HNM5 support somewhere in the
lineage.

### Full HNM/UBB API surface

```
GL_OpenHnm              GL_CloseHnm
GL_InitHnmScreen        GL_FreeHnmScreen
GL_PlayHnmDDRAW         GL_PlayHnmGDI            <- two output backends
GL_InitHnmSound         GL_StartHnmSound
GL_StopHnmSound         GL_CloseHnmSound
GL_AdjustFreqSoundHnm   GL_DcptOneBlockSound     <- "dcpt" = decompact
GL_dcpt_one_frame_ubb   GL_load_one_block_ubb    GL_search_blocks
```

Note the two playback backends mirror the game's own DirectDraw/GDI split — the
same architectural choice, independently implemented.

`GL_DcptOneBlockSound` is the APC audio path (`dcpt` = *décompacter*).

### Practical exploitation

Two routes, both far cheaper than writing a decoder from the spec:

1. **Call it.** Write a small Win32 host that `LoadLibrary`s `CRYO.DLL` and drives
   `GL_OpenHnm` → `GL_InitHnmScreen` → `GL_PlayHnmGDI`, dumping frames. The DLL is
   32-bit, so the host must be 32-bit. This is the fastest path to viewable video.
2. **Reverse it.** A debug build with 165 named exports is a near-ideal target.
   `_GL_HNM6_Decompression_Warp@8` can be read directly against the published
   bitstream spec in [hnm6-spec.md](hnm6-spec.md) to confirm or correct it.

**[unverified]** — neither attempted yet. Risk: the DLL may be built against the
demo content's specific HNM revision rather than the game's.

## Other subsystems worth knowing about

The export list is effectively a map of what a 1997 Cryo title needed.

### BigFile archive format

```
GL_OpenBigFile      GL_CloseBigFile      GL_CreateBigFile
GL_OpenFileBigFile  GL_ReadFileBigFile   GL_CloseFileBigFile
GL_GetFileSizeBigFile  GL_ReplaceFileBigFile  SetBigFileDebug
```

A full read/write virtual-filesystem API. `SETUP.INI` references `.big` files in
the demo directories (`demos1\ubik\ubik.big`, `3mill.big`, `atlant.big`), so
BigFile is Cryo's archive container. The game itself does not appear to use it —
`Dreams to Reality` ships loose files. **[verified]** that the API exists;
**[unverified]** as to the on-disk format.

### LZW compression

```
GL_PackLZW  GL_UnpackLZW  GL_LoadPackLZW  GL_SavePackLZW
```

with the literal signature string **`LZWCRYO`**. If any game asset turns out to
be LZW-compressed, that tag is what to search for.

Also `GL_ArjFile` and `GL_ZipFile` — ARJ and ZIP handling.

### Graphics / DirectDraw

```
GL_OpenDdraw  GL_CloseDdraw  GL_TestIfDdraw  GL_SetDdrawCooperativeLevel
GL_BlitDdrawScreen  GL_BlitScreen  GL_BlitImage  GL_FlipScreen  GL_FlipToGDI
GL_InitAllScreen  GL_ResetAllScreen  GL_SelectScreen  GL_GetNumLogicalScreen
GL_LockScreen  GL_UnlockScreen  GL_LockLogicalScreen  GL_UnlockLogicalScreen
GL_ClsScreen  GL_ClsLogicalScreen  GL_CopyEcran  GL_ViewEcran
GL_ConvertTrueColors  GL_RestoreSurfaces  GL_ToggleWinFullScreen
GL_Box  GL_PBox  GL_Line  GL_Texte  GL_TexteOpaque  GL_SetGraphColor
```

A logical-screen abstraction over DirectDraw with a GDI fallback — again the same
dual-backend pattern. `Ecran` is French for screen; `GL_CopyEcran` and
`GL_ViewEcran` sit alongside English-named functions, matching the mixed-language
sloppiness seen throughout the project.

Error string: `Erreur Set Display Mode`, `DDRAW ERROR`.

### Sound

```
GL_AllocDirectSound  GL_InitDirectSound  GL_LoadDirectSound  GL_FreeDirectSound
GL_PlayDirectSound   GL_StopDirectSound  GL_SetDirectSoundVolume
GL_SetDirectSoundPan GL_SetDirectSoundFrequency  GL_GetDirectSoundPosition
GL_SetDirectSoundPosition  GL_GetDirectSoundStatus  GL_LockDirectSoundAdr
GL_FreeDirectSoundAdr  GL_LockBufferSound  GL_UnlockBufferSound
GL_OpenWAV  GL_CloseWAV  GL_LoadWAV  GL_LoadWav  GL_SaveWAV  GL_GetWAVHeader
GL_PlayWav  GL_PlayWavDirectSound  GL_IsWavFinished
InitAudioCard  CloseAudioCard
```

Note `GL_LoadWAV` and `GL_LoadWav` both exist — two spellings of the same
operation, exported separately. Another sign of an unpoliced codebase.

### TGA

```
GL_LoadTga  GL_SaveTga  GL_GetTgaInfo  GL_SetTgaInfo  GL_LoadTgaInfo
```

This is what `PLAYTGA.EXE` drives.

### Files, registry, process, input

```
GL_FileOpen  GL_FileClose  GL_FileRead  GL_FileWrite  GL_FileGetLen
GL_FileCopy  GL_FileBrowse  GL_FileMoveIn  GL_FileRelatifMoveIn
GL_FilePackAndCopy  GL_CreateFile  GL_ReadFile  GL_IsFileExist
GL_GetFileSize  GL_SetFilePointer  GL_CloseFileMapping  GL_CloseHandle

GL_CreateKey  GL_DeleteKey  GL_GetKeyStrValue  GL_SetKeyStrValue
GL_TestIfExistKey                                   <- Windows registry

GL_CallExe  GL_ShellExec  GL_IsExeRunning  GL_TestAppAlreadyRunning
GL_InitTestIfExeRunning  GL_ExecRoutine  GL_QuitWithError
GL_QuitWithErrorCode  GL_QuitWithoutError  GL_SetErrorMode  GL_SetDebugFlag

GL_KbHit  GL_KeyHit  GL_WaitKey  GL_GetLastKey  GL_ClrKeybTable
GL_SetKeybTable  GL_MouseX  GL_MouseY  GL_MouseK  GL_ShowMouse  GL_HideMouse

GL_GetTime  GL_WaitTime  GL_GetMessages  GL_WindowProc  GL_SetHwnd
GL_SethInstance  GL_IsAppActive  GL_WaitForAppActive  GL_IconifyWindow
```

Imports include `QueryPerformanceCounter` / `QueryPerformanceFrequency` — a
higher-resolution clock than the game engine's bare `timeGetTime`, and a reminder
that the frame-pacing bug is a property of the *game*, not of Cryo's tooling.

## Ordinal imports

`PLAYUBB.EXE` and `PLAYTGA.EXE` bind by ordinal, not name:

```
PLAYUBB.EXE  #65, #151, #138, #127, #131, #123, #53, #58, #12, #92, #68, #100, #13, #134 (+2)
PLAYTGA.EXE  #53, #80, #138, #127, #131, #123, #65, #92, #75, #88, #57, #60, #43, #6 (+11)
```

Since all 165 exports are named and the export table carries both arrays,
ordinals map cleanly back to names via the export address table. Doing that
mapping would reveal the exact minimal call sequence needed to play a `.UBB` —
a ready-made worked example. **Not yet done; cheap and high value.**

Shared ordinals `#123`, `#127`, `#131`, `#138`, `#53`, `#65`, `#92` appear in both
players and are therefore the common init/shutdown/screen scaffolding.
