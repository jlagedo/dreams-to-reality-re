"""Cryo's LZ codec, used by ``.DSN`` and ``.DAN`` record tags 1 and 2.

Recovered from ``FUN_0049afd1`` in ``WINDREAM.EXE``. A bit-oriented scheme: a
32-bit control word is consumed **most-significant bit first**, and each code
is either a literal or one of two back-reference forms.

::

    1              literal      copy one input byte
    0 1            long match   read u16 code
    0 0 b1 b2      short match  read one displacement byte

    long:   distance = (int32)(0xffff0000 | code) >> 3      -8192 .. -1
            length   = (code & 7) + 2                        3 .. 9
            ...or, when (code & 7) == 0:
            length   = next byte; 0 terminates; otherwise + 2

    short:  distance = next byte - 256                       -256 .. -1
            length   = 2 + 2*b1 + b2                         2 .. 5

There is no stored output length - the stream ends on a zero length byte in the
long form. Matches copy one byte at a time, so overlapping back-references are
legal and are how runs are encoded.

Verified on **610/610** tag 1 and tag 2 records across every scene and animation
on both discs, with zero failures. Expansion runs 1.14x to 7.32x.
"""

from __future__ import annotations

MIN_LONG_DISTANCE = -8192
MIN_SHORT_DISTANCE = -256


class LZError(ValueError):
    """The stream ran off the end of its input, or asked for a bad distance."""


def decompress(src: bytes, limit: int = 1 << 26) -> bytes:
    """Decode one packed record payload.

    ``limit`` caps the output so a corrupt stream cannot exhaust memory; it is
    far above the largest real record (about 1.2 MB).
    """
    out = bytearray()
    pos = 0
    ctrl = 0
    nbits = 0
    n = len(src)

    def bit() -> int:
        nonlocal ctrl, nbits, pos
        if nbits == 0:
            if pos + 4 > n:
                raise LZError(f"control word past end at {pos}")
            ctrl = int.from_bytes(src[pos : pos + 4], "little")
            pos += 4
            nbits = 32
        nbits -= 1
        return (ctrl >> nbits) & 1

    while True:
        if bit():
            if pos >= n:
                raise LZError(f"literal past end at {pos}")
            out.append(src[pos])
            pos += 1
            continue

        if bit():  # long match
            if pos + 2 > n:
                raise LZError(f"long code past end at {pos}")
            code = int.from_bytes(src[pos : pos + 2], "little")
            pos += 2
            distance = ((0xFFFF0000 | code) - (1 << 32)) >> 3
            if code & 7:
                length = (code & 7) + 2
            else:
                if pos >= n:
                    raise LZError(f"length byte past end at {pos}")
                length = src[pos]
                pos += 1
                if length == 0:
                    break
                length += 2
        else:  # short match
            b1, b2 = bit(), bit()
            if pos >= n:
                raise LZError(f"displacement past end at {pos}")
            distance = src[pos] - 256
            pos += 1
            length = 2 + 2 * b1 + b2

        if -distance > len(out):
            raise LZError(f"distance {distance} before start of output")
        if len(out) + length > limit:
            raise LZError(f"output exceeds {limit} bytes")
        for _ in range(length):
            out.append(out[distance])

    return bytes(out)
