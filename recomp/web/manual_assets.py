"""Extract tribute artwork from the 33-page Spanish retail manual.

uv run --with pypdf --with pillow python recomp/web/manual_assets.py MANUAL.pdf

The original scan and extracted artwork stay under out/; never commit them.
No artwork is redrawn or generated. Crops only remove margins or adjacent text.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "out" / "recomp" / "web" / "manual"

# PDF page numbers, then a crop in fractions of the original image dimensions.
ART = {
    "cover": (1, None),
    "young-duncan": (6, (0.17, 0.06, 0.49, 0.94)),
    "duncan": (8, (0.14, 0.025, 0.52, 0.95)),
    "forest": (9, None),
    "flight": (14, None),
    "chamber": (15, None),
    "stone-heads": (17, None),
    "priests": (28, None),
    "team": (29, (0.14, 0.025, 0.86, 0.92)),
    "credits-29": (30, None),
    "credits-30": (31, None),
    "cast": (32, None),
}


def main() -> int:
    from PIL import Image
    from pypdf import PdfReader

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pdf", type=Path)
    ap.add_argument("--out", type=Path, default=OUT)
    args = ap.parse_args()
    reader = PdfReader(args.pdf)
    if len(reader.pages) != 33:
        ap.error("Expected the 33-page Spanish scan; crops are specific to that edition.")
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    for name, (number, crop) in ART.items():
        page = reader.pages[number - 1]
        if len(page.images) != 1:
            ap.error(f"PDF page {number} must contain one scanned page image.")
        im = page.images[0].image.convert("RGB")
        if crop:
            w, h = im.size
            im = im.crop(tuple(round(v * (w if i % 2 == 0 else h)) for i, v in enumerate(crop)))
        im.thumbnail((2000, 1600), Image.Resampling.LANCZOS)
        im.save(args.out / f"{name}.jpg", quality=90, optimize=True)
        records.append(
            {
                "file": f"{name}.jpg",
                "pdf_page": number,
                "printed_page": number - 1,
                "crop": crop,
                "width": im.width,
                "height": im.height,
            }
        )
    shutil.copy2(args.pdf, args.out / "original-manual-es.pdf")
    manifest = {
        "edition": "Spanish PC manual",
        "source_file": args.pdf.name,
        "sha256": hashlib.sha256(args.pdf.read_bytes()).hexdigest(),
        "assets": records,
    }
    (args.out / "sources.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Extracted {len(records)} images and copied the source manual to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
