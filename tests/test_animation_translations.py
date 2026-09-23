from __future__ import annotations

import math
import struct

import pytest

from dreams import paths, pe
from dreams.formats.animation import (
    TranslationKeyframe,
    read_dan_animations,
    sample_translation,
)


def test_hermite_positions_use_interval_scaled_tangents():
    a = TranslationKeyframe(0, (0, 0, 0), out_tangent=(20, 0, 0))
    b = TranslationKeyframe(10, (10, 0, 0), in_tangent=(0, 0, 0))
    assert sample_translation(a, b, 0) == (0, 0, 0)
    assert sample_translation(a, b, 1) == (10, 0, 0)
    assert sample_translation(a, b, 0.5) == (7.5, 0, 0)
    # No second multiplication by the ten-frame interval.
    b.time = 100
    assert sample_translation(a, b, 0.5) == (7.5, 0, 0)


def test_zero_tangents_are_valid_and_linear_keys_remain_linear():
    a = TranslationKeyframe(0, (0, 0, 0), out_tangent=(0, 0, 0))
    b = TranslationKeyframe(10, (10, 0, 0), in_tangent=(0, 0, 0))
    assert sample_translation(a, b, 0.25)[0] == 1.5625
    a.out_tangent = None
    assert sample_translation(a, b, 0.25)[0] == 2.5


DISC = paths.configured("disc1")
needs_disc = pytest.mark.skipif(DISC is None or not DISC.exists(), reason="Disc 1 unavailable")


@needs_disc
def test_duncan_translation_decode_and_exact_key_sampling():
    clips = read_dan_animations(DISC / "DATA/3DC/XH_.DAN")
    idle = clips[0]
    assert idle.tracks[0].translation_key_count == 9
    assert idle.sample_translations(1)[0] == (-5, -177, 0)
    assert idle.tracks[0].translation_keys[0].out_tangent == (0, 2, 0)
    jump = next(c for c in clips if c.name == "XH_AN020.3DA")
    assert jump.sample_translations(jump.duration_frames)[0] == (-17, -181, -92)
    for clip in clips:
        for track in clip.tracks:
            assert len(track.translation_keys) == track.translation_key_count
            for key in track.translation_keys:
                assert clip.sample_translations(key.time)[track.node_index] == pytest.approx(
                    key.position
                )
        assert all(math.isfinite(v) for p in clip.sample_translations(12.5).values() for v in p)


@needs_disc
def test_executable_hermite_basis_and_blend_rate():
    exe = DISC / "WINDREAM.EXE"
    blob, sections = exe.read_bytes(), pe.read(exe).sections

    def read(va, fmt):
        return struct.unpack_from(fmt, blob, pe._rva_to_offset(sections, va - 0x400000))

    assert read(0x4AA710, "<16f") == (2, -2, 1, 1, -3, 3, -2, -1, 0, 0, 1, 0, 1, 0, 0, 0)
    assert read(0x4C33E4, "<f") == (48,)
    assert 256 / (48 * 30) == pytest.approx(0.1777777778)
