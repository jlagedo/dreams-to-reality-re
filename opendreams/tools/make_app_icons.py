"""Export the checked-in app artwork for Windows, macOS, Linux, and SDL.

Run from the repository root with:
    uv run --with pillow python opendreams/tools/make_app_icons.py
"""

from pathlib import Path

from PIL import Image


ICONS = Path(__file__).resolve().parents[1] / "assets" / "icons"
NAMES = ("ODViewer", "ODRuntime")


def export(name: str) -> None:
    with Image.open(ICONS / f"{name}.png") as original:
        source = original.convert("RGBA")
    if source.width != source.height or source.width < 1024:
        raise ValueError(f"{name}.png must be square and at least 1024 pixels")

    master = source.resize((1024, 1024), Image.Resampling.LANCZOS)
    master.save(ICONS / f"{name}.icns", format="ICNS")
    master.save(
        ICONS / f"{name}.ico",
        format="ICO",
        sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)],
    )
    master.resize((512, 512), Image.Resampling.LANCZOS).save(
        ICONS / f"{name}-512.png"
    )
    (ICONS / f"{name}-window.rgba").write_bytes(
        master.resize((64, 64), Image.Resampling.LANCZOS).tobytes()
    )


if __name__ == "__main__":
    for icon_name in NAMES:
        export(icon_name)
