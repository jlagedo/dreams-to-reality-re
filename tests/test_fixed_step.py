"""The original physics only behaves as tuned at one frame delta.

Recovered from WINDREAM.EXE (docs/engine.md, *The fixed step*): per frame,
v += (dt/3) * 9.81 while airborne, v *= 0.99, and the position steps by
round(v) with no dt factor. Landing damage (player controller 0x421717)
compares s = v / dt with 60, 100, 120, 180 and 250, dealing 5, 10, 15, 40 and
100 cumulatively. These tests pin the table in engine.md and the choice of
dt = 1.0 for the port's fixed step.
"""

from __future__ import annotations

import pytest

GRAVITY = 9.81
AIR_DAMPING = 0.99
LANDING = ((60, 5), (100, 10), (120, 15), (180, 40), (250, 100))


def fall_damage(height: int, dt: float) -> int:
    v, y = 0.0, 0
    while y < height:
        v += dt * (1 / 30) * 10 * GRAVITY
        v *= AIR_DAMPING
        y += round(v)
    s = v / dt
    return sum(d for t, d in LANDING if s > t)


@pytest.mark.parametrize(
    ("height", "expected"),
    [
        (1000, {0.2: 30, 0.5: 15, 0.9: 5, 1.0: 5, 1.05: 5, 2.0: 0}),
        (3000, {0.2: 70, 0.5: 30, 0.9: 30, 1.0: 15, 1.05: 15, 2.0: 5}),
        (5000, {0.2: 170, 0.5: 70, 0.9: 30, 1.0: 30, 1.05: 30, 2.0: 15}),
    ],
)
def test_fall_damage_depends_on_the_step(height, expected):
    assert {dt: fall_damage(height, dt) for dt in expected} == expected


def test_seven_ticks_is_closer_than_six_to_the_recorder_step():
    """The demo recorder forces dt = 1.0; 7 ticks (1.05) tracks it, 6 (0.9) does not."""
    heights = range(200, 8001, 200)
    ref = [fall_damage(h, 1.0) for h in heights]
    off7 = sum(fall_damage(h, 7 * 0.15) != r for h, r in zip(heights, ref, strict=True))
    off6 = sum(fall_damage(h, 6 * 0.15) != r for h, r in zip(heights, ref, strict=True))
    assert off7 < off6
