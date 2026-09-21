"""Minimal glTF 2.0 writer - no dependencies, no glTF library.

Emits a ``.gltf`` next to a ``.bin`` buffer, which is what Blender's importer
expects. Geometry is written **unindexed**, three vertices per triangle: the
source stores one UV per corner rather than per vertex, so sharing positions
would mean splitting them again anyway.

Coordinates are converted from the engine's axes to glTF's: Y is negated,
because the engine puts the floor at 0 and the ceiling at a large negative Y.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

COMPONENT_FLOAT = 5126
TARGET_ARRAY_BUFFER = 34962


@dataclass
class Primitive:
    name: str
    positions: list[tuple[float, float, float]]
    uvs: list[tuple[float, float]]
    material: int = 0


@dataclass
class Scene:
    name: str
    primitives: list[Primitive] = field(default_factory=list)
    materials: list[tuple[str, str | None]] = field(default_factory=list)


def write(target: str | Path, doc: Scene, scale: float = 0.01) -> Path:
    """Write ``doc`` as ``target`` (a ``.gltf``) plus a sibling ``.bin``."""
    out = Path(target)
    out.parent.mkdir(parents=True, exist_ok=True)
    bin_name = out.with_suffix(".bin").name

    blob = bytearray()
    views: list[dict] = []
    accessors: list[dict] = []
    meshes: list[dict] = []
    nodes: list[dict] = []

    def add_view(data: bytes) -> int:
        while len(blob) % 4:
            blob.append(0)
        views.append(
            {
                "buffer": 0,
                "byteOffset": len(blob),
                "byteLength": len(data),
                "target": TARGET_ARRAY_BUFFER,
            }
        )
        blob.extend(data)
        return len(views) - 1

    for prim in doc.primitives:
        if not prim.positions:
            continue
        pts = [(x * scale, -y * scale, z * scale) for x, y, z in prim.positions]
        pos = add_view(b"".join(struct.pack("<3f", *p) for p in pts))
        accessors.append(
            {
                "bufferView": pos,
                "componentType": COMPONENT_FLOAT,
                "count": len(pts),
                "type": "VEC3",
                "min": [min(p[i] for p in pts) for i in range(3)],
                "max": [max(p[i] for p in pts) for i in range(3)],
            }
        )
        a_pos = len(accessors) - 1

        uv = add_view(b"".join(struct.pack("<2f", *t) for t in prim.uvs))
        accessors.append(
            {
                "bufferView": uv,
                "componentType": COMPONENT_FLOAT,
                "count": len(prim.uvs),
                "type": "VEC2",
            }
        )
        a_uv = len(accessors) - 1

        meshes.append(
            {
                "name": prim.name,
                "primitives": [
                    {
                        "attributes": {"POSITION": a_pos, "TEXCOORD_0": a_uv},
                        "material": prim.material,
                    }
                ],
            }
        )
        nodes.append({"mesh": len(meshes) - 1, "name": prim.name})

    images: list[dict] = []
    textures: list[dict] = []
    materials: list[dict] = []
    for name, texture in doc.materials:
        entry: dict = {"name": name, "doubleSided": True}
        if texture:
            images.append({"uri": texture})
            textures.append({"source": len(images) - 1})
            entry["pbrMetallicRoughness"] = {
                "baseColorTexture": {"index": len(textures) - 1},
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            }
        else:
            entry["pbrMetallicRoughness"] = {
                "baseColorFactor": [0.8, 0.8, 0.8, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            }
        materials.append(entry)

    gltf = {
        "asset": {"version": "2.0", "generator": "dreams (Dreams to Reality RE)"},
        "scene": 0,
        "scenes": [{"name": doc.name, "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials or [{"name": "default"}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"uri": bin_name, "byteLength": len(blob)}],
    }
    if images:
        gltf["images"] = images
        gltf["textures"] = textures

    out.with_suffix(".bin").write_bytes(bytes(blob))
    out.write_text(json.dumps(gltf, indent=1), encoding="utf-8")
    return out


def from_scene(path, out_dir: str | Path, textures: bool = True) -> tuple[Path, dict]:
    """Export one ``.DSN`` as glTF. Returns ``(gltf_path, stats)``.

    Writes one 256x256 PNG per object - the surface interleaved from that
    object's 64 subsampled planes, which is exactly the space the UVs address
    (they take the values 0, 127.5 and 255 of a 256-unit range).
    """
    from dreams import png
    from dreams.formats import mesh as _mesh
    from dreams.formats import scene as _scene

    m = _mesh.read_mesh(path)
    source = "tag1"
    hit, total = _mesh.verify_against_tag2(path)
    if not (total and hit == total):
        # Tag 1's references only resolve for 4 of 95 scenes. Tag 2 carries its
        # own triangle array whose references resolve arithmetically, so it
        # decodes everywhere - at the cost of object names, materials and UVs.
        m = _mesh.read_tri_mesh(path)
        source = "tag2"
        textures = False
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    stem = Path(path).stem.lower()

    banks = {}
    if textures:
        try:
            banks = {b.name: b for b in _scene.read_textures(path)}
        except (ValueError, struct.error):
            banks = {}

    doc = Scene(name=Path(path).stem)
    slot: dict[str, int] = {}
    for obj in m.objects:
        tex = None
        bank = banks.get(obj.name)
        if bank is not None:
            safe = "".join(c if c.isalnum() else "_" for c in obj.name.lower())
            img = out / f"{stem}_{safe}.png"
            if not img.exists():
                png.write(img, _scene.SURFACE, _scene.SURFACE, bank.surface_rgb())
            tex = img.name
        if obj.name not in slot:
            slot[obj.name] = len(doc.materials)
            doc.materials.append((obj.name, tex))

        positions, uvs = [], []
        for i, face in enumerate(obj.faces):
            for k, vi in enumerate(face):
                positions.append(tuple(float(c) for c in m.vertices[vi]))
                n = i * 3 + k
                uvs.append(obj.uvs[n] if n < len(obj.uvs) else (0.0, 0.0))
        doc.primitives.append(Primitive(obj.name, positions, uvs, slot[obj.name]))

    target = write(out / f"{stem}.gltf", doc)
    return target, {
        "source": source,
        "objects": len(m.objects),
        "vertices": len(m.vertices),
        "faces": m.face_count,
        "textures": len([1 for _, t in doc.materials if t]),
        "clean": m.mapping_is_clean,
    }


def from_model(
    path, out_dir: str | Path, textures: bool = True, preview: str | Path | None = None
) -> tuple[Path, dict]:
    """Export one ``.DAN`` character or prop model as glTF. ``(path, stats)``.

    Writes **one primitive and one 256x256 page per face-block name**: the
    name is the texture selector, so a model with `XH_IMG_A` and `XH_IMG_B`
    gets two materials over one shared mesh, not two copies of a model.

    UVs address the page correctly: a UV reference is exact, and the two words
    it points at are texels in 16.16, so ``value / 65536 / 256`` normalises
    them. Reading five bytes early - a rule that belongs to ``.DSN``, where it
    is that format's relocation delta and not a field offset - put every
    corner inside the pool while pointing at the wrong bytes, which is how a
    decode that failed completely still passed a bounds check.
    """
    from dreams import png
    from dreams.formats import node as _node

    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    stem = Path(path).stem.lower().rstrip("_") or "model"

    suffix = Path(path).suffix.lower()
    model = _node.read_3dc(path) if suffix == ".3dc" else _node.read_model(path)
    banks = _node.texture_pages(path) if (textures and suffix != ".3dc") else []

    # One material per face-block name, because the name is the texture page.
    groups = sorted({f.group for f in model.faces})
    slot = _node.page_for_group(groups)

    doc = Scene(name=stem)
    for n, name in enumerate(groups):
        tex = None
        bank = banks[slot[name]] if slot[name] < len(banks) else None
        if bank is not None:
            palette, page = bank
            img = out / (f"{stem}_tex.png" if len(groups) < 2 else f"{stem}_tex{n}.png")
            if not img.exists():
                png.write(
                    img, _node.TEX_SIZE, _node.TEX_SIZE, b"".join(bytes(palette[b]) for b in page)
                )
            tex = img.name
        positions, uvs = [], []
        for face in model.faces:
            if face.group != name:
                continue
            positions += [tuple(float(c) for c in corner) for corner in face.corners]
            uvs += list(face.uvs)
        doc.materials.append((name or stem, tex))
        doc.primitives.append(Primitive(name or stem, positions, uvs, n))
    target = write(out / f"{stem}.gltf", doc)
    if preview is not None:
        from dreams import preview as _preview

        Path(preview).parent.mkdir(parents=True, exist_ok=True)
        _preview.render_model(model, banks, preview)
    return target, {
        "parts": len(model.nodes),
        "drawn": len(model.nodes) - len(model.empty),
        "proxies": len(model.empty),
        "faces": len(model.faces),
        "bridges": model.bridge_count,
        "texture": len([1 for _, t in doc.materials if t]),
        "groups": len(groups),
    }
