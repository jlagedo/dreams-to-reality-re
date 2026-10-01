/*
 * WINDREAM recompilation - the Win32 ABI as the 32-bit guest sees it.
 *
 * Constants and 32-bit structure offsets for the bridges that no longer call
 * Win32 (user.c, gdi.c, winmm.c, dsound.c run on SDL3), so they build without
 * <windows.h>. The values are the guest's contract, not host API: on Windows,
 * win32_abi_check.c asserts each one against the SDK headers. Structures that
 * hold pointers or handles (MSG, WNDCLASSA, CREATESTRUCTA, MCI parameter
 * blocks) have a different layout on a 64-bit host; their offsets here are
 * the 32-bit ones and are not checked.
 */
#ifndef WD_GUEST_WIN32_H
#define WD_GUEST_WIN32_H

/* ---- USER32: messages, windows ---- */
#define W32_WM_CREATE        0x0001u
#define W32_WM_DESTROY       0x0002u
#define W32_WM_CLOSE         0x0010u
#define W32_WM_QUIT          0x0012u
#define W32_PM_REMOVE        0x0001u
#define W32_WS_VISIBLE       0x10000000u
#define W32_CW_USEDEFAULT    0x80000000u
#define W32_SM_CXSCREEN      0
#define W32_SM_CYSCREEN      1

/* MSG, 28 bytes */
#define W32_MSG_HWND         0
#define W32_MSG_MESSAGE      4
#define W32_MSG_WPARAM       8
#define W32_MSG_LPARAM       12
#define W32_MSG_TIME         16
#define W32_MSG_PT_X         20
#define W32_MSG_PT_Y         24

/* WNDCLASSA, 40 bytes */
#define W32_WC_STYLE         0
#define W32_WC_WNDPROC       4
#define W32_WC_CLASSNAME     36

/* CREATESTRUCTA, 48 bytes */
#define W32_CS_SIZE          48u
#define W32_CS_INSTANCE      4
#define W32_CS_CY            16
#define W32_CS_CX            20
#define W32_CS_Y             24
#define W32_CS_X             28
#define W32_CS_STYLE         32
#define W32_CS_NAME          36
#define W32_CS_CLASS         40
#define W32_CS_EXSTYLE       44

/* MessageBoxA */
#define W32_MB_TYPEMASK      0x0000000Fu
#define W32_MB_ICONMASK      0x000000F0u
#define W32_MB_DEFMASK       0x00000F00u
#define W32_MB_OK            0x0u
#define W32_MB_OKCANCEL      0x1u
#define W32_MB_ABORTRETRYIGNORE 0x2u
#define W32_MB_YESNOCANCEL   0x3u
#define W32_MB_YESNO         0x4u
#define W32_MB_RETRYCANCEL   0x5u
#define W32_MB_ICONHAND      0x10u
#define W32_MB_ICONQUESTION  0x20u
#define W32_MB_ICONEXCLAMATION 0x30u
#define W32_MB_ICONASTERISK  0x40u
#define W32_IDOK             1
#define W32_IDCANCEL         2
#define W32_IDABORT          3
#define W32_IDRETRY          4
#define W32_IDIGNORE         5
#define W32_IDYES            6
#define W32_IDNO             7

/* Virtual-key codes */
#define W32_VK_LBUTTON       0x01
#define W32_VK_RBUTTON       0x02
#define W32_VK_MBUTTON       0x04
#define W32_VK_XBUTTON1      0x05
#define W32_VK_XBUTTON2      0x06
#define W32_VK_BACK          0x08
#define W32_VK_TAB           0x09
#define W32_VK_CLEAR         0x0C
#define W32_VK_RETURN        0x0D
#define W32_VK_SHIFT         0x10
#define W32_VK_CONTROL       0x11
#define W32_VK_MENU          0x12
#define W32_VK_PAUSE         0x13
#define W32_VK_CAPITAL       0x14
#define W32_VK_ESCAPE        0x1B
#define W32_VK_SPACE         0x20
#define W32_VK_PRIOR         0x21
#define W32_VK_NEXT          0x22
#define W32_VK_END           0x23
#define W32_VK_HOME          0x24
#define W32_VK_LEFT          0x25
#define W32_VK_UP            0x26
#define W32_VK_RIGHT         0x27
#define W32_VK_DOWN          0x28
#define W32_VK_SNAPSHOT      0x2C
#define W32_VK_INSERT        0x2D
#define W32_VK_DELETE        0x2E
#define W32_VK_LWIN          0x5B
#define W32_VK_RWIN          0x5C
#define W32_VK_APPS          0x5D
#define W32_VK_NUMPAD0       0x60
#define W32_VK_MULTIPLY      0x6A
#define W32_VK_ADD           0x6B
#define W32_VK_SUBTRACT      0x6D
#define W32_VK_DECIMAL       0x6E
#define W32_VK_DIVIDE        0x6F
#define W32_VK_F1            0x70
#define W32_VK_F10           0x79
#define W32_VK_F11           0x7A
#define W32_VK_NUMLOCK       0x90
#define W32_VK_SCROLL        0x91
#define W32_VK_LSHIFT        0xA0
#define W32_VK_RSHIFT        0xA1
#define W32_VK_LCONTROL      0xA2
#define W32_VK_RCONTROL      0xA3
#define W32_VK_LMENU         0xA4
#define W32_VK_RMENU         0xA5
#define W32_VK_OEM_1         0xBA
#define W32_VK_OEM_PLUS      0xBB
#define W32_VK_OEM_COMMA     0xBC
#define W32_VK_OEM_MINUS     0xBD
#define W32_VK_OEM_PERIOD    0xBE
#define W32_VK_OEM_2         0xBF
#define W32_VK_OEM_3         0xC0
#define W32_VK_OEM_4         0xDB
#define W32_VK_OEM_5         0xDC
#define W32_VK_OEM_6         0xDD
#define W32_VK_OEM_7         0xDE
#define W32_VK_OEM_102       0xE2

/* ---- GDI32 ---- */
#define W32_BI_RGB           0u
#define W32_BI_BITFIELDS     3u
#define W32_OPAQUE           2
/* BITMAPINFOHEADER, 40 bytes; BI_BITFIELDS masks follow at biSize */
#define W32_BIH_SIZE         0
#define W32_BIH_WIDTH        4
#define W32_BIH_HEIGHT       8
#define W32_BIH_BITCOUNT     14
#define W32_BIH_COMPRESSION  16
#define W32_BIH_CLRUSED      32
/* RECT */
#define W32_RECT_RIGHT       8
#define W32_RECT_BOTTOM      12

/* ---- KERNEL32 VirtualAlloc flags (vm.c takes the guest's values) ---- */
#define W32_MEM_COMMIT       0x00001000u
#define W32_MEM_RESERVE      0x00002000u
#define W32_MEM_RELEASE      0x00008000u
#define W32_PAGE_READWRITE   0x04u

/* ---- KERNEL32: files, handles, waits (files.c, kernel.c, threads.c) ---- */
#define W32_MAX_PATH                260
#define W32_INVALID_HANDLE_VALUE    0xFFFFFFFFu
#define W32_INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define W32_INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define W32_GENERIC_READ            0x80000000u
#define W32_GENERIC_WRITE           0x40000000u
#define W32_CREATE_NEW              1u
#define W32_CREATE_ALWAYS           2u
#define W32_OPEN_EXISTING           3u
#define W32_OPEN_ALWAYS             4u
#define W32_TRUNCATE_EXISTING       5u
#define W32_FILE_ATTRIBUTE_DIRECTORY 0x10u
#define W32_FILE_ATTRIBUTE_ARCHIVE  0x20u
#define W32_FILE_TYPE_UNKNOWN       0u
#define W32_FILE_TYPE_DISK          1u
#define W32_FILE_TYPE_CHAR          2u
#define W32_STD_INPUT_HANDLE        0xFFFFFFF6u   /* (DWORD)-10 */
#define W32_STD_OUTPUT_HANDLE       0xFFFFFFF5u   /* (DWORD)-11 */
#define W32_CURRENT_PROCESS         0xFFFFFFFFu   /* GetCurrentProcess's pseudo-handle */
#define W32_CURRENT_THREAD          0xFFFFFFFEu   /* GetCurrentThread's */
#define W32_CREATE_SUSPENDED        0x00000004u
#define W32_INFINITE                0xFFFFFFFFu
#define W32_WAIT_OBJECT_0           0u
#define W32_WAIT_TIMEOUT            258u
#define W32_WAIT_FAILED             0xFFFFFFFFu
#define W32_TLS_OUT_OF_INDEXES      0xFFFFFFFFu
#define W32_CP_ACP                  0u
#define W32_CP_OEMCP                1u
#define W32_CP_THREAD_ACP           3u
/* WIN32_FIND_DATAA, 320 bytes: attributes, three FILETIMEs, size high and low,
 * two reserved dwords, then the name (260) and the 8.3 alternate name (14) */
#define W32_FIND_DATA_SIZE          320u
#define W32_FIND_DATA_NAME          44
/* GetLastError values the bridges report */
#define W32_ERROR_FILE_NOT_FOUND    2u
#define W32_ERROR_PATH_NOT_FOUND    3u
#define W32_ERROR_TOO_MANY_OPEN_FILES 4u
#define W32_ERROR_ACCESS_DENIED     5u
#define W32_ERROR_INVALID_HANDLE    6u
#define W32_ERROR_NOT_ENOUGH_MEMORY 8u
#define W32_ERROR_NO_MORE_FILES     18u
#define W32_ERROR_SHARING_VIOLATION 32u
#define W32_ERROR_FILE_EXISTS       80u
#define W32_ERROR_INVALID_PARAMETER 87u
#define W32_ERROR_INSUFFICIENT_BUFFER 122u
#define W32_ERROR_NEGATIVE_SEEK     131u
#define W32_ERROR_ALREADY_EXISTS    183u
#define W32_ERROR_FILENAME_EXCED_RANGE 206u
#define W32_ERROR_INVALID_ADDRESS   487u

/* ---- WINMM: joystick ---- */
#define W32_JOYERR_NOERROR   0u
#define W32_JOYERR_PARMS     165u
#define W32_JOYERR_UNPLUGGED 167u
#define W32_JOY_RETURNX      0x001u
#define W32_JOY_RETURNY      0x002u
#define W32_JOY_RETURNZ      0x004u
#define W32_JOY_RETURNR      0x008u
#define W32_JOY_RETURNU      0x010u
#define W32_JOY_RETURNV      0x020u
#define W32_JOY_RETURNPOV    0x040u
#define W32_JOY_RETURNBUTTONS 0x080u
#define W32_JOY_POVCENTERED  0xFFFFu
#define W32_JOYCAPS_HASZ     0x01u
#define W32_JOYCAPS_HASR     0x02u
#define W32_JOYCAPS_HASU     0x04u
#define W32_JOYCAPS_HASPOV   0x10u
#define W32_JOYCAPS_POV4DIR  0x20u
/* JOYCAPSA, 404 bytes */
#define W32_JOYCAPSA_SIZE    404u
#define W32_JC_MID           0
#define W32_JC_PID           2
#define W32_JC_PNAME         4      /* char[32] */
#define W32_JC_XMIN          36     /* then xmax, ymin, ymax, zmin, zmax */
#define W32_JC_NUMBUTTONS    60
#define W32_JC_PERIODMIN     64
#define W32_JC_PERIODMAX     68
#define W32_JC_RMIN          72     /* then rmax, umin, umax, vmin, vmax */
#define W32_JC_CAPS          96
#define W32_JC_MAXAXES       100
#define W32_JC_NUMAXES       104
#define W32_JC_MAXBUTTONS    108
/* JOYINFOEX, 52 bytes */
#define W32_JOYINFOEX_SIZE   52u
#define W32_JI_FLAGS         4
#define W32_JI_XPOS          8      /* then y, z, r, u, v */
#define W32_JI_BUTTONS       32
#define W32_JI_BUTTONNUMBER  36
#define W32_JI_POV           40

/* ---- WINMM: MCI (CD audio) ---- */
#define W32_MCI_OPEN         0x0803u
#define W32_MCI_CLOSE        0x0804u
#define W32_MCI_PLAY         0x0806u
#define W32_MCI_SEEK         0x0807u
#define W32_MCI_STOP         0x0808u
#define W32_MCI_PAUSE        0x0809u
#define W32_MCI_SET          0x080Du
#define W32_MCI_STATUS       0x0814u
#define W32_MCI_RESUME       0x0855u
#define W32_MCI_FROM         0x00000004u
#define W32_MCI_OPEN_TYPE    0x00002000u
#define W32_MCI_SET_DOOR_OPEN 0x00000100u
#define W32_MCI_STATUS_ITEM  0x00000100u
#define W32_MCI_STATUS_NUMBER_OF_TRACKS 3u
#define W32_MCI_STATUS_MODE  4u
#define W32_MCI_STATUS_MEDIA_PRESENT 5u
#define W32_MCI_STATUS_READY 7u
#define W32_MCI_STATUS_CURRENT_TRACK 8u
#define W32_MCI_MODE_STOP    525u
#define W32_MCI_MODE_PLAY    526u
#define W32_MCI_MODE_PAUSE   529u
#define W32_MCIERR_INVALID_DEVICE_ID   257u
#define W32_MCIERR_UNRECOGNIZED_KEYWORD 259u
#define W32_MCIERR_OUTOFRANGE          282u
#define W32_MCIERR_MISSING_PARAMETER   273u
/* MCI parameter blocks (32-bit): OPEN {cb, wDeviceID, lpstrDeviceType},
 * STATUS {cb, dwReturn, dwItem, dwTrack}, PLAY {cb, dwFrom, dwTo} */
#define W32_MCI_OPEN_DEVICEID   4
#define W32_MCI_OPEN_DEVICETYPE 8
#define W32_MCI_STATUS_RETURN   4
#define W32_MCI_STATUS_ITEMOFF  8
#define W32_MCI_PLAY_FROM       4

#endif /* WD_GUEST_WIN32_H */
