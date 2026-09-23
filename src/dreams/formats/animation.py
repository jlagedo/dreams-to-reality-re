"""Character and creature animation keys (``.DAN`` Tag 3 / ``.3DA``).

``.DAN`` is a container holding 3D model parts (``.3DM``) and animation clips
(``.3DA``). The container opens with a ``DANF`` header declaring two directories:

    +0x00  char[5]   "DANF\\0"
    +0x05  u32       file size
    +0x09  u8        flag (0)
    +0x0a  u32       header size
    +0x0e  u16       count of .3DM models (count1)
    +0x10  11*count1 model name directory (8 bytes name + 3 bytes padding)
    ...    u16       count of .3DA animation clips (count2)
    ...    13*count2 animation clip directory (12 bytes name + 1 byte null)

Each animation clip is stored in a Tag 3 chunk, compressed with Cryo's LZ codec
(:mod:`dreams.formats.lz`). Decompressed payload layout:

    +0x14  u32       track count N (includes control tracks on some models)
    +0x18  u32[N]    offsets of each node's track

The table is followed by track data, not a duration/FPS header. In Duncan,
the first two words after it are the root track's duration and rotation count.
The nominal 30 frames/second base comes from WINDREAM.EXE's clock calculation,
not this file. Actor/state speed modifiers are separate; see animation-timing.md.

Each track record contains:
    +0x14  u32       duration in frames
    +0x18  u32       number of keyframes K
    +0x1c  u32       translation key count
    +0x20  u32       start offset of keyframes
    +0x24  u32       translation array pointer (also ends the rotation array)
    +0x28            first keyframe starts here; no rest quaternion in the header

Keyframes are stored as unit quaternions scaled by 32768:
    20-byte linear keys:
        +0x00  u32   frame time
        +0x04  i32   qx
        +0x08  i32   qy
        +0x0c  i32   qz
        +0x10  i32   qw (scaled by 32768 = 1.0)

    60-byte spline rotation keys:
        +0x00  u32   frame time
        +0x04  i32   qx
        +0x08  i32   qy
        +0x0c  i32   qz
        +0x10  i32   qw
        +0x14  ...   spline tangent / control parameters
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from pathlib import Path

from dreams.formats.lz import decompress

DANF_MAGIC = b"DANF\x00"
ENGINE_BASE_FRAME_RATE = 30  #: WINDREAM.EXE 004171b2, constant at 004c41cc


@dataclass
class Keyframe:
    """A single rotation keyframe for a scene-graph node."""

    time: int  #: Frame number (0 .. clip duration)
    rotation: tuple[float, float, float, float]  #: Unit quaternion (x, y, z, w)
    raw_quat: tuple[int, int, int, int]  #: Q15 integers (scaled by 32768)
    ease: tuple[float, float] = (0.0, 0.0)  #: raw float fields at key +0x14/+0x18
    out_control: tuple[float, float, float, float] | None = None  #: key +0x1c
    in_control: tuple[float, float, float, float] | None = None  #: key +0x2c


@dataclass
class TranslationKeyframe:
    time: int
    position: tuple[int, int, int]
    ease: tuple[float, float] = (0.0, 0.0)
    in_tangent: tuple[int, int, int] | None = None
    out_tangent: tuple[int, int, int] | None = None


@dataclass
class AnimationTrack:
    """Animation track for a single node / skeletal bone."""

    node_index: int  #: Original directory/track slot, not geometry scanner order
    duration: int
    num_keys: int
    translation_key_count: int
    stride: int
    rest_rotation: tuple[float, float, float, float]  #: First key rotation, or identity if empty
    keyframes: list[Keyframe] = field(default_factory=list)
    bone_name: str | None = None
    mesh_node_index: int | None = None  #: -1 for a named node without geometry
    translation_keys: list[TranslationKeyframe] = field(default_factory=list)


@dataclass
class AnimationClip:
    """A complete multi-track animation clip (one .3DA stream)."""

    name: str
    duration_frames: int
    frame_rate: int
    track_count: int
    tracks: list[AnimationTrack] = field(default_factory=list)

    def sample_pose(self, frame: float) -> dict[int, tuple[float, float, float, float]]:
        """Sample node rotation quaternions at an arbitrary frame timestamp."""
        pose: dict[int, tuple[float, float, float, float]] = {}
        for trk in self.tracks:
            if not trk.keyframes:
                pose[trk.node_index] = trk.rest_rotation
                continue
            if len(trk.keyframes) == 1 or frame <= trk.keyframes[0].time:
                pose[trk.node_index] = trk.keyframes[0].rotation
                continue
            if frame >= trk.keyframes[-1].time:
                pose[trk.node_index] = trk.keyframes[-1].rotation
                continue

            # Find bounding keyframes
            for k in range(len(trk.keyframes) - 1):
                k0 = trk.keyframes[k]
                k1 = trk.keyframes[k + 1]
                if k0.time <= frame <= k1.time:
                    dt = k1.time - k0.time
                    alpha = (frame - k0.time) / dt if dt > 0 else 0.0
                    pose[trk.node_index] = sample_rotation(k0, k1, alpha)
                    break
        return pose

    def to_dict(self) -> dict:
        """Convert animation clip to JSON-serializable dictionary."""
        return {
            "name": self.name,
            "duration": self.duration_frames,
            "frameRate": self.frame_rate,
            "frameRateSource": "windream-engine-base",
            "trackCount": self.track_count,
            "tracks": [
                {
                    "nodeIndex": t.node_index,
                    "boneName": t.bone_name,
                    "meshNodeIndex": t.mesh_node_index,
                    "duration": t.duration,
                    "translationKeyCount": t.translation_key_count,
                    "keyStride": t.stride,
                    "translationKeys": [
                        {
                            "time": k.time,
                            "position": k.position,
                            "ease": k.ease,
                            "inTangent": k.in_tangent,
                            "outTangent": k.out_tangent,
                        }
                        for k in t.translation_keys
                    ],
                    "restRotation": list(t.rest_rotation),
                    "keyframes": [
                        {
                            "time": k.time,
                            "rotation": list(k.rotation),
                            **(
                                {
                                    "ease": list(k.ease),
                                    "outControl": k.out_control,
                                    "inControl": k.in_control,
                                }
                                if t.stride == 60
                                else {}
                            ),
                        }
                        for k in t.keyframes
                    ],
                }
                for t in self.tracks
            ],
        }

    def sample_translations(self, frame: float) -> dict[int, tuple[float, float, float]]:
        result = {}
        for track in self.tracks:
            keys = track.translation_keys
            if not keys:
                continue
            if frame <= keys[0].time:
                result[track.node_index] = keys[0].position
            elif frame >= keys[-1].time:
                result[track.node_index] = keys[-1].position
            else:
                for a, b in zip(keys, keys[1:], strict=False):
                    if a.time <= frame < b.time:
                        result[track.node_index] = sample_translation(
                            a, b, (frame - a.time) / (b.time - a.time)
                        )
                        break
        return result


def slerp(
    q1: tuple[float, float, float, float],
    q2: tuple[float, float, float, float],
    t: float,
    shortest_path: bool = True,
) -> tuple[float, float, float, float]:
    """Spherical linear interpolation between two unit quaternions (x, y, z, w)."""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2

    dot = x1 * x2 + y1 * y2 + z1 * z2 + w1 * w2
    if dot < 0.0 and shortest_path:
        dot = -dot
        x2, y2, z2, w2 = -x2, -y2, -z2, -w2

    dot = min(1.0, max(-1.0, dot))
    if dot < -0.9995:
        # Continuous great-circle choice for the antipodal degeneracy.
        orthogonal = (-y1, x1, -w1, z1)
        return tuple(
            a * math.cos(math.pi * t) + b * math.sin(math.pi * t)
            for a, b in zip(q1, orthogonal, strict=True)
        )
    if dot > 0.9995:
        # Linear interpolation if almost identical
        xr = x1 + t * (x2 - x1)
        yr = y1 + t * (y2 - y1)
        zr = z1 + t * (z2 - z1)
        wr = w1 + t * (w2 - w1)
        length = math.sqrt(xr * xr + yr * yr + zr * zr + wr * wr)
        if length > 0:
            return (xr / length, yr / length, zr / length, wr / length)
        return (0.0, 0.0, 0.0, 1.0)

    theta_0 = math.acos(dot)
    sin_theta_0 = math.sin(theta_0)
    theta = theta_0 * t
    sin_theta = math.sin(theta)

    s1 = math.cos(theta) - dot * sin_theta / sin_theta_0
    s2 = sin_theta / sin_theta_0

    xr = s1 * x1 + s2 * x2
    yr = s1 * y1 + s2 * y2
    zr = s1 * z1 + s2 * z2
    wr = s1 * w1 + s2 * w2
    return (xr, yr, zr, wr)


def spline_ease(t: float, left_field: float, right_field: float) -> float:
    """Float transcription of 00459ec0; field order follows the caller pushes.

    The incoming right key's +0x18 field controls the initial ramp; the left
    key's +0x14 controls the final ramp. All currently surveyed fields are zero.
    """
    start, end = right_field, left_field
    total = start + end
    if total == 0:
        return t
    if total > 1:
        start, end = start / total, end / total
    scale = 1 / (2 - start - end)
    if t < start:
        return scale * t * t / start
    if t >= 1 - end and end > 0:
        return 1 - scale * (1 - t) ** 2 / end
    return (2 * t - start) * scale


def sample_rotation(left: Keyframe, right: Keyframe, t: float):
    """Engine SQUAD structure in floating point; zero controls use SLERP."""
    if left.out_control is None or right.in_control is None:
        return slerp(left.rotation, right.rotation, t)
    u = spline_ease(t, left.ease[0], right.ease[1])
    base = slerp(left.rotation, right.rotation, u, shortest_path=False)
    control = slerp(left.out_control, right.in_control, u, shortest_path=False)
    result = slerp(base, control, 2 * u * (1 - u), shortest_path=False)
    norm = math.sqrt(sum(v * v for v in result))
    return tuple(v / norm for v in result)


def sample_translation(left: TranslationKeyframe, right: TranslationKeyframe, t: float):
    """Hermite basis at 004aa710. Tangents are already interval-scaled."""
    if left.out_tangent is None or right.in_tangent is None:
        return tuple(a + (b - a) * t for a, b in zip(left.position, right.position, strict=True))
    u = spline_ease(t, left.ease[0], right.ease[1])
    h00, h01 = 2 * u**3 - 3 * u**2 + 1, -2 * u**3 + 3 * u**2
    h10, h11 = u**3 - 2 * u**2 + u, u**3 - u**2
    return tuple(
        h00 * a + h01 * b + h10 * out + h11 * inc
        for a, b, out, inc in zip(
            left.position, right.position, left.out_tangent, right.in_tangent, strict=True
        )
    )


def read_dan_animations(path_or_bytes: str | Path | bytes) -> list[AnimationClip]:
    """Extract and decode all Tag 3 animation clips from a .DAN file."""
    if isinstance(path_or_bytes, (str, Path)):
        data = Path(path_or_bytes).read_bytes()
    else:
        data = path_or_bytes

    if len(data) < 16 or not data.startswith(DANF_MAGIC[:4]):
        raise ValueError("Not a valid DANF container")

    n_3dm = struct.unpack_from("<H", data, 14)[0]
    off = 16 + n_3dm * 11
    if off + 2 > len(data):
        return []

    n_3da = struct.unpack_from("<H", data, off)[0]
    off += 2

    clip_names = []
    for _ in range(n_3da):
        if off + 13 > len(data):
            break
        name = data[off : off + 12].rstrip(b"\x00").decode("latin-1", "replace")
        clip_names.append(name)
        off += 13

    clips: list[AnimationClip] = []
    tag3_idx = 0
    rig = None

    while off + 5 <= len(data):
        tag = data[off]
        tag_len = struct.unpack_from("<I", data, off + 1)[0]
        if tag_len < 5 or off + tag_len > len(data):
            break

        if tag == 1 and rig is None:
            from dreams.formats.rig import read_rig

            rig = read_rig(decompress(data[off + 5 : off + tag_len]))
        elif tag == 3:
            payload = data[off + 5 : off + tag_len]
            decomp = decompress(payload)
            clip_name = (
                clip_names[tag3_idx] if tag3_idx < len(clip_names) else f"ANIM_{tag3_idx:03d}.3DA"
            )
            clip = _parse_tag3_payload(decomp, clip_name)
            clips.append(clip)
            tag3_idx += 1

        off += tag_len

    for clip in clips:
        # Some models have differently sized clip directories; do not guess
        # bindings for those. Unresolved mappings remain explicitly null.
        if rig is not None and len(rig) == clip.track_count:
            for track, nd in zip(clip.tracks, rig, strict=True):
                track.bone_name = nd.name
                track.mesh_node_index = nd.mesh_index if nd.mesh_index is not None else -1
    return clips


def _parse_tag3_payload(buf: bytes, clip_name: str) -> AnimationClip:
    """Decode an uncompressed Tag 3 byte stream into an AnimationClip."""
    if len(buf) < 0x24:
        return AnimationClip(clip_name, 0, ENGINE_BASE_FRAME_RATE, 0, [])

    track_count = struct.unpack_from("<I", buf, 0x14)[0]
    if track_count < 1:
        return AnimationClip(clip_name, 0, ENGINE_BASE_FRAME_RATE, 0, [])

    num_tracks = track_count
    if 0x18 + num_tracks * 4 > len(buf):
        raise ValueError(f"{clip_name}: truncated track directory")
    frame_rate = ENGINE_BASE_FRAME_RATE  # Executable base rate, not clip metadata.

    offsets = [struct.unpack_from("<I", buf, 0x18 + i * 4)[0] for i in range(num_tracks)]
    tracks: list[AnimationTrack] = []

    for node_idx, trk_off in enumerate(offsets):
        if trk_off + 0x28 > len(buf):
            continue

        duration = struct.unpack_from("<I", buf, trk_off + 0x14)[0]
        num_keys = struct.unpack_from("<I", buf, trk_off + 0x18)[0]
        translation_key_count = struct.unpack_from("<I", buf, trk_off + 0x1C)[0]
        start_keys = struct.unpack_from("<I", buf, trk_off + 0x20)[0]
        end_keys = struct.unpack_from("<I", buf, trk_off + 0x24)[0]

        stride = (end_keys - start_keys) // num_keys if num_keys > 0 else 0
        keyframes: list[Keyframe] = []
        step = stride if stride in (20, 60) else 60
        if num_keys > 0:
            for k in range(num_keys):
                k_at = trk_off + 40 + k * step
                if k_at + 20 <= len(buf):
                    t = struct.unpack_from("<I", buf, k_at)[0]
                    qx, qy, qz, qw = struct.unpack_from("<4i", buf, k_at + 4)
                    norm = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
                    unit_quat = (
                        (qx / norm, qy / norm, qz / norm, qw / norm)
                        if norm > 0
                        else (0.0, 0.0, 0.0, 1.0)
                    )
                    key = Keyframe(t, unit_quat, (qx, qy, qz, qw))
                    if stride == 60 and k_at + 60 <= len(buf):
                        key.ease = struct.unpack_from("<2f", buf, k_at + 20)
                        controls = []
                        for offset in (28, 44):
                            raw = struct.unpack_from("<4i", buf, k_at + offset)
                            length = math.sqrt(sum(v * v for v in raw))
                            controls.append(tuple(v / length for v in raw) if length else None)
                        key.out_control, key.in_control = controls
                    keyframes.append(key)

        # +0x28 is key 0's timestamp, not a separate rest quaternion.
        rest_quat = keyframes[0].rotation if keyframes else (0.0, 0.0, 0.0, 1.0)

        translation_keys = []
        if translation_key_count:
            position_stride = {20: 16, 60: 48}.get(stride)
            if position_stride is None:
                raise ValueError(f"{clip_name}: unsupported translation record layout")
            start = end_keys + 20
            if start + position_stride * translation_key_count > len(buf):
                raise ValueError(f"{clip_name}: translation array exceeds payload")
            for k in range(translation_key_count):
                at = start + k * position_stride
                timestamp, x, y, z = struct.unpack_from("<4i", buf, at)
                key = TranslationKeyframe(timestamp, (x, y, z))
                if position_stride == 48:
                    key.ease = struct.unpack_from("<2f", buf, at + 16)
                    key.in_tangent = struct.unpack_from("<3i", buf, at + 24)
                    key.out_tangent = struct.unpack_from("<3i", buf, at + 36)
                translation_keys.append(key)
            if any(
                a.time > b.time
                for a, b in zip(translation_keys, translation_keys[1:], strict=False)
            ):
                raise ValueError(f"{clip_name}: nonmonotonic translation keys")

        tracks.append(
            AnimationTrack(
                node_index=node_idx,
                duration=duration,
                num_keys=num_keys,
                translation_key_count=translation_key_count,
                stride=stride,
                rest_rotation=rest_quat,
                keyframes=keyframes,
                translation_keys=translation_keys,
            )
        )

    return AnimationClip(
        name=clip_name,
        duration_frames=max((t.duration for t in tracks), default=0),
        frame_rate=frame_rate,
        track_count=len(tracks),
        tracks=tracks,
    )
