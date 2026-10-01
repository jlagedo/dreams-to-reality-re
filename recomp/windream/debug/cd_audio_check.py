"""Find a dump of the mixer's output (wdctl audio_dump) inside a disc image's
audio tracks: which track was playing, and from which of its samples.

The mixer adds the CD track to the game's sound effects and clips, so the dump
is not the track's bytes. A window is compared by normalized cross-correlation
(1.0: the same samples up to a gain; near 0: other music). The dump and the
tracks are both 44.1 kHz 16-bit stereo, so nothing is resampled.

Two searches:

  align(dump, pcm, expected_lag)   standard library only. The track is expected
      to have started at about dump frame `expected_lag` (from the "[cd] play"
      event's time); every lag within the tolerance is tried. The answer's lag
      is the dump frame at which the track's frame 0 was output.
  locate(dump, pcm, at)            needs numpy. Finds a window of the dump
      anywhere in a track, with no expectation.

    table = track_table(cue)             # the disc library's own listing tool
    dump = read_wav("mix.wav")           # array('h'), interleaved stereo
    pcm = track_pcm(table[9], frames=10 * RATE)   # from INDEX 01
    match = align(dump, pcm, expected_lag=92918)  # Match(lag, score, exact)

Command line (needs numpy):

    uv run --with numpy python recomp/windream/debug/cd_audio_check.py mix.wav disc.cue [TRACK...]

Track data is game data: this reads it and writes nothing.
"""

from __future__ import annotations

import math
import operator
import subprocess
import sys
import wave
from array import array
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RATE = 44100
FRAME_BYTES = 4  # 16-bit stereo
COARSE = 16  # align's first pass runs on mono summed over this many frames


@dataclass
class Track:
    number: int
    audio: bool
    sector: int
    offset: int  # bytes: INDEX 01 in the file
    length: int  # bytes: INDEX 01 to the end of the track
    path: Path

    @property
    def frames(self) -> int:
        return self.length // FRAME_BYTES


@dataclass
class Match:
    lag: int  # the dump frame at which the track's frame 0 was output
    score: float  # normalized cross-correlation of the compared window, -1..1
    exact: float  # share of the window's samples equal to the track's
    track_frame: int  # where in the track the compared window starts


def disc_list_tool() -> Path:
    """The disc library's listing tool (recomp/disc/build.py builds it)."""
    sys.path.insert(0, str(ROOT / "recomp"))
    try:
        import recomp_env
    finally:
        sys.path.remove(str(ROOT / "recomp"))
    return recomp_env.out_dir("disc") / "build" / recomp_env.exe_name("disc_list")


def track_table(cue: str | Path, tool: str | Path | None = None) -> dict[int, Track]:
    """The image's tracks by number, from the listing tool's lines
    TRACK <nn> <AUDIO|DATA> <sector size> <offset of INDEX 01> <length> <file>."""
    out = subprocess.run(
        [str(tool or disc_list_tool()), str(cue)],
        capture_output=True, text=True, encoding="utf-8", check=True,
    ).stdout  # fmt: skip
    table = {}
    for line in out.splitlines():
        cells = line.split("\t")
        if cells[0] == "TRACK":
            table[int(cells[1])] = Track(
                int(cells[1]), cells[2] == "AUDIO", int(cells[3]), int(cells[4]), int(cells[5]),
                Path(cells[6]),
            )  # fmt: skip
        elif cells[0] == "FILES":
            break
    return table


def _samples(data: bytes) -> array:
    pcm = array("h")
    pcm.frombytes(data[: len(data) // FRAME_BYTES * FRAME_BYTES])
    if sys.byteorder == "big":
        pcm.byteswap()
    return pcm


def read_wav(path: str | Path) -> array:
    """The dump as interleaved 16-bit stereo samples (two per frame)."""
    with wave.open(str(path), "rb") as wav:
        form = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate())
        if form != (2, 2, RATE):
            raise ValueError(f"{path}: {form}, expected 16-bit stereo at {RATE} Hz")
        return _samples(wav.readframes(wav.getnframes()))


def file_pcm(track: Track, byte_offset: int, frames: int) -> array:
    """Audio at any place of the track's file: before INDEX 01 (the pregap) or
    past the track's end (the next track of a file that holds several)."""
    with open(track.path, "rb") as f:
        f.seek(byte_offset)
        return _samples(f.read(frames * FRAME_BYTES))


def track_pcm(track: Track, start: int = 0, frames: int | None = None) -> array:
    """The track's audio from its INDEX 01 (from frame `start` of it), at most
    to the track's end."""
    count = track.frames - start if frames is None else min(frames, track.frames - start)
    return file_pcm(track, track.offset + start * FRAME_BYTES, max(count, 0))


def frames_of(pcm: array) -> int:
    return len(pcm) // 2


def mono(pcm: array, start: int = 0, frames: int | None = None) -> list[int]:
    """Left plus right, per frame."""
    stop = len(pcm) if frames is None else min(len(pcm), 2 * (start + frames))
    return list(map(operator.add, pcm[2 * start : stop : 2], pcm[2 * start + 1 : stop : 2]))


def ncc(x: list[int], y: list[int]) -> float:
    """Normalized cross-correlation of two equally long signals (0 when either is silent)."""
    n = min(len(x), len(y))
    x, y = x[:n], y[:n]
    norm = math.sqrt(sum(map(operator.mul, x, x)) * sum(map(operator.mul, y, y)))
    return sum(map(operator.mul, x, y)) / norm if norm > 0 else 0.0


def loudness(pcm: array) -> float:
    """Root mean square of the samples."""
    return math.sqrt(sum(map(operator.mul, pcm, pcm)) / len(pcm)) if len(pcm) else 0.0


def _blocks(signal: list[int], size: int) -> list[int]:
    return [sum(signal[i : i + size]) for i in range(0, len(signal) - size + 1, size)]


def align(
    dump: array,
    pcm: array,
    expected_lag: int,
    tolerance: int = RATE // 4,
    at: int = RATE,
    length: int = RATE,
) -> Match:
    """Compare the track's frames [at, at + length) with the dump for every
    start of the track within `tolerance` frames of `expected_lag`, and return
    the best. A coarse pass on block sums, then every frame around its best."""
    reference = mono(pcm, at, length)
    if len(reference) < length:
        raise ValueError("the track is shorter than the window")
    first = max(expected_lag - tolerance, -at)
    region = mono(dump, first + at, 2 * tolerance + length + COARSE)
    if len(region) < length:
        raise ValueError("the dump ends before the window")
    coarse_reference = _blocks(reference, COARSE)
    coarse_region = _blocks(region, COARSE)
    steps = len(coarse_region) - len(coarse_reference) + 1
    scores = [
        ncc(coarse_region[s : s + len(coarse_reference)], coarse_reference) for s in range(steps)
    ]
    centre = max(range(steps), key=scores.__getitem__) * COARSE
    best_shift, best_score = centre, -2.0
    for shift in range(max(0, centre - COARSE), min(len(region) - length, centre + COARSE) + 1):
        value = ncc(region[shift : shift + length], reference)
        if value > best_score:
            best_shift, best_score = shift, value
    lag = first + best_shift
    theirs = pcm[2 * at : 2 * (at + length)]
    ours = dump[2 * (lag + at) : 2 * (lag + at + length)]
    exact = sum(map(operator.eq, ours, theirs)) / len(theirs)
    return Match(lag, best_score, exact, at)


def score_at(dump: array, pcm: array, lag: int, at: int, length: int = RATE) -> float:
    """The correlation of the track's frames [at, at + length) with the dump,
    for a track that started at dump frame `lag`."""
    return ncc(mono(dump, lag + at, length), mono(pcm, at, length))


def locate(dump: array, pcm: array, at: int, length: int = RATE) -> Match:
    """Find dump frames [at, at + length) anywhere in a track (numpy): a
    search on decimated mono by FFT, then every frame around the best places."""
    import numpy as np

    decimate = 8
    window = np.array(mono(dump, at, length), dtype=np.float64)
    track = np.frombuffer(pcm.tobytes(), dtype="<i2").reshape(-1, 2).astype(np.float64).sum(axis=1)
    if len(window) < length or len(track) < length:
        raise ValueError("the window does not fit")

    def small(signal):
        return signal[: len(signal) // decimate * decimate].reshape(-1, decimate).mean(axis=1)

    small_window, small_track = small(window), small(track)
    m, n = len(small_window), len(small_track)
    size = 1 << (n + m).bit_length()
    spectrum = np.fft.rfft(small_track, size) * np.conj(np.fft.rfft(small_window, size))
    corr = np.fft.irfft(spectrum, size)[: n - m + 1]
    energy = np.concatenate(([0.0], np.cumsum(small_track * small_track)))
    product = (energy[m:] - energy[:-m]) * (small_window**2).sum()
    coarse = corr / np.sqrt(np.maximum(product, 1e-9))[: len(corr)]
    window_energy = (window**2).sum()
    best = Match(at, -2.0, 0.0, 0)
    for place in np.argsort(coarse)[-3:]:
        centre = int(place) * decimate
        for q in range(max(0, centre - 16), min(len(track) - length, centre + 16) + 1):
            piece = track[q : q + length]
            denominator = math.sqrt(window_energy * (piece**2).sum())
            value = float((window * piece).sum() / denominator) if denominator > 0 else 0.0
            if value > best.score:
                best = Match(at - q, value, 0.0, q)
    theirs = pcm[2 * best.track_frame : 2 * (best.track_frame + length)]
    ours = dump[2 * at : 2 * (at + length)]
    best.exact = sum(map(operator.eq, ours, theirs)) / len(theirs)
    return best


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    dump = read_wav(sys.argv[1])
    table = track_table(sys.argv[2])
    wanted = [int(n) for n in sys.argv[3:]] or [n for n, t in table.items() if t.audio]
    total = frames_of(dump)
    places = range(0, total - RATE + 1, RATE)
    if not places:
        print("the dump is shorter than one second")
        return 1
    print(f"dump: {total} frames ({total / RATE:.2f} s)")
    for number in wanted:
        pcm = track_pcm(table[number])
        best = max((locate(dump, pcm, at) for at in places), key=lambda match: match.score)
        print(
            f"track {number:02d}: best score {best.score:+.3f}, dump frame "
            f"{best.lag + best.track_frame} = track frame {best.track_frame} "
            f"({best.track_frame / RATE:.2f} s), lag {best.lag}, exact {best.exact:.2f}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
