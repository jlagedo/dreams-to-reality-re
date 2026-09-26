/* DirectDraw and DirectSound COM interfaces used by WINDREAM.EXE / GDIDREAM.EXE.
 *
 * Minimal declarations for Ghidra's C parser (ghidra_scripts/ApplyTypes.java),
 * so vtable calls decompile as lpVtbl->Lock(...) instead of *(code **)(+0x64).
 * The game links DirectX 3 era interfaces: IDirectDraw (DirectDrawCreate),
 * IDirectDraw2 (QueryInterface), IDirectDrawSurface, IDirectSound and
 * IDirectSoundBuffer. Method order was generated from Wine's include/ddraw.h
 * and include/dsound.h, checked identical to Microsoft's ddraw.h/dsound.h
 * (Windows SDK 10.0.26100), and matches the offsets the game uses (Lock
 * +0x64, Unlock +0x80, Flip +0x2c, SetDisplayMode +0x54, CreateSoundBuffer
 * +0x0c). The SDK headers themselves need windows.h/objbase.h and SAL, which
 * Ghidra's C parser handles poorly; hence this standalone file.
 * Structure layouts are the DirectX 3 SDK sizes; parameters whose structures
 * the game never passes (DDCAPS, DDBLTFX, ...) are void *.
 */
#ifndef DREAMS_DIRECTX_H
#define DREAMS_DIRECTX_H

typedef unsigned long  DWORD;
typedef unsigned long  ULONG;
typedef long           LONG;
typedef long           HRESULT;
typedef int            BOOL;
typedef unsigned short WORD;
typedef void          *HANDLE;
typedef void          *HWND;
typedef void          *HDC;

typedef struct GUID { DWORD Data1; WORD Data2; WORD Data3; unsigned char Data4[8]; } GUID;
typedef struct RECT { LONG left; LONG top; LONG right; LONG bottom; } RECT;
typedef struct PALETTEENTRY { unsigned char peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;

typedef struct DDSCAPS { DWORD dwCaps; } DDSCAPS;
typedef struct DDCOLORKEY { DWORD dwColorSpaceLowValue; DWORD dwColorSpaceHighValue; } DDCOLORKEY;

/* 0x20 bytes. */
typedef struct DDPIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;              /* DDPF_RGB 0x40 */
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;           /* 0xf800 (565) or 0x7c00 (555) in DDRAW_SetMode */
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} DDPIXELFORMAT;

/* 0x6c bytes; the game sets dwSize = 0x6c. */
typedef struct DDSURFACEDESC {
    DWORD dwSize;
    DWORD dwFlags;              /* DDSD_CAPS 1, DDSD_BACKBUFFERCOUNT 0x20, DDSD_PIXELFORMAT 0x1000 */
    DWORD dwHeight;
    DWORD dwWidth;
    LONG  lPitch;
    DWORD dwBackBufferCount;
    DWORD dwRefreshRate;        /* union with dwMipMapCount */
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    void *lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;            /* DDSCAPS_PRIMARYSURFACE|FLIP|COMPLEX = 0x218 */
} DDSURFACEDESC;

#pragma pack(push, 1)
/* 18 bytes, packed. */
typedef struct WAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX;
#pragma pack(pop)

/* DirectX 3 form, 0x14 bytes; the game sets dwSize = 0x14. */
typedef struct DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;              /* DSBCAPS_CTRLFREQUENCY 0x20|CTRLPAN 0x40|CTRLVOLUME 0x80 */
    DWORD dwBufferBytes;
    DWORD dwReserved;
    WAVEFORMATEX *lpwfxFormat;
} DSBUFFERDESC;

typedef struct DSBCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwUnlockTransferRate;
    DWORD dwPlayCpuOverhead;
} DSBCAPS;

typedef struct DSCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwMinSecondarySampleRate;
    DWORD dwMaxSecondarySampleRate;
    DWORD dwPrimaryBuffers;
    DWORD dwMaxHwMixingAllBuffers;
    DWORD dwMaxHwMixingStaticBuffers;
    DWORD dwMaxHwMixingStreamingBuffers;
    DWORD dwFreeHwMixingAllBuffers;
    DWORD dwFreeHwMixingStaticBuffers;
    DWORD dwFreeHwMixingStreamingBuffers;
    DWORD dwMaxHw3DAllBuffers;
    DWORD dwMaxHw3DStaticBuffers;
    DWORD dwMaxHw3DStreamingBuffers;
    DWORD dwFreeHw3DAllBuffers;
    DWORD dwFreeHw3DStaticBuffers;
    DWORD dwFreeHw3DStreamingBuffers;
    DWORD dwTotalHwMemBytes;
    DWORD dwFreeHwMemBytes;
    DWORD dwMaxContigFreeHwMemBytes;
    DWORD dwUnlockTransferRateHwBuffers;
    DWORD dwPlayCpuOverheadSwBuffers;
    DWORD dwReserved1;
    DWORD dwReserved2;
} DSCAPS;

typedef struct IDirectDraw IDirectDraw;
typedef struct IDirectDraw2 IDirectDraw2;
typedef struct IDirectDrawSurface IDirectDrawSurface;
typedef struct IDirectDrawPalette IDirectDrawPalette;
typedef struct IDirectDrawClipper IDirectDrawClipper;
typedef struct IDirectSound IDirectSound;
typedef struct IDirectSoundBuffer IDirectSoundBuffer;

/* IDirectDraw: 23 methods. */
struct IDirectDrawVtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectDraw *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectDraw *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectDraw *This); /* +0x08 */
    HRESULT (__stdcall *Compact)(IDirectDraw *This); /* +0x0c */
    HRESULT (__stdcall *CreateClipper)(IDirectDraw *This, DWORD flags, IDirectDrawClipper **clipper, void *outer); /* +0x10 */
    HRESULT (__stdcall *CreatePalette)(IDirectDraw *This, DWORD flags, PALETTEENTRY *color_table, IDirectDrawPalette **palette, void *outer); /* +0x14 */
    HRESULT (__stdcall *CreateSurface)(IDirectDraw *This, DDSURFACEDESC *surface_desc, IDirectDrawSurface **surface, void *outer); /* +0x18 */
    HRESULT (__stdcall *DuplicateSurface)(IDirectDraw *This, IDirectDrawSurface *src_surface, IDirectDrawSurface **dst_surface); /* +0x1c */
    HRESULT (__stdcall *EnumDisplayModes)(IDirectDraw *This, DWORD flags, DDSURFACEDESC *surface_desc, void *ctx, void * cb); /* +0x20 */
    HRESULT (__stdcall *EnumSurfaces)(IDirectDraw *This, DWORD flags, DDSURFACEDESC *surface_desc, void *ctx, void * cb); /* +0x24 */
    HRESULT (__stdcall *FlipToGDISurface)(IDirectDraw *This); /* +0x28 */
    HRESULT (__stdcall *GetCaps)(IDirectDraw *This, void *driver_caps, void *hel_caps); /* +0x2c */
    HRESULT (__stdcall *GetDisplayMode)(IDirectDraw *This, DDSURFACEDESC *surface_desc); /* +0x30 */
    HRESULT (__stdcall *GetFourCCCodes)(IDirectDraw *This, DWORD * lpNumCodes, DWORD * lpCodes); /* +0x34 */
    HRESULT (__stdcall *GetGDISurface)(IDirectDraw *This, IDirectDrawSurface **surface); /* +0x38 */
    HRESULT (__stdcall *GetMonitorFrequency)(IDirectDraw *This, DWORD * lpdwFrequency); /* +0x3c */
    HRESULT (__stdcall *GetScanLine)(IDirectDraw *This, DWORD * lpdwScanLine); /* +0x40 */
    HRESULT (__stdcall *GetVerticalBlankStatus)(IDirectDraw *This, BOOL *lpbIsInVB); /* +0x44 */
    HRESULT (__stdcall *Initialize)(IDirectDraw *This, GUID *lpGUID); /* +0x48 */
    HRESULT (__stdcall *RestoreDisplayMode)(IDirectDraw *This); /* +0x4c */
    HRESULT (__stdcall *SetCooperativeLevel)(IDirectDraw *This, HWND hWnd, DWORD dwFlags); /* +0x50 */
    HRESULT (__stdcall *SetDisplayMode)(IDirectDraw *This, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP); /* +0x54 */
    HRESULT (__stdcall *WaitForVerticalBlank)(IDirectDraw *This, DWORD dwFlags, HANDLE hEvent); /* +0x58 */
};
struct IDirectDraw { struct IDirectDrawVtbl *lpVtbl; };

/* IDirectDraw2: 24 methods. */
struct IDirectDraw2Vtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectDraw2 *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectDraw2 *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectDraw2 *This); /* +0x08 */
    HRESULT (__stdcall *Compact)(IDirectDraw2 *This); /* +0x0c */
    HRESULT (__stdcall *CreateClipper)(IDirectDraw2 *This, DWORD flags, IDirectDrawClipper **clipper, void *outer); /* +0x10 */
    HRESULT (__stdcall *CreatePalette)(IDirectDraw2 *This, DWORD flags, PALETTEENTRY *color_table, IDirectDrawPalette **palette, void *outer); /* +0x14 */
    HRESULT (__stdcall *CreateSurface)(IDirectDraw2 *This, DDSURFACEDESC *surface_desc, IDirectDrawSurface **surface, void *outer); /* +0x18 */
    HRESULT (__stdcall *DuplicateSurface)(IDirectDraw2 *This, IDirectDrawSurface *src_surface, IDirectDrawSurface **dst_surface); /* +0x1c */
    HRESULT (__stdcall *EnumDisplayModes)(IDirectDraw2 *This, DWORD flags, DDSURFACEDESC *surface_desc, void *ctx, void * cb); /* +0x20 */
    HRESULT (__stdcall *EnumSurfaces)(IDirectDraw2 *This, DWORD flags, DDSURFACEDESC *surface_desc, void *ctx, void * cb); /* +0x24 */
    HRESULT (__stdcall *FlipToGDISurface)(IDirectDraw2 *This); /* +0x28 */
    HRESULT (__stdcall *GetCaps)(IDirectDraw2 *This, void *driver_caps, void *hel_caps); /* +0x2c */
    HRESULT (__stdcall *GetDisplayMode)(IDirectDraw2 *This, DDSURFACEDESC *surface_desc); /* +0x30 */
    HRESULT (__stdcall *GetFourCCCodes)(IDirectDraw2 *This, DWORD * lpNumCodes, DWORD * lpCodes); /* +0x34 */
    HRESULT (__stdcall *GetGDISurface)(IDirectDraw2 *This, IDirectDrawSurface **surface); /* +0x38 */
    HRESULT (__stdcall *GetMonitorFrequency)(IDirectDraw2 *This, DWORD * lpdwFrequency); /* +0x3c */
    HRESULT (__stdcall *GetScanLine)(IDirectDraw2 *This, DWORD * lpdwScanLine); /* +0x40 */
    HRESULT (__stdcall *GetVerticalBlankStatus)(IDirectDraw2 *This, BOOL *lpbIsInVB); /* +0x44 */
    HRESULT (__stdcall *Initialize)(IDirectDraw2 *This, GUID *lpGUID); /* +0x48 */
    HRESULT (__stdcall *RestoreDisplayMode)(IDirectDraw2 *This); /* +0x4c */
    HRESULT (__stdcall *SetCooperativeLevel)(IDirectDraw2 *This, HWND hWnd, DWORD dwFlags); /* +0x50 */
    HRESULT (__stdcall *SetDisplayMode)(IDirectDraw2 *This, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags); /* +0x54 */
    HRESULT (__stdcall *WaitForVerticalBlank)(IDirectDraw2 *This, DWORD dwFlags, HANDLE hEvent); /* +0x58 */
    HRESULT (__stdcall *GetAvailableVidMem)(IDirectDraw2 *This, DDSCAPS *caps, DWORD *total, DWORD *free); /* +0x5c */
};
struct IDirectDraw2 { struct IDirectDraw2Vtbl *lpVtbl; };

/* IDirectDrawSurface: 36 methods. */
struct IDirectDrawSurfaceVtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectDrawSurface *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectDrawSurface *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectDrawSurface *This); /* +0x08 */
    HRESULT (__stdcall *AddAttachedSurface)(IDirectDrawSurface *This, IDirectDrawSurface *attachment); /* +0x0c */
    HRESULT (__stdcall *AddOverlayDirtyRect)(IDirectDrawSurface *This, RECT * lpRect); /* +0x10 */
    HRESULT (__stdcall *Blt)(IDirectDrawSurface *This, RECT *dst_rect, IDirectDrawSurface *src_surface, RECT *src_rect, DWORD flags, void *fx); /* +0x14 */
    HRESULT (__stdcall *BltBatch)(IDirectDrawSurface *This, void *batch, DWORD count, DWORD flags); /* +0x18 */
    HRESULT (__stdcall *BltFast)(IDirectDrawSurface *This, DWORD x, DWORD y, IDirectDrawSurface *src_surface, RECT *src_rect, DWORD flags); /* +0x1c */
    HRESULT (__stdcall *DeleteAttachedSurface)(IDirectDrawSurface *This, DWORD flags, IDirectDrawSurface *attachment); /* +0x20 */
    HRESULT (__stdcall *EnumAttachedSurfaces)(IDirectDrawSurface *This, void *ctx, void * cb); /* +0x24 */
    HRESULT (__stdcall *EnumOverlayZOrders)(IDirectDrawSurface *This, DWORD flags, void *ctx, void * cb); /* +0x28 */
    HRESULT (__stdcall *Flip)(IDirectDrawSurface *This, IDirectDrawSurface *dst_surface, DWORD flags); /* +0x2c */
    HRESULT (__stdcall *GetAttachedSurface)(IDirectDrawSurface *This, DDSCAPS *caps, IDirectDrawSurface **attachment); /* +0x30 */
    HRESULT (__stdcall *GetBltStatus)(IDirectDrawSurface *This, DWORD dwFlags); /* +0x34 */
    HRESULT (__stdcall *GetCaps)(IDirectDrawSurface *This, DDSCAPS *caps); /* +0x38 */
    HRESULT (__stdcall *GetClipper)(IDirectDrawSurface *This, IDirectDrawClipper **clipper); /* +0x3c */
    HRESULT (__stdcall *GetColorKey)(IDirectDrawSurface *This, DWORD flags, DDCOLORKEY *color_key); /* +0x40 */
    HRESULT (__stdcall *GetDC)(IDirectDrawSurface *This, HDC *lphDC); /* +0x44 */
    HRESULT (__stdcall *GetFlipStatus)(IDirectDrawSurface *This, DWORD dwFlags); /* +0x48 */
    HRESULT (__stdcall *GetOverlayPosition)(IDirectDrawSurface *This, LONG * lplX, LONG * lplY); /* +0x4c */
    HRESULT (__stdcall *GetPalette)(IDirectDrawSurface *This, IDirectDrawPalette **palette); /* +0x50 */
    HRESULT (__stdcall *GetPixelFormat)(IDirectDrawSurface *This, DDPIXELFORMAT *format); /* +0x54 */
    HRESULT (__stdcall *GetSurfaceDesc)(IDirectDrawSurface *This, DDSURFACEDESC *surface_desc); /* +0x58 */
    HRESULT (__stdcall *Initialize)(IDirectDrawSurface *This, IDirectDraw *ddraw, DDSURFACEDESC *surface_desc); /* +0x5c */
    HRESULT (__stdcall *IsLost)(IDirectDrawSurface *This); /* +0x60 */
    HRESULT (__stdcall *Lock)(IDirectDrawSurface *This, RECT *rect, DDSURFACEDESC *surface_desc, DWORD flags, HANDLE event); /* +0x64 */
    HRESULT (__stdcall *ReleaseDC)(IDirectDrawSurface *This, HDC hDC); /* +0x68 */
    HRESULT (__stdcall *Restore)(IDirectDrawSurface *This); /* +0x6c */
    HRESULT (__stdcall *SetClipper)(IDirectDrawSurface *This, IDirectDrawClipper *clipper); /* +0x70 */
    HRESULT (__stdcall *SetColorKey)(IDirectDrawSurface *This, DWORD flags, DDCOLORKEY *color_key); /* +0x74 */
    HRESULT (__stdcall *SetOverlayPosition)(IDirectDrawSurface *This, LONG lX, LONG lY); /* +0x78 */
    HRESULT (__stdcall *SetPalette)(IDirectDrawSurface *This, IDirectDrawPalette *palette); /* +0x7c */
    HRESULT (__stdcall *Unlock)(IDirectDrawSurface *This, void *data); /* +0x80 */
    HRESULT (__stdcall *UpdateOverlay)(IDirectDrawSurface *This, RECT *src_rect, IDirectDrawSurface *dst_surface, RECT *dst_rect, DWORD flags, void *fx); /* +0x84 */
    HRESULT (__stdcall *UpdateOverlayDisplay)(IDirectDrawSurface *This, DWORD dwFlags); /* +0x88 */
    HRESULT (__stdcall *UpdateOverlayZOrder)(IDirectDrawSurface *This, DWORD flags, IDirectDrawSurface *reference_surface); /* +0x8c */
};
struct IDirectDrawSurface { struct IDirectDrawSurfaceVtbl *lpVtbl; };

/* IDirectDrawPalette: 7 methods. */
struct IDirectDrawPaletteVtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectDrawPalette *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectDrawPalette *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectDrawPalette *This); /* +0x08 */
    HRESULT (__stdcall *GetCaps)(IDirectDrawPalette *This, DWORD * lpdwCaps); /* +0x0c */
    HRESULT (__stdcall *GetEntries)(IDirectDrawPalette *This, DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, PALETTEENTRY * lpEntries); /* +0x10 */
    HRESULT (__stdcall *Initialize)(IDirectDrawPalette *This, IDirectDraw *ddraw, DWORD flags, PALETTEENTRY *color_table); /* +0x14 */
    HRESULT (__stdcall *SetEntries)(IDirectDrawPalette *This, DWORD dwFlags, DWORD dwStartingEntry, DWORD dwCount, PALETTEENTRY * lpEntries); /* +0x18 */
};
struct IDirectDrawPalette { struct IDirectDrawPaletteVtbl *lpVtbl; };

/* IDirectDrawClipper: 9 methods. */
struct IDirectDrawClipperVtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectDrawClipper *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectDrawClipper *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectDrawClipper *This); /* +0x08 */
    HRESULT (__stdcall *GetClipList)(IDirectDrawClipper *This, RECT * lpRect, void * lpClipList, DWORD * lpdwSize); /* +0x0c */
    HRESULT (__stdcall *GetHWnd)(IDirectDrawClipper *This, HWND *lphWnd); /* +0x10 */
    HRESULT (__stdcall *Initialize)(IDirectDrawClipper *This, IDirectDraw *ddraw, DWORD flags); /* +0x14 */
    HRESULT (__stdcall *IsClipListChanged)(IDirectDrawClipper *This, BOOL *lpbChanged); /* +0x18 */
    HRESULT (__stdcall *SetClipList)(IDirectDrawClipper *This, void * lpClipList, DWORD dwFlags); /* +0x1c */
    HRESULT (__stdcall *SetHWnd)(IDirectDrawClipper *This, DWORD dwFlags, HWND hWnd); /* +0x20 */
};
struct IDirectDrawClipper { struct IDirectDrawClipperVtbl *lpVtbl; };

/* IDirectSound: 11 methods. */
struct IDirectSoundVtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectSound *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectSound *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectSound *This); /* +0x08 */
    HRESULT (__stdcall *CreateSoundBuffer)(IDirectSound *This, const DSBUFFERDESC * lpcDSBufferDesc, IDirectSoundBuffer ** lplpDirectSoundBuffer, void *pUnkOuter); /* +0x0c */
    HRESULT (__stdcall *GetCaps)(IDirectSound *This, DSCAPS * lpDSCaps); /* +0x10 */
    HRESULT (__stdcall *DuplicateSoundBuffer)(IDirectSound *This, IDirectSoundBuffer * lpDsbOriginal, IDirectSoundBuffer ** lplpDsbDuplicate); /* +0x14 */
    HRESULT (__stdcall *SetCooperativeLevel)(IDirectSound *This, HWND hwnd, DWORD dwLevel); /* +0x18 */
    HRESULT (__stdcall *Compact)(IDirectSound *This); /* +0x1c */
    HRESULT (__stdcall *GetSpeakerConfig)(IDirectSound *This, DWORD * lpdwSpeakerConfig); /* +0x20 */
    HRESULT (__stdcall *SetSpeakerConfig)(IDirectSound *This, DWORD dwSpeakerConfig); /* +0x24 */
    HRESULT (__stdcall *Initialize)(IDirectSound *This, const GUID * lpcGuid); /* +0x28 */
};
struct IDirectSound { struct IDirectSoundVtbl *lpVtbl; };

/* IDirectSoundBuffer: 21 methods. */
struct IDirectSoundBufferVtbl {
    HRESULT (__stdcall *QueryInterface)(IDirectSoundBuffer *This, const GUID * riid, void** ppvObject); /* +0x00 */
    ULONG (__stdcall *AddRef)(IDirectSoundBuffer *This); /* +0x04 */
    ULONG (__stdcall *Release)(IDirectSoundBuffer *This); /* +0x08 */
    HRESULT (__stdcall *GetCaps)(IDirectSoundBuffer *This, DSBCAPS * lpDSBufferCaps); /* +0x0c */
    HRESULT (__stdcall *GetCurrentPosition)(IDirectSoundBuffer *This, DWORD * lpdwCurrentPlayCursor, DWORD * lpdwCurrentWriteCursor); /* +0x10 */
    HRESULT (__stdcall *GetFormat)(IDirectSoundBuffer *This, WAVEFORMATEX * lpwfxFormat, DWORD dwSizeAllocated, DWORD * lpdwSizeWritten); /* +0x14 */
    HRESULT (__stdcall *GetVolume)(IDirectSoundBuffer *This, LONG * lplVolume); /* +0x18 */
    HRESULT (__stdcall *GetPan)(IDirectSoundBuffer *This, LONG * lplpan); /* +0x1c */
    HRESULT (__stdcall *GetFrequency)(IDirectSoundBuffer *This, DWORD * lpdwFrequency); /* +0x20 */
    HRESULT (__stdcall *GetStatus)(IDirectSoundBuffer *This, DWORD * lpdwStatus); /* +0x24 */
    HRESULT (__stdcall *Initialize)(IDirectSoundBuffer *This, IDirectSound * lpDirectSound, const DSBUFFERDESC * lpcDSBufferDesc); /* +0x28 */
    HRESULT (__stdcall *Lock)(IDirectSoundBuffer *This, DWORD dwOffset, DWORD dwBytes, void * *ppvAudioPtr1, DWORD * pdwAudioBytes1, void * *ppvAudioPtr2, DWORD * pdwAudioBytes2, DWORD dwFlags); /* +0x2c */
    HRESULT (__stdcall *Play)(IDirectSoundBuffer *This, DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags); /* +0x30 */
    HRESULT (__stdcall *SetCurrentPosition)(IDirectSoundBuffer *This, DWORD dwNewPosition); /* +0x34 */
    HRESULT (__stdcall *SetFormat)(IDirectSoundBuffer *This, const WAVEFORMATEX * lpcfxFormat); /* +0x38 */
    HRESULT (__stdcall *SetVolume)(IDirectSoundBuffer *This, LONG lVolume); /* +0x3c */
    HRESULT (__stdcall *SetPan)(IDirectSoundBuffer *This, LONG lPan); /* +0x40 */
    HRESULT (__stdcall *SetFrequency)(IDirectSoundBuffer *This, DWORD dwFrequency); /* +0x44 */
    HRESULT (__stdcall *Stop)(IDirectSoundBuffer *This); /* +0x48 */
    HRESULT (__stdcall *Unlock)(IDirectSoundBuffer *This, void * pvAudioPtr1, DWORD dwAudioBytes1, void * pvAudioPtr2, DWORD dwAudioPtr2); /* +0x4c */
    HRESULT (__stdcall *Restore)(IDirectSoundBuffer *This); /* +0x50 */
};
struct IDirectSoundBuffer { struct IDirectSoundBufferVtbl *lpVtbl; };
#endif
