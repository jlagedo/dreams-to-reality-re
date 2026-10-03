"""Sweep camera poses over a captured level: the faces retail draws against the direct renderer's.

uv run --with unicorn --with pillow python recomp/windream/verify/render_pose_sweep.py \\
    out/recomp/render-parity/cases/p046-0.wdmi --poses 200

Input: a memory image from the control channel's memory_dump (render_parity.py
capture writes one per case). No game runs and nobody plays: each pose is
written into the image and both sides compute that frame.

Per pose:
- The camera node (the render root) gets an eye (+0x1c) and a Q15 rotation
  (+0x28), and the composed camera matrix retail keeps at +0x4c (rotation
  transposed, translation -R^T eye). REND_DrawScene does not compose the camera
  itself; composed_camera() is checked against every input image first.
- Retail: REND_DrawScene (0x47e700) runs on Unicorn with the pixel backend
  stubbed (render_smoke.Replay). At SW_DrawObjectFaces (0x473014), the face hook,
  each face block's visible list (+0x24, next +4) after REND_CullFaces holds
  the faces retail draws (bit 1 clear); REND_ClipFaceNear (0x479320) receives
  those that cross the near plane. That set is the 3dfx build's too: the same
  REND_* routines, and its hook walks the same +0xa4 list.
- Direct: the native adapter (WDSceneAdapterOracle.dll, capture_scene) reads
  the same memory, and the direct renderer's decisions for each face are
  replayed on the CPU (render_scene_draw.cpp): node submitted, far flag (bit 1),
  a type Glide draws, the near-plane clip, the GPU back-face test
  (SG_CULLMODE_BACK, CCW in NDC) and the viewport at the original aspect.

A face retail draws and direct does not is "missing" (geometry that would be
black), with the direct stage that dropped it and the pixels it would cover in
the 3D viewport; the reverse is "extra". Types Glide never draws are left out
of both, and so are faces seen edge-on (the eye within one unit of the stored
plane: retail keeps a sliver of the back). A pose fails when what it misses
covers --fail-pixels. With --browser N the first N posed images also go
through render_parity's harness: the wasm32 adapter must write the same scene
bytes as the 64-bit one, and WebGL2 the same pixels as D3D11. Reports and
images (missing red, extra blue) go to DREAMS_OUT/recomp/pose-sweep/<image>/.

Assumption, stated: the adapter sees node transforms (+0x4c..+0x7c) composed
for the pose's camera, as after retail's draw. In the live game the capture
reads what was composed last; a one-frame lag there is not modelled.
"""

from __future__ import annotations

import argparse
import bisect
import collections
import ctypes
import json
import math
import random
import shutil
import struct
import sys
from pathlib import Path

from mdmp import MemoryImage
from render_scene_smoke import compose, point, read_snapshot
from render_smoke import DRAW_SCENE, Replay
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as xr

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

DRAW_FACES = 0x473014  # SW_DrawObjectFaces: REND_DrawObject's face hook, EAX = node
CLIP_NEAR = 0x479320  # REND_ClipFaceNear(EAX block, EDX node, EBX face)
VIEWPORT = 0x661E90


def glide_no_draw(kind):
    """render_scene_draw.cpp glide_no_draw: types the Glide hook skips."""
    return (
        -15 <= kind <= -8
        or kind == -2
        or 4 <= kind <= 8
        or 10 <= kind <= 0xC
        or kind in (0x11, 0x12, 0x14, 0x15)
        or 0x19 <= kind <= 0x1F
    )


def composed_camera(eye, rotation):
    """The camera node's +0x4c: translation -(R^T eye >> 15), then R^T."""
    transposed = [rotation[c * 3 + r] for r in range(3) for c in range(3)]
    translation = [-(sum(transposed[r * 3 + c] * eye[c] for c in range(3)) >> 15) for r in range(3)]
    return translation + transposed


class Camera:
    """Pose generator in the captured camera's frame: world down is the axis its
    screen-down column (rotation column 1) is closest to."""

    def __init__(self, eye, rotation):
        self.eye = eye
        self.rotation = rotation
        down = [rotation[r * 3 + 1] for r in range(3)]
        axis = max(range(3), key=lambda a: abs(down[a]))
        self.down = [0.0, 0.0, 0.0]
        self.down[axis] = math.copysign(1.0, down[axis])
        self.horizontal = [a for a in range(3) if a != axis]
        m = [[rotation[r * 3 + c] for c in range(3)] for r in range(3)]
        self.det_sign = math.copysign(1, det3(m))

    def rotation_for(self, yaw, pitch):
        """Q15 camera-to-world rotation, row-major, columns right/down/forward.
        pitch > 0 looks down."""
        h = [0.0, 0.0, 0.0]
        h[self.horizontal[0]] = math.cos(yaw)
        h[self.horizontal[1]] = math.sin(yaw)
        forward = [math.cos(pitch) * h[i] + math.sin(pitch) * self.down[i] for i in range(3)]
        down = [-math.sin(pitch) * h[i] + math.cos(pitch) * self.down[i] for i in range(3)]
        right = cross(down, forward)
        columns = [right, down, forward]
        if math.copysign(1, det3([[columns[c][r] for c in range(3)] for r in range(3)])) != (
            self.det_sign
        ):
            columns[0] = [-v for v in right]
        return [round(columns[c][r] * 32767) for r in range(3) for c in range(3)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def det3(m):
    return (
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    )


class Sweep:
    def __init__(self, image_path: Path, dll, out: Path):
        self.image = MemoryImage(image_path)
        self.root = self.image.root
        assert self.image.u32(0x661EE8) == self.root, "image root is not the scene root"
        self.out = out
        self.replay = Replay(self.image, "full")
        self.drawn: set[int] = set()
        self.clipped: set[int] = set()
        self.replay.uc.hook_add(UC_HOOK_CODE, self._faces, begin=DRAW_FACES, end=DRAW_FACES)
        self.replay.uc.hook_add(UC_HOOK_CODE, self._clip, begin=CLIP_NEAR, end=CLIP_NEAR)
        self.dll = dll
        self.capture_native = dll.wd_capture_scene_file
        self.reader_type = ctypes.CFUNCTYPE(
            ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t
        )
        self.capture_native.argtypes = [
            self.reader_type,
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_char_p,
            ctypes.c_void_p,
            ctypes.c_size_t,
        ]
        self.capture_native.restype = ctypes.c_int
        self.eye = struct.unpack("<3i", self.image.read(self.root + 0x1C, 12))
        self.rotation = struct.unpack("<9i", self.image.read(self.root + 0x28, 36))
        composed = list(struct.unpack("<12i", self.image.read(self.root + 0x4C, 48)))
        if composed_camera(self.eye, self.rotation) != composed:
            raise AssertionError(
                f"composed_camera does not reproduce the captured camera: "
                f"{composed_camera(self.eye, self.rotation)} != {composed}"
            )
        viewport = self.image.read(VIEWPORT, 0x4C)
        self.width = struct.unpack_from("<I", viewport, 0x2C)[0]
        self.height = struct.unpack_from("<I", viewport, 0x38)[0]

    # ---- retail ----
    def _faces(self, uc, _address, _size, _data):
        node = uc.reg_read(xr.UC_X86_REG_EAX)
        self.drawn_nodes.add(node)
        r = self.replay
        block, blocks = r.u32(node + 0xA4), 0
        while block:
            face, count = r.u32(block + 0x24), 0
            while face:
                if not r.u32(face) & 1:
                    self.drawn.add(face)
                face = r.u32(face + 4)
                count += 1
                assert count < 100000, "cyclic visible list"
            block = r.u32(block)
            blocks += 1
            assert blocks < 10000, "cyclic face blocks"

    def _clip(self, uc, _address, _size, _data):
        self.clipped.add(uc.reg_read(xr.UC_X86_REG_EBX))

    def write_image(self, path: Path):
        """The image with every page the replay touched as it is now (a posed
        frame, composed by retail), in the memory_dump format."""
        raw = bytearray(self.image.raw)
        for page in self.replay.pages:
            i = bisect.bisect_right(self.image.starts, page) - 1
            start, size, at = self.image.ranges[i]
            assert start <= page and page + 4096 <= start + size
            raw[at + page - start : at + page - start + 4096] = self.replay.uc.mem_read(page, 4096)
        path.write_bytes(bytes(raw))

    def set_pose(self, eye, rotation):
        self.replay.write(self.root + 0x1C, struct.pack("<3i", *eye))
        self.replay.write(self.root + 0x28, struct.pack("<9i", *rotation))
        self.replay.write(self.root + 0x4C, struct.pack("<12i", *composed_camera(eye, rotation)))

    def retail(self):
        """Faces retail draws, those among them it splits at the near plane, and
        the nodes whose faces reached the hook (not culled as a whole)."""
        self.drawn, self.clipped, self.drawn_nodes = set(), set(), set()
        self.replay.run(DRAW_SCENE, self.root)
        return self.drawn | self.clipped, self.clipped, self.drawn_nodes

    # ---- direct ----
    def capture(self, path: Path):
        replay = self.replay

        @self.reader_type
        def reader(_context, address, destination, size):
            try:
                data = replay.read(address, size)
            except RuntimeError:
                return False
            ctypes.memmove(destination, data, size)
            return True

        error = ctypes.create_string_buffer(512)
        if not self.capture_native(reader, None, self.root, str(path).encode(), error, len(error)):
            raise RuntimeError(f"capture_scene: {error.value.decode(errors='replace')}")
        return read_snapshot(path)

    def direct(self, snapshot):
        """Per face: (stage, screen polygon or None, area inside the 3D viewport in
        pixels). stage "drawn" or the stage of render_scene_draw/the GPU that drops it."""
        camera, nodes, vertices, faces = snapshot
        world = compose(nodes)
        fx, fy = camera["focal"]
        cx, cy, near, _far = camera["projection"]
        vx, vy, vw, vh = camera["viewport"]
        rect = (vx, vy, vx + vw, vy + vh)
        view = camera["view"]
        cache = {}

        def cam(node, vertex):
            key = (node, vertex)
            if key not in cache:
                cache[key] = point(view, point(world[node], vertices[vertex][1]))
            return cache[key]

        result = {}
        for f in faces:
            node = nodes[f["owner"]]
            if not node["active"]:
                result[f["address"]] = ("node", None, 0.0)
                continue
            if f["flags"] & 1:
                result[f["address"]] = ("far_vertex", None, 0.0)
                continue
            if glide_no_draw(f["kind"]):
                result[f["address"]] = ("no_draw_type", None, 0.0)
                continue
            polygon = clip_near([cam(c[0], c[1]) for c in f["corners"]], near)
            if len(polygon) < 3:
                result[f["address"]] = ("behind_near", None, 0.0)
                continue
            screen = [(cx + fx * x / z, cy + fy * y / z) for x, y, z in polygon]
            area = shoelace(screen)
            inside = clip_rect(screen, rect)
            visible = abs(shoelace(inside)) / 2 if len(inside) >= 3 else 0.0
            if not visible:
                stage = "offscreen"
            elif area == 0:
                stage = "degenerate"
            # NDC y is up, screen y down: CCW in NDC (front) is a negative
            # shoelace area in screen coordinates.
            elif area > 0:
                stage = "back_gpu"
            else:
                stage = "drawn"
            result[f["address"]] = (stage, screen, visible)
        return result


def clip_near(points, near):
    """Sutherland-Hodgman against z >= near (the GPU's near plane in camera space)."""
    out = []
    for i, a in enumerate(points):
        b = points[(i + 1) % len(points)]
        ina, inb = a[2] >= near, b[2] >= near
        if ina:
            out.append(a)
        if ina != inb:
            t = (near - a[2]) / (b[2] - a[2])
            out.append(tuple(a[k] + t * (b[k] - a[k]) for k in range(3)))
    return out


def clip_rect(points, rect):
    """Sutherland-Hodgman of a screen polygon against the viewport rectangle."""
    x0, y0, x1, y1 = rect
    edges = (
        (lambda p: p[0] >= x0, 0, x0),
        (lambda p: p[0] <= x1, 0, x1),
        (lambda p: p[1] >= y0, 1, y0),
        (lambda p: p[1] <= y1, 1, y1),
    )
    for inside, axis, bound in edges:
        out = []
        for i, a in enumerate(points):
            b = points[(i + 1) % len(points)]
            ina, inb = inside(a), inside(b)
            if ina:
                out.append(a)
            if ina != inb:
                t = (bound - a[axis]) / (b[axis] - a[axis])
                out.append(tuple(a[k] + t * (b[k] - a[k]) for k in range(2)))
        points = out
        if not points:
            break
    return points


def shoelace(screen):
    return sum(
        screen[i][0] * screen[(i + 1) % len(screen)][1]
        - screen[(i + 1) % len(screen)][0] * screen[i][1]
        for i in range(len(screen))
    )


def horizontal_faces(snapshot, down):
    """World-space centroid and plane height (along down) of the faces within
    ~25 degrees of horizontal, with their XZ triangles for floor lookups."""
    _camera, nodes, vertices, faces = snapshot
    world = compose(nodes)
    found = []
    for f in faces:
        corners = [point(world[c[0]], vertices[c[1]][1]) for c in f["corners"]]
        n = cross(
            [corners[1][k] - corners[0][k] for k in range(3)],
            [corners[2][k] - corners[0][k] for k in range(3)],
        )
        length = math.sqrt(sum(v * v for v in n))
        if length == 0 or abs(sum(n[k] * down[k] for k in range(3))) / length < 0.9:
            continue
        found.append(corners)
    return found


def along(p, down):
    return sum(p[k] * down[k] for k in range(3))


def surfaces_at(eye, horizontals, down, plane_axes):
    """Heights (along down) of the horizontal faces whose footprint contains the eye."""
    a, b = plane_axes
    heights = []
    for corners in horizontals:
        pts = [(c[a], c[b]) for c in corners]
        if not in_triangle((eye[a], eye[b]), pts):
            continue
        # Height of the face plane at the eye's footprint (barycentric).
        (x1, y1), (x2, y2), (x3, y3) = pts
        d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        if d == 0:
            continue
        l1 = ((y2 - y3) * (eye[a] - x3) + (x3 - x2) * (eye[b] - y3)) / d
        l2 = ((y3 - y1) * (eye[a] - x3) + (x1 - x3) * (eye[b] - y3)) / d
        l3 = 1 - l1 - l2
        heights.append(sum(w * along(c, down) for w, c in zip((l1, l2, l3), corners, strict=True)))
    return heights


def in_triangle(p, t):
    def side(a, b, c):
        return (a[0] - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (a[1] - c[1])

    d1, d2, d3 = side(p, t[0], t[1]), side(p, t[1], t[2]), side(p, t[2], t[0])
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


def generate_poses(sweep: Sweep, snapshot, count: int, seed: int):
    """Pose 0 is the captured camera. The others stand over horizontal faces
    that have a surface above them (inside the level), at the captured camera's
    height over its floor, looking in 8 directions at four pitches."""
    camera = Camera(sweep.eye, sweep.rotation)
    down = camera.down
    horizontals = horizontal_faces(snapshot, down)
    eye_h = along(sweep.eye, down)
    below = [h for h in surfaces_at(sweep.eye, horizontals, down, camera.horizontal) if h > eye_h]
    height = (min(below) - eye_h) if below else 600.0
    rng = random.Random(seed)
    poses = [{"eye": list(sweep.eye), "rotation": list(sweep.rotation), "kind": "captured"}]
    candidates = list(range(len(horizontals)))
    rng.shuffle(candidates)
    for index in candidates:
        if len(poses) >= count:
            break
        corners = horizontals[index]
        centre = [sum(c[k] for c in corners) / 3 for k in range(3)]
        eye = [centre[k] - height * down[k] for k in range(3)]
        heights = surfaces_at(eye, horizontals, down, camera.horizontal)
        floor = [h for h in heights if h > along(eye, down)]
        ceiling = [h for h in heights if h < along(eye, down)]
        # Stand on this face (the nearest surface below), under a roof.
        if not floor or abs(min(floor) - along(centre, down)) > 1 or not ceiling:
            continue
        for _ in range(2):
            if len(poses) >= count:
                break
            yaw = rng.randrange(8) * math.pi / 4 + rng.uniform(-0.2, 0.2)
            pitch = math.radians(rng.choice((-10, 0, 15, 35)))
            poses.append(
                {
                    "eye": [round(v) for v in eye],
                    "rotation": camera.rotation_for(yaw, pitch),
                    "kind": "floor",
                    "yaw_degrees": round(math.degrees(yaw), 1),
                    "pitch_degrees": round(math.degrees(pitch), 1),
                }
            )
    return poses, height


def face_detail(sweep: Sweep, face_index, address, area, clipped):
    """What retail's back-face test saw: REND_CullFaces compares the face
    normal's n.eye (normal +0xc, REND_ComputeNormalDots) with the face's plane
    distance (+0x30); flag 8 recomputes the normal from the camera-space corners."""
    r = sweep.replay
    f = face_index[address]
    normal = r.u32(address + 0x2C)
    detail = {
        "face": hex(address),
        "kind": f["kind"],
        "visible_pixels": round(area, 1),
        "retail_clipped_near": address in clipped,
        "flags": hex(r.u32(address)),
    }
    if normal and not f["flags"] & 8:
        dot = struct.unpack("<i", r.read(normal + 0xC, 4))[0]
        plane = struct.unpack("<i", r.read(address + 0x30, 4))[0]
        detail.update(normal=hex(normal), normal_dot=dot, plane=plane, margin=dot - plane)
    return detail


def run_pose(sweep: Sweep, pose, scratch: Path):
    sweep.set_pose(pose["eye"], pose["rotation"])
    retail, clipped, retail_nodes = sweep.retail()
    snapshot = sweep.capture(scratch)
    direct = sweep.direct(snapshot)
    _camera, nodes, _vertices, faces = snapshot
    face_index = {f["address"]: f for f in faces}
    known = set(direct)
    retail_known = retail & known
    # A face retail draws that direct drops is a failure only if it would
    # have covered pixels of the 3D viewport.
    # An eye within one unit of the face's stored plane (REND_CullFaces draws
    # at n.eye - plane >= 0) sees the face edge-on: retail keeps a thin wedge
    # of its back, the GPU's winding drops it. Reported, not a gap.
    missing = collections.defaultdict(list)
    harmless = collections.Counter()
    for face in sorted(retail_known):
        stage, screen, area = direct[face]
        if stage == "drawn" or stage == "no_draw_type":
            continue
        if area < 1:
            harmless[stage] += 1
            continue
        if stage == "back_gpu":
            margin = face_detail(sweep, face_index, face, area, clipped).get("margin")
            if margin is not None and 0 <= margin <= 1:
                harmless["edge_on"] += 1
                continue
        missing[stage].append((face, area, screen))
    extra = collections.defaultdict(list)
    for face, (stage, screen, area) in direct.items():
        if stage == "drawn" and face not in retail and area >= 1:
            owner = nodes[face_index[face]["owner"]]["address"]
            why = "retail_face_culled" if owner in retail_nodes else "retail_node_culled"
            extra[why].append((face, area, screen))
    details = [
        face_detail(sweep, face_index, face, area, clipped)
        for entries in missing.values()
        for face, area, _ in sorted(entries, key=lambda e: -e[1])[:10]
    ]
    return (
        {
            "retail_faces": len(retail_known),
            "retail_clipped": len(clipped & known),
            "retail_unknown_faces": len(retail - known),
            "direct_faces": sum(1 for s in direct.values() if s[0] == "drawn"),
            "missing": {k: len(v) for k, v in missing.items()},
            "missing_pixels": {k: round(sum(a for _, a, _ in v)) for k, v in missing.items()},
            "missing_without_pixels": dict(harmless),
            "extra": {k: len(v) for k, v in extra.items()},
            "extra_pixels": {k: round(sum(a for _, a, _ in v)) for k, v in extra.items()},
            "missing_detail": details,
        },
        missing,
        [e for v in extra.values() for e in v],
        direct,
    )


def browser_parity(cases: list[Path], out: Path, swiftshader: bool) -> dict[str, dict]:
    """Each posed image through render_parity's harness: the adapter on a 64-bit
    host and on wasm32 must write the same scene bytes, and D3D11 and WebGL2
    the same pixels (render_parity's tolerance and budget)."""
    import render_parity as rp
    from PIL import Image

    rp.CASES = cases[0].parent
    native_errors = rp.run_native(cases, out / "native")
    web_errors, gpu = rp.run_web(cases, swiftshader, False, out / "web")
    rows = {}
    for path in cases:
        name = rp.out_name(path)
        row = rows[path.stem] = {"gpu": gpu}
        if name in native_errors or name in web_errors:
            row["error"] = native_errors.get(name) or web_errors.get(name)
            continue
        a = (out / "native" / (name + ".wds")).read_bytes()
        b = (out / "web" / (name + ".wds")).read_bytes()
        row["data_same"] = a == b
        frame_a = rp.read_rgba(out / "native" / (name + ".rgba"))
        frame_b = rp.read_rgba(out / "web" / (name + ".rgba"))
        row.update(rp.pixel_stats(frame_a, frame_b, 2))
        row["ok"] = row["data_same"] and row["beyond"] <= 0.002 * rp.WIDTH * rp.HEIGHT
        if not row["ok"]:
            sheet = Image.new("RGB", (rp.WIDTH * 2, rp.HEIGHT))
            for i, frame in enumerate((frame_a, frame_b)):
                sheet.paste(Image.frombytes("RGBA", (rp.WIDTH, rp.HEIGHT), frame).convert("RGB"),
                            (rp.WIDTH * i, 0))  # fmt: skip
            sheet.save(out / f"{path.stem}-windows-browser.png")
    return rows


def draw(path: Path, direct, missing, extra, width, height):
    from PIL import Image, ImageDraw

    image = Image.new("RGB", (width, height), (0, 0, 0))
    g = ImageDraw.Draw(image)
    for stage, screen, _ in direct.values():
        if stage == "drawn" and screen:
            g.polygon(screen, fill=(70, 70, 70), outline=(110, 110, 110))
    for _face, _area, screen in extra:
        if screen:
            g.polygon(screen, fill=(40, 60, 200))
    for entries in missing.values():
        for _face, _area, screen in entries:
            if screen:
                g.polygon(screen, fill=(220, 30, 30))
    image.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("images", nargs="+", type=Path, help="memory_dump files (.wdmi)")
    ap.add_argument(
        "--poses", type=int, default=100, help="poses per image, the captured one first"
    )
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--images-for", type=int, default=12, help="draw the N worst poses")
    ap.add_argument(
        "--fail-pixels",
        type=int,
        default=64,
        help="a pose fails when the faces it misses cover this many pixels (slivers are reported)",
    )
    ap.add_argument(
        "--browser",
        type=int,
        default=0,
        help="also run the first N poses through render_parity (Windows D3D11 and the "
        "browser's WebGL2; build with render_parity.py build first)",
    )
    ap.add_argument("--swiftshader", action="store_true", help="software WebGL2 in the browser")
    args = ap.parse_args()
    dll = ctypes.CDLL(
        str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll")
    )
    failures = 0
    for image_path in args.images:
        out = recomp_env.out_dir("pose-sweep", image_path.stem)
        sweep = Sweep(image_path, dll, out)
        scratch = out / "pose.wds"
        sweep.set_pose(sweep.eye, sweep.rotation)
        sweep.retail()
        base = sweep.capture(scratch)
        poses, height = generate_poses(sweep, base, args.poses, args.seed)
        results = []
        cases = out / "cases"
        shutil.rmtree(cases, ignore_errors=True)
        cases.mkdir()
        for i, pose in enumerate(poses):
            summary, missing, extra, direct = run_pose(sweep, pose, scratch)
            summary.update(pose=i, **pose)
            results.append((summary, missing, extra, direct))
            if i < args.browser:
                sweep.write_image(cases / f"pose{i:03d}.wdmi")
            print(
                f"{image_path.stem} pose {i:3d} {pose['kind']:8s} "
                f"retail {summary['retail_faces']:5d} direct {summary['direct_faces']:5d} "
                f"missing {summary['missing_pixels']} px "
                f"extra {summary['extra_pixels']} px",
                flush=True,
            )
        worst = sorted(results, key=lambda r: -sum(r[0]["missing_pixels"].values()))
        for summary, missing, extra, direct in worst[: args.images_for]:
            if summary["missing"]:
                draw(out / f"pose{summary['pose']:03d}.png", direct, missing, extra,
                     sweep.width, sweep.height)  # fmt: skip
        browser = {}
        if args.browser:
            browser = browser_parity(sorted(cases.glob("*.wdmi")), out, args.swiftshader)
            for name, row in browser.items():
                state = "ok" if row.get("ok") else "FAIL"
                print(f"{image_path.stem} {name} browser {state}: " + json.dumps(row), flush=True)
            shutil.rmtree(cases, ignore_errors=True)  # 18 MB each
        totals, pixels, extra_pixels = (collections.Counter() for _ in range(3))
        for summary, *_ in results:
            totals.update(summary["missing"])
            pixels.update(summary["missing_pixels"])
            extra_pixels.update(summary["extra_pixels"])
        report = {
            "image": str(image_path),
            "poses": len(results),
            "camera_height": round(height),
            "screen": [sweep.width, sweep.height],
            "poses_with_missing": sum(1 for s, *_ in results if s["missing"]),
            "missing_faces_by_stage": dict(totals),
            "missing_pixels_by_stage": dict(pixels),
            "poses_with_extra": sum(1 for s, *_ in results if s["extra"]),
            "extra_pixels_by_retail_reason": dict(extra_pixels),
            "browser_poses": len(browser),
            "browser_failures": sorted(n for n, r in browser.items() if not r.get("ok")),
            "browser": browser,
            "fail_pixels": args.fail_pixels,
            "failed_poses": [
                s["pose"]
                for s, *_ in results
                if sum(s["missing_pixels"].values()) >= args.fail_pixels
            ],
            "results": [s for s, *_ in results],
        }
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(
            f"{image_path.stem}: {report['poses_with_missing']}/{len(results)} poses miss faces "
            f"{dict(totals)} ({dict(pixels)} px), {len(report['failed_poses'])} by "
            f">= {args.fail_pixels} px; {report['poses_with_extra']} draw extra "
            f"{dict(extra_pixels)} px; {len(report['browser_failures'])}/{len(browser)} "
            f"browser failures; {out / 'report.json'}"
        )
        failures += len(report["failed_poses"]) + len(report["browser_failures"])
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
