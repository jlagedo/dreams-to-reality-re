"""Export menu corner brackets and title sprites to web/public/ui/menu/."""

from __future__ import annotations

import pathlib
import tempfile

from dreams import paths, png
from dreams.formats.image import (
    _rgb555,
    extract_bundle,
    menu_sprite_rgba,
    read_bundle,
    read_menu_sheet,
)

DEST_DIR = pathlib.Path("web/public/ui/menu")


def export_sprites() -> None:
    disc2 = paths.configured("disc2")
    if not disc2:
        raise RuntimeError("disc2 not configured")
    icones_bf = disc2 / "DATA/ICONE/ICONES.BF"
    if not icones_bf.exists():
        raise RuntimeError(f"{icones_bf} does not exist")

    DEST_DIR.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp_str:
        tmp = pathlib.Path(tmp_str)
        bundle = read_bundle(icones_bf)
        extracted = extract_bundle(bundle, tmp)

        # 1. Export INTERF corner brackets (sprites 0..7)
        interf_path = next(p for p in extracted if p.name.lower() == "interf.alp")
        interf_sheet = read_menu_sheet(interf_path, bank="interf")
        for sp in interf_sheet.sprites[:8]:
            name = sp.name.lower()
            out_png = DEST_DIR / f"bracket_{name}.png"
            png.write(
                out_png,
                sp.width,
                sp.height,
                menu_sprite_rgba(interf_sheet, sp),
                alpha=True,
            )
            print(f"Wrote {out_png} ({sp.width}x{sp.height})")

        # 2. Export TITRES (12 title sprites: 4 titles x 3 states)
        # States: 0 = Normal, 1 = Active/Glow, 2 = Clicked/Alt
        # Titles: 0 = New Game, 1 = Load Game, 2 = Options, 3 = Quit
        titres_path = next(p for p in extracted if p.name.lower() == "titres.spr")
        titres_sheet = read_menu_sheet(titres_path)
        titres_data = titres_path.read_bytes()

        title_names = ["new_game", "load_game", "options", "quit"]
        state_names = ["normal", "active", "pressed"]

        for idx, sp in enumerate(titres_sheet.sprites):
            state_idx = idx // 4
            title_idx = idx % 4
            sname = state_names[state_idx]
            tname = title_names[title_idx]

            rgba = bytearray(sp.width * sp.height * 4)
            for i in range(sp.width * sp.height):
                color_idx = titres_data[sp.offset + i]
                if color_idx == 0:
                    rgba[i * 4 : i * 4 + 4] = bytes((0, 0, 0, 0))
                else:
                    r, g, b = _rgb555(titres_sheet.palette[color_idx])
                    rgba[i * 4 : i * 4 + 4] = bytes((r, g, b, 255))

            out_png = DEST_DIR / f"title_{tname}_{sname}.png"
            png.write(out_png, sp.width, sp.height, bytes(rgba), alpha=True)
            print(f"Wrote {out_png} ({sp.width}x{sp.height})")


if __name__ == "__main__":
    export_sprites()
