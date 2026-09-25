"""Image assets: ``UBIK`` bundles (``.BF``), sprites (``.SPR``) and ``.ALP`` maps.

Indexed sprites, menu sprites, and bitmap-font glyph pixels are decoded.
`HI320`'s percent-code descriptor is malformed and is preserved but skipped
by the renderer. **[verified]**

``ICONES.BF`` — UBIK bundle
    char[4]  "UBIK"
    u32      2                    container version
    u32      table offset         == filesize - 267*count
    u32      count
    ---- 0x10 ----                payload, entries back to back
    ---- table ----               count x 267-byte records:
        char[259] filename        NUL-padded
        u32       absolute offset
        u32       length

The payload chain is exact: each ``offset + length`` equals the next entry's
offset, and the last ends precisely at the table. Members are ``.SPR``
and ``.ALP`` files in the menu-sprite family below, so ``.BF`` carries no
image format of its own.

``.SPR`` has no magic and comes in **three** flavours.

*Indexed bundles* (``DATA/OBJET``) — 8-bit paletted, NOT raw RGB555:
    0x000  u8[4][256]   VGA palette, RGBX, 6-bit channels (<= 0x3F)
    0x400  u32[256]     pointer table, offsets relative to 0x400
    record u32 width, u32 height, u32 ?, u32 ?, then width*height indices
           record size = round4(16 + width*height)

*Font files* (``DATA/FONT`` — ``HI320``/``HI480``/``HI640``):
    0x00   u16[256]     RGB555 palette
    0x200  ...          one-byte palette-index glyph bitmaps
    tail   256 x 28 B   glyph descriptors (width @+4, height @+8, pixels @+0x18)
    end    u32 0x100    glyph count

The engine renders each character code through its descriptor and advances by
``width - s32(descriptor[+0x0c])`` (space is assigned the advance of ``'0'``).
See :func:`read_font_sheet`.

*Menu sprite bundles* (the ``ICONES.BF`` members) — see
:func:`read_menu_sheet` below: a 512-byte RGB555 palette, pixel blobs, a
``"TABLE"`` marker, and 28-byte descriptors. One or two bytes per pixel,
per file. The two-byte texels are a palette index and an opacity/blend value,
not a packed RGB color.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

UBIK_MAGIC = b"UBIK"
RECORD_SIZE = 267
NAME_SIZE = 259

# Pointer-table slots that are not payload records. Their meaning is unverified;
# they are consistent across all five OBJET sprite files.
SENTINELS = {0, 0x100, 0x5000}


@dataclass
class Entry:
    name: str
    offset: int
    size: int


@dataclass
class Bundle:
    path: Path
    version: int
    table_offset: int
    entry_count: int
    entries: list[Entry] = field(default_factory=list)

    @property
    def chain_is_exact(self) -> bool:
        """True if every payload abuts the next and the last ends at the table."""
        pos = 16
        for e in self.entries:
            if e.offset != pos:
                return False
            pos += e.size
        return pos == self.table_offset


def read_bundle(path: str | Path) -> Bundle:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(UBIK_MAGIC):
        raise ValueError(f"{p.name}: not a UBIK bundle")

    version, table_offset, count = struct.unpack_from("<III", data, 4)
    bundle = Bundle(p, version, table_offset, count)

    for i in range(count):
        base = table_offset + i * RECORD_SIZE
        if base + RECORD_SIZE > len(data):
            break
        name = data[base : base + NAME_SIZE].split(b"\x00")[0].decode("latin-1")
        offset, size = struct.unpack_from("<II", data, base + NAME_SIZE)
        bundle.entries.append(Entry(name, offset, size))
    return bundle


def extract_bundle(bundle: Bundle, out_dir: str | Path) -> list[Path]:
    """Write every member of a UBIK bundle as a standalone file."""
    data = bundle.path.read_bytes()
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    written = []
    for e in bundle.entries:
        blob = data[e.offset : e.offset + e.size]
        if len(blob) != e.size:
            continue
        target = out / e.name
        target.write_bytes(blob)
        written.append(target)
    return written


# ------------------------------------------------- menu sprite bundles ---
#
# The ``ICONES.BF`` members are a third sprite family, shared by ``.ALP`` and
# the menu ``.SPR`` files. Loader ``FUN_00426c46`` in WINDREAM.EXE:
# read 512-byte palette, convert 5:5:5 -> 5:6:5 when the display is 16-bit,
# then per descriptor (width @+4, height @+8, pixel offset @+0x18) read the
# pixel blob. Sprite names are NOT in the files; the engine resolves them
# through a 72-entry table at 0x49db12 -> (bank, slot) at 0x49dd9a.
#
#     0x000  u16[256]    palette, RGB555
#     0x200  ...         sprite pixel blobs at absolute offsets
#     ...    "TABLE"     marker (5 bytes)
#     +5     N x 28 B    descriptors: +0 palette ptr (0 in file), +4 width,
#                        +8 height, +0xC/+0x10/+0x14 flags, +0x18 pixel offset
#     end-8  u32 0, u32 N   footer (approximate; record area is zero-padded)
#
# Pixel layout is per file, recovered from the offset stride: 1 byte/pixel is
# an index into the palette (TOUCHES.SPR, TITRES.SPR); 2 bytes/pixel are
# (palette index, opacity/blend value) (MAGIE/ANIM/PYRAM/INTERF .ALP and
# SOUR.ALP). The retail blitter looks up the first byte in the palette and
# uses the second as a blend amount. The engine always reads w*h*2 bytes,
# which over-reads the 8-bit files harmlessly.

# In the PYRAM two-byte compositor, these coverage-byte values reference other
# live sprite layers instead of expressing opacity. The PNG preview colors the
# references visibly so they cannot be mistaken for opaque pixels.
PYRAM_LAYER_MARKERS: dict[int, tuple[int, int, int]] = {
    0xFD: (255, 255, 0),  # layer 5 / state-dependent fill
    0xFE: (0, 255, 255),  # layer 4
    0xFF: (255, 0, 255),  # layer 3
}


@dataclass
class MenuSprite:
    index: int
    width: int
    height: int
    offset: int

    @property
    def name(self) -> str | None:
        return MENU_SPRITE_NAMES.get((self.bank_name, self.index))

    bank_name: str = ""


@dataclass
class MenuSheet:
    path: Path
    palette: list[int]
    table_offset: int
    bytes_per_pixel: int
    sprites: list[MenuSprite] = field(default_factory=list)


# The engine's name table (0x49db12 in WINDREAM.EXE), bank order from the
# filename table at 0x49dacc. [verified] by dumping both tables.
MENU_BANKS = ["magie", "anim", "pyram", "touches", "interf"]
MENU_SPRITE_NAMES: dict[tuple[str, int], str] = {
    ("magie", 0): "feu",
    ("magie", 1): "shaman",
    ("magie", 2): "cleeau",
    ("magie", 3): "canard",
    ("magie", 4): "disque",
    ("magie", 5): "epee",
    ("magie", 6): "connaiss",
    ("magie", 7): "holo",
    ("magie", 8): "invivib",
    ("magie", 9): "lettre0",
    ("magie", 10): "lettre1",
    ("magie", 11): "lettre2",
    ("magie", 13): "tournd",
    ("magie", 14): "spirit",
    ("magie", 15): "mine",
    ("magie", 16): "soufnot1",
    ("magie", 17): "soufnot2",
    ("magie", 18): "omega",
    ("magie", 19): "moulin",
    ("magie", 20): "masque",
    ("magie", 21): "temps",
    ("magie", 22): "resurec",
    ("magie", 23): "guerison",
    ("magie", 24): "vitesse",
    ("magie", 25): "sucette",
    ("magie", 26): "piece",
    ("magie", 27): "arc",
    ("magie", 28): "bouclier",
    ("magie", 29): "parfum",
    ("magie", 30): "surfplan",
    ("magie", 31): "infosne",
    ("magie", 32): "mcombat",
    ("magie", 33): "infosde",
    ("anim", 0): "nothing",
    ("pyram", 0): "pyrambo",
    ("pyram", 1): "pyrcurs",
    ("pyram", 2): "pyramvi",
    ("pyram", 3): "pyramma",
    ("pyram", 4): "pyramox",
    ("pyram", 5): "exprbor",
    ("pyram", 6): "exprlev",
    ("pyram", 7): "replay",
    ("pyram", 8): "record",
    ("pyram", 9): "pyrafvi",
    ("pyram", 10): "pyrafma",
    ("touches", 0): "joy_up",
    ("touches", 1): "joy_dn",
    ("touches", 2): "joy_lf",
    ("touches", 3): "joy_rt",
    ("touches", 4): "joy_k1",
    ("touches", 5): "joy_k2",
    ("touches", 6): "joy_k3",
    ("touches", 7): "joy_sel",
    ("touches", 8): "joy_swi",
    ("touches", 9): "joy_bt0",
    ("touches", 10): "joy_bt1",
    ("interf", 0): "DnRg",
    ("interf", 1): "DnLf",
    ("interf", 2): "UpLf",
    ("interf", 3): "UpRg",
    ("interf", 4): "DnRgNA",
    ("interf", 5): "DnLfNA",
    ("interf", 6): "UpLfNA",
    ("interf", 7): "UpRgNA",
    ("interf", 8): "RubLf",
    ("interf", 9): "RubRg",
    ("interf", 10): "Desc1",
    ("interf", 11): "Desc2",
    ("interf", 12): "Desc3",
    ("interf", 13): "Desc4",
}


def _rgb555(value: int) -> tuple[int, int, int]:
    r, g, b = value >> 10 & 0x1F, value >> 5 & 0x1F, value & 0x1F
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def read_menu_sheet(path: str | Path, bank: str = "") -> MenuSheet:
    """Parse an ``ICONES.BF`` member (.ALP or menu .SPR) with a TABLE section.

    ``bank`` overrides the filename stem used for sprite names and the PYRAM
    layer-marker preview.
    """
    p = Path(path)
    data = p.read_bytes()
    name_bank = (bank or p.stem).lower()
    table = data.find(b"TABLE")
    if table < 0x200:
        raise ValueError(f"{p.name}: no TABLE section - not a menu sprite bundle")

    palette = list(struct.unpack_from("<256H", data, 0))

    # Records run from the marker to the zero padding; stop at the first
    # record whose pixel offset does not point inside the data area.
    sprites: list[MenuSprite] = []
    off = table + 5
    while off + 28 <= len(data):
        width, height = struct.unpack_from("<II", data, off + 4)
        pix = struct.unpack_from("<I", data, off + 0x18)[0]
        if not (0 < width <= 4096 and 0 < height <= 4096 and 0x200 <= pix < table):
            break
        sprites.append(MenuSprite(len(sprites), width, height, pix, bank_name=name_bank))
        off += 28

    if not sprites:
        raise ValueError(f"{p.name}: no sprite descriptors after TABLE")

    first = sprites[0]
    stride = sprites[1].offset - first.offset if len(sprites) > 1 else table - first.offset
    if stride == first.width * first.height:
        bpp = 1
    elif stride == first.width * first.height * 2:
        bpp = 2
    else:
        raise ValueError(f"{p.name}: sprite stride {stride} matches neither 1 nor 2 bytes/pixel")

    last = sprites[-1]
    if last.offset + last.width * last.height * bpp > table:
        raise ValueError(f"{p.name}: last sprite runs past the TABLE marker")

    return MenuSheet(p, palette, table, bpp, sprites)


def menu_sprite_rgba(sheet: MenuSheet, sprite: MenuSprite) -> bytes:
    """Expand one menu sprite to tightly packed RGBA bytes.

    One-byte texels are palette indices; index 0 is transparent in the retail
    indexed blit path. Two-byte texels are ``(palette index, blend value)``.
    The renderer uses the index to fetch RGB555 from the palette and feeds the
    second byte through its blend path. Index 0 is transparent in both paths.
    The menu wrapper sets source flag 0x10. In that renderer path, values 0
    are skipped, values >=63 are opaque, and values 1..62 blend with weight
    ``value >> 1``. The returned alpha maps that weight to an 8-bit preview.
    PNG straight alpha cannot reproduce the game's exact weighted sum, whose
    coefficients total 31 before shifting by 5. In `PYRAM`, coverage bytes
    ``0xfd``/``0xfe``/``0xff`` are layer-reference commands for a dynamic
    compositor; the preview marks them in color rather than treating them as
    ordinary alpha.
    """
    data = sheet.path.read_bytes()
    w, h = sprite.width, sprite.height
    out = bytearray(w * h * 4)
    for i in range(w * h):
        if sheet.bytes_per_pixel == 1:
            index = data[sprite.offset + i]
            alpha = 0 if index == 0 else 255
        else:
            index = data[sprite.offset + i * 2]
            blend = data[sprite.offset + i * 2 + 1]
            if index != 0 and sprite.bank_name == "pyram" and blend in PYRAM_LAYER_MARKERS:
                r, g, b = PYRAM_LAYER_MARKERS[blend]
                out[i * 4 : i * 4 + 4] = bytes((r, g, b, 255))
                continue
            weight = blend >> 1
            alpha = (
                0
                if index == 0
                else 255
                if blend >= 63
                else (weight * 255 + 16) // 32
            )
        r, g, b = _rgb555(sheet.palette[index])
        out[i * 4 : i * 4 + 4] = bytes((r, g, b, alpha))
    return bytes(out)


# ----------------------------------------------------------------- sprites ---


@dataclass
class Sprite:
    index: int
    offset: int
    width: int
    height: int
    unknown_a: int
    unknown_b: int

    @property
    def record_size(self) -> int:
        raw = 16 + self.width * self.height
        return raw + (-raw % 4)  # round up to 4


@dataclass
class SpriteSheet:
    path: Path
    palette: list[tuple[int, int, int]]
    sprites: list[Sprite] = field(default_factory=list)


FONT_GLYPH_COUNT = 256
FONT_GLYPH_RECORD_SIZE = 28
FONT_GLYPH_TABLE_SIZE = FONT_GLYPH_COUNT * FONT_GLYPH_RECORD_SIZE


@dataclass
class FontGlyph:
    codepoint: int
    offset: int
    width: int
    height: int
    field_0c: int
    field_10: int
    field_14: int
    renderable: bool

    @property
    def base_advance(self) -> int:
        """Descriptor advance: width minus signed field +0x0c."""
        adjustment = self.field_0c if self.field_0c < 0x80000000 else self.field_0c - 0x100000000
        return self.width - adjustment


@dataclass
class FontSheet:
    path: Path
    palette: list[int]
    table_offset: int
    glyphs: list[FontGlyph] = field(default_factory=list)
    glyph_count: int = FONT_GLYPH_COUNT

    def advance(self, codepoint: int) -> int:
        """Return the retail text cursor advance for one byte character code."""
        if not 0 <= codepoint < len(self.glyphs):
            raise ValueError(f"font codepoint out of range: {codepoint}")
        # FUN_00425c61 fills the space slot from the glyph record for '0'.
        glyph = self.glyphs[0x30 if codepoint == 0x20 else codepoint]
        return glyph.base_advance


def read_font_sheet(path: str | Path) -> FontSheet:
    """Decode a ``DATA/FONT/HI*.SPR`` 256-glyph indexed bitmap font."""
    p = Path(path)
    data = p.read_bytes()
    min_size = 0x200 + FONT_GLYPH_TABLE_SIZE + 4
    if len(data) < min_size:
        raise ValueError(f"{p.name}: too short for a HI font")

    glyph_count = struct.unpack_from("<I", data, len(data) - 4)[0]
    if glyph_count != FONT_GLYPH_COUNT:
        raise ValueError(f"{p.name}: expected 256 font glyphs, found {glyph_count}")
    table = len(data) - 4 - FONT_GLYPH_TABLE_SIZE
    if table < 0x200:
        raise ValueError(f"{p.name}: glyph table overlaps palette")

    palette = list(struct.unpack_from("<256H", data, 0))
    glyphs = []
    for codepoint in range(FONT_GLYPH_COUNT):
        base = table + codepoint * FONT_GLYPH_RECORD_SIZE
        _palette_ptr, width, height, field_0c, field_10, field_14, offset = (
            struct.unpack_from("<7I", data, base)
        )
        renderable = (
            0 < width <= 256
            and 0 < height <= 256
            and offset >= 0x200
            and offset + width * height <= table
        )
        glyphs.append(
            FontGlyph(
                codepoint,
                offset,
                width,
                height,
                field_0c,
                field_10,
                field_14,
                renderable,
            )
        )
    return FontSheet(p, palette, table, glyphs, glyph_count)


def font_glyph_rgba(
    sheet: FontSheet, glyph: FontGlyph, transparent_index: int = 0
) -> bytes:
    """Expand one indexed font glyph to tightly packed RGBA bytes."""
    if not glyph.renderable:
        raise ValueError(f"{sheet.path.name}: glyph {glyph.codepoint:#04x} is malformed")
    data = sheet.path.read_bytes()
    pixels = data[glyph.offset : glyph.offset + glyph.width * glyph.height]
    out = bytearray(glyph.width * glyph.height * 4)
    for i, index in enumerate(pixels):
        r, g, b = _rgb555(sheet.palette[index])
        alpha = 0 if index == transparent_index else 255
        out[i * 4 : i * 4 + 4] = bytes((r, g, b, alpha))
    return bytes(out)


def looks_like_vga_palette(data: bytes, entries: int = 256) -> bool:
    """True if the buffer opens with 4-byte RGBX records whose channels are 6-bit."""
    if len(data) < entries * 4:
        return False
    return all(
        data[i * 4] <= 0x3F and data[i * 4 + 1] <= 0x3F and data[i * 4 + 2] <= 0x3F
        for i in range(entries)
    )


def read_spritesheet(path: str | Path) -> SpriteSheet:
    """Parse an indexed ``.SPR`` bundle. Raises if it is not the indexed flavour."""
    from dreams.png import vga6_to_rgb

    p = Path(path)
    data = p.read_bytes()
    if not looks_like_vga_palette(data):
        raise ValueError(f"{p.name}: no 6-bit VGA palette - not an indexed .SPR")

    palette = [vga6_to_rgb(data[i * 4 : i * 4 + 4]) for i in range(256)]
    sheet = SpriteSheet(p, palette)

    pointers = struct.unpack_from("<256I", data, 0x400)
    seen: set[int] = set()
    for i, rel in enumerate(pointers):
        if rel in SENTINELS:
            continue
        off = 0x400 + rel
        if off in seen or off + 16 > len(data):
            continue
        width, height, ua, ub = struct.unpack_from("<IIII", data, off)
        if not (0 < width <= 4096 and 0 < height <= 4096):
            continue
        if off + 16 + width * height > len(data):
            continue
        seen.add(off)
        sheet.sprites.append(Sprite(i, off, width, height, ua, ub))
    return sheet


def sprite_rgba(sheet: SpriteSheet, sprite: Sprite, transparent_index: int = 0) -> bytes:
    """Expand one sprite to tightly packed RGBA bytes.

    ``transparent_index`` is **[unverified]**: index 0 is the usual convention
    for 8-bit sprite transparency and these sheets do keep index 0 dark, but the
    engine has not been checked. Pass ``-1`` to make every pixel opaque.
    """
    data = sheet.path.read_bytes()
    start = sprite.offset + 16
    pixels = data[start : start + sprite.width * sprite.height]

    out = bytearray(len(pixels) * 4)
    for i, idx in enumerate(pixels):
        r, g, b = sheet.palette[idx]
        out[i * 4] = r
        out[i * 4 + 1] = g
        out[i * 4 + 2] = b
        out[i * 4 + 3] = 0 if idx == transparent_index else 255
    return bytes(out)
