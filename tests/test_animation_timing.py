"""Pin the viewer base rate to the clock instructions in the original binary.

These are deliberately build-specific evidence checks. A different executable
needs a new trace instead of silently borrowing this build's addresses.
"""

from __future__ import annotations

import struct

import pytest

from dreams import paths, pe
from dreams.formats.animation import ENGINE_BASE_FRAME_RATE, read_dan_animations

DISC1 = paths.configured("disc1")
EXE = DISC1 / "WINDREAM.EXE" if DISC1 else None
pytestmark = pytest.mark.skipif(EXE is None or not EXE.exists(), reason="WINDREAM.EXE unavailable")


@pytest.fixture(scope="module")
def original_bytes():
    data = EXE.read_bytes()
    binary = pe.read(EXE)
    optional = struct.unpack_from("<I", data, 0x3C)[0] + 24
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    assert image_base == 0x400000

    def read(va, size):
        offset = pe._rva_to_offset(binary.sections, va - image_base)
        assert offset is not None
        return data[offset : offset + size]

    return read


def test_original_timer_initializes_200_hz_counter(original_bytes):
    # MOV EBX,15; MOV EDX,200; MOV EAX,12; CALL dispatcher.
    # Dispatcher case 12 -> 00424b4f -> 00440802: period_ms = 1000 / 200.
    assert original_bytes(0x415FD8, 15) == bytes.fromhex(
        "bb 0f 00 00 00 ba c8 00 00 00 b8 0c 00 00 00"
    )
    clock_hz = struct.unpack("<I", original_bytes(0x415FDE, 4))[0]
    divisor = struct.unpack("<d", original_bytes(0x4C41C4, 8))[0]
    assert clock_hz == divisor == 200


def test_original_clock_to_animation_scale_is_30(original_bytes):
    # FLD delta_ticks; FDIVR [200]; store measured render FPS.
    assert original_bytes(0x417171, 15) == bytes.fromhex(
        "d9 45 f8 dc 3d c4 41 4c 00 dd 1d 88 53 5e 00"
    )
    # FLD [30]; FDIV measured FPS; store animation delta.
    assert original_bytes(0x4171B2, 18) == bytes.fromhex(
        "dd 05 cc 41 4c 00 dc 35 88 53 5e 00 dd 1d 88 53 5e 00"
    )
    rate = struct.unpack("<d", original_bytes(0x4C41CC, 8))[0]
    assert rate == ENGINE_BASE_FRAME_RATE == 30
    # Independent update path has the same 200 / elapsed_ticks -> 30 / FPS constants.
    assert struct.unpack("<2d", original_bytes(0x4C398C, 16)) == (200, 30)
    # Main-loop clamps are simulation quirks, not per-clip FPS values.
    assert struct.unpack("<2d", original_bytes(0x4C41D4, 16)) == (5, 0.2)


def test_action_transition_resets_speed_and_frame_accumulator_uses_it(original_bytes):
    # MOV [actor+0x178], float(1.0), not constructor's temporary 1.5.
    assert original_bytes(0x405AFA, 10) == bytes.fromhex("c7 80 78 01 00 00 00 00 80 3f")
    # FLD speed; FMUL effective_dt; load actor; FADD frame; FSTP frame.
    assert original_bytes(0x407026, 24) == bytes.fromhex(
        "d9 80 78 01 00 00 d8 4d f8 8b 45 f4 d8 80 70 01 00 00 d9 98 70 01 00 00"
    )


def test_duncan_clip_key_counts_do_not_change_playback_rate(original_bytes):
    clips = read_dan_animations(DISC1 / "DATA/3DC/XH_.DAN")
    recovered_rate = struct.unpack("<d", original_bytes(0x4C41CC, 8))[0]
    assert len({clip.tracks[0].num_keys for clip in clips}) > 1
    assert all(clip.frame_rate == recovered_rate for clip in clips)
    assert all(clip.to_dict()["frameRateSource"] == "windream-engine-base" for clip in clips)
