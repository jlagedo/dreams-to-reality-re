"""input_map.h: the key names, WD_KEYMAP, WD_PADMAP and WD_PAD_DIRECTION parsing and lookup.

The header is pure C (no SDL), so a small program compiled against it checks the
tables the host builds from the environment. What this cannot reach is the SDL
side: a physical key or pad button producing the remapped key (a manual run).
"""
# ruff: noqa: E501

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SDL_DIR = ROOT / "recomp/windream/host/sdl"

PROGRAM = r"""
#include <stdio.h>
#include <string.h>
#include "input_map.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int nbad;
static char badlog[512];
static void bad(void* ctx, const char* e, size_t n, const char* why) {
    (void)ctx; nbad++;
    size_t at = strlen(badlog);
    snprintf(badlog + at, sizeof badlog - at, "[%.*s|%s]", (int)n, e, why);
}

#define VK(name) wd_vk_from_name(name, strlen(name))

int main(void) {
    /* names: the ones WD_KEYS has always taken keep their values */
    CHECK(VK("UP") == W32_VK_UP && VK("down") == W32_VK_DOWN && VK("Left") == W32_VK_LEFT && VK("RIGHT") == W32_VK_RIGHT);
    CHECK(VK("W") == 'W' && VK("w") == 'W' && VK("0") == '0' && VK("9") == '9' && VK("z") == 'Z');
    CHECK(VK("CTRL") == W32_VK_CONTROL && VK("ALT") == W32_VK_MENU && VK("SHIFT") == W32_VK_SHIFT);
    CHECK(VK("SPACE") == W32_VK_SPACE && VK("RETURN") == W32_VK_RETURN && VK("ENTER") == W32_VK_RETURN);
    CHECK(VK("ESC") == W32_VK_ESCAPE && VK("ESCAPE") == W32_VK_ESCAPE && VK("TAB") == W32_VK_TAB);
    CHECK(VK("BACK") == W32_VK_BACK && VK("BACKSPACE") == W32_VK_BACK);
    for (int i = 1; i <= 12; i++) {
        char f[8];
        snprintf(f, sizeof f, "F%d", i);
        CHECK(VK(f) == W32_VK_F1 + i - 1);
    }
    CHECK(VK("F11") == W32_VK_F10 + 1 && VK("F10") == W32_VK_F10 && VK("F24") == W32_VK_F1 + 23);
    CHECK(VK("F25") == 0 && VK("F0") == 0 && VK("F") == 'F');
    CHECK(VK("KP1") == 0x61 && VK("KP5") == 0x65 && VK("KP0") == 0x60);
    CHECK(VK("") == 0 && VK("NOPE") == 0 && VK("-") == 0 && VK("W W") == 0);

    /* WD_KEYMAP */
    WdKeyMap km;
    nbad = 0; badlog[0] = 0;
    CHECK(wd_keymap_parse(&km, "W=UP,A=LEFT,S=DOWN,D=RIGHT", bad, NULL) == 0 && km.n == 4 && !nbad);
    CHECK(wd_keymap_apply(&km, 'W') == W32_VK_UP && wd_keymap_apply(&km, 'D') == W32_VK_RIGHT);
    CHECK(wd_keymap_apply(&km, W32_VK_UP) == W32_VK_UP);          /* unlisted: itself */
    CHECK(wd_keymap_apply(&km, 'Q') == 'Q' && wd_keymap_apply(&km, 0) == 0);

    CHECK(wd_keymap_parse(&km, " w = up , A=left ,, ", bad, NULL) == 0 && km.n == 2);   /* case, spaces, empty */
    CHECK(wd_keymap_apply(&km, 'W') == W32_VK_UP);

    /* a swap, and a key that is both a target and a source */
    CHECK(wd_keymap_parse(&km, "UP=W,W=UP", bad, NULL) == 0);
    CHECK(wd_keymap_apply(&km, 'W') == W32_VK_UP && wd_keymap_apply(&km, W32_VK_UP) == 'W');
    CHECK(wd_keymap_parse(&km, "W=UP", bad, NULL) == 0);
    CHECK(wd_keymap_apply(&km, 'W') == W32_VK_UP);                /* W is gone ... */
    CHECK(wd_keymap_apply(&km, W32_VK_UP) == W32_VK_UP);          /* ... UP still works */

    /* modifiers: a generic pair covers both sides, a specific one only its side */
    CHECK(wd_keymap_parse(&km, "CTRL=SPACE,LSHIFT=ALT,RSHIFT=F1", bad, NULL) == 0);
    uint16_t lctrl = W32_VK_LCONTROL | W32_VK_CONTROL << 8, rctrl = W32_VK_RCONTROL | W32_VK_CONTROL << 8;
    uint16_t lshift = W32_VK_LSHIFT | W32_VK_SHIFT << 8, rshift = W32_VK_RSHIFT | W32_VK_SHIFT << 8;
    CHECK(wd_keymap_apply(&km, lctrl) == W32_VK_SPACE && wd_keymap_apply(&km, rctrl) == W32_VK_SPACE);
    CHECK(wd_keymap_apply(&km, lshift) == (W32_VK_LMENU | W32_VK_MENU << 8));      /* ALT arrives as an Alt key */
    CHECK(wd_keymap_apply(&km, rshift) == W32_VK_F1);
    CHECK(wd_keymap_parse(&km, "CTRL=SHIFT,LCTRL=F2", bad, NULL) == 0);
    CHECK(wd_keymap_apply(&km, lctrl) == W32_VK_F1 + 1 && wd_keymap_apply(&km, rctrl) == (W32_VK_LSHIFT | W32_VK_SHIFT << 8));

    /* bad entries are reported by name and skipped, the good ones stay */
    nbad = 0; badlog[0] = 0;
    CHECK(wd_keymap_parse(&km, "W=UP,Q,X=NOPE,NOPE=Y,W=DOWN,=A,B=,A=B=C,S=DOWN", bad, NULL) == 7 && nbad == 7);
    CHECK(km.n == 2 && wd_keymap_apply(&km, 'W') == W32_VK_UP && wd_keymap_apply(&km, 'S') == W32_VK_DOWN);
    CHECK(strstr(badlog, "[Q|") && strstr(badlog, "[X=NOPE|unknown game key name]") &&
          strstr(badlog, "[NOPE=Y|unknown physical key name]") && strstr(badlog, "[W=DOWN|physical key already mapped]"));
    CHECK(wd_keymap_parse(&km, "", bad, NULL) == 0 && km.n == 0 && wd_keymap_apply(&km, 'W') == 'W');

    /* WD_PAD_DIRECTION */
    CHECK(wd_dir_parse("stick") == WD_DIR_STICK && wd_dir_parse("DPAD") == WD_DIR_DPAD);
    CHECK(wd_dir_parse("Both") == (WD_DIR_STICK | WD_DIR_DPAD));
    CHECK(wd_dir_parse("") == 0 && wd_dir_parse("sticks") == 0 && wd_dir_parse(NULL) == 0);

    /* WD_PADMAP defaults: today's tables */
    WdPadMap pm;
    wd_padmap_defaults(&pm);
    for (int i = 0; i < 10; i++) CHECK(pm.joy[i] == i + 1);
    CHECK(pm.joy[WD_PB_LT] == 0 && pm.joy[WD_PB_RT] == 0);
    CHECK(pm.key[WD_PB_A] == (W32_VK_LCONTROL | W32_VK_CONTROL << 8));
    CHECK(wd_padmap_key_is(&pm, WD_PB_A, W32_VK_CONTROL) && wd_padmap_key_is(&pm, WD_PB_A, W32_VK_LCONTROL));
    CHECK(!wd_padmap_key_is(&pm, WD_PB_A, W32_VK_RCONTROL));
    CHECK(wd_padmap_key_is(&pm, WD_PB_X, W32_VK_MENU) && wd_padmap_key_is(&pm, WD_PB_X, W32_VK_LMENU));
    CHECK(pm.key[WD_PB_B] == W32_VK_DOWN && pm.key[WD_PB_Y] == W32_VK_SPACE);
    CHECK(pm.key[WD_PB_LB] == '1' && pm.key[WD_PB_RB] == '2' && pm.key[WD_PB_LT] == '3');
    CHECK(pm.key[WD_PB_START] == W32_VK_ESCAPE && pm.key[WD_PB_BACK] == W32_VK_RETURN);
    CHECK(pm.key[WD_PB_LS] == 0 && pm.key[WD_PB_RS] == 0 && pm.key[WD_PB_RT] == 0);

    /* winmm: buttonN targets over the defaults */
    nbad = 0; badlog[0] = 0;
    CHECK(wd_padmap_parse(&pm, 0, "a=button5, START=Button32,rs=button1", bad, NULL) == 0);
    CHECK(pm.joy[WD_PB_A] == 5 && pm.joy[WD_PB_START] == 32 && pm.joy[WD_PB_RS] == 1 && pm.joy[WD_PB_B] == 2);
    CHECK(pm.key[WD_PB_A] == (W32_VK_LCONTROL | W32_VK_CONTROL << 8));   /* keys left alone */
    wd_padmap_defaults(&pm);
    CHECK(wd_padmap_parse(&pm, 0, "a=button0,b=button33,x=button,y=ctrl,lb=button01,lt=button11,rt=button12,"
                                  "zz=button3,rb=button4,rb=button5,start", bad, NULL) == 10);
    CHECK(strstr(badlog, "lt and rt are the Z axis"));
    CHECK(pm.joy[WD_PB_A] == 1 && pm.joy[WD_PB_B] == 2 && pm.joy[WD_PB_X] == 3 && pm.joy[WD_PB_Y] == 4);
    CHECK(pm.joy[WD_PB_RB] == 4 && pm.joy[WD_PB_LT] == 0 && pm.joy[WD_PB_RT] == 0);

    /* keys: key-name targets, modifiers expanded, triggers allowed */
    wd_padmap_defaults(&pm);
    nbad = 0; badlog[0] = 0;
    CHECK(wd_padmap_parse(&pm, 1, "a=SPACE,rt=F5,lt=shift,b=button1,start=NOPE", bad, NULL) == 2 && nbad == 2);
    CHECK(pm.key[WD_PB_A] == W32_VK_SPACE && pm.key[WD_PB_RT] == W32_VK_F1 + 4);
    CHECK(pm.key[WD_PB_LT] == (W32_VK_LSHIFT | W32_VK_SHIFT << 8) && wd_padmap_key_is(&pm, WD_PB_LT, W32_VK_SHIFT));
    CHECK(pm.key[WD_PB_B] == W32_VK_DOWN && pm.key[WD_PB_START] == W32_VK_ESCAPE);   /* bad: default stays */
    CHECK(pm.key[WD_PB_Y] == W32_VK_SPACE);   /* the same key may serve two buttons */
    CHECK(!wd_padmap_key_is(&pm, WD_PB_LS, 0) && !wd_padmap_key_is(&pm, WD_PB_LS, W32_VK_UP));

    if (!fails) printf("input_map ok\n");
    return fails != 0;
}
"""


def _compile(out: Path) -> Path:
    source = out / "input_map_test.c"
    source.write_text(PROGRAM, encoding="utf-8")
    exe = out / ("input_map_test.exe" if sys.platform == "win32" else "input_map_test")
    if sys.platform == "win32":
        sys.path.insert(0, str(ROOT / "recomp"))
        import recomp_env

        env = recomp_env.build_env()
        compiler = shutil.which("cl.exe", path=env["PATH"])
        assert compiler
        cmd = [compiler, "/nologo", f"/I{SDL_DIR}", str(source), f"/Fe:{exe}", f"/Fo:{out}\\"]
    else:
        env = None
        compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            pytest.skip("no C compiler")
        cmd = [
            compiler,
            "-std=c99",
            "-Wall",
            "-Werror",
            f"-I{SDL_DIR}",
            str(source),
            "-o",
            str(exe),
        ]
    subprocess.run(cmd, cwd=out, env=env, check=True, capture_output=True, text=True)
    return exe


def test_input_map_tables(tmp_path):
    exe = _compile(tmp_path)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "input_map ok" in run.stdout
