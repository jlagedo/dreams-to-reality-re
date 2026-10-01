"""Recheck immutable binary evidence behind the recorded blind name reviews.

The observations themselves are human-readable review evidence, not an
automatic equivalence claim. This verifies the original symbols and the
Windows-demo/retail whole-code bridge used alongside those observations.
"""

import argparse
import hashlib
import json
from pathlib import Path

from check_wip_windows import fingerprint
from relocated_image import Image
from watcom_debug import read_debug

from dreams import paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--check", type=Path, required=True)
    args = parser.parse_args()
    proof = json.loads(args.check.read_text())
    source = Image(args.demo / "DREAMS.EXE")
    beta = Image(args.demo / "DREAMWIN.EXE")
    target = Image(paths.disc(1) / "WINDREAM.EXE")
    twin = Image(paths.disc(1) / "GDIDREAM.EXE")
    for field, image in (
        ("source_sha256", source),
        ("beta_windows_sha256", beta),
        ("target_sha256", target),
        ("twin_sha256", twin),
    ):
        if hashlib.sha256(image.raw).hexdigest() != proof[field]:
            raise ValueError(f"Executable differs: {field}")
    symbols = read_debug(source.raw)[1]
    for row in proof["rows"]:
        if not any(
            s["address"] == f"{row['source_entry']:08x}"
            and s["name"] == row["name"]
            and s["module"] == row["module"]
            and s["kind"] & 4
            for s in symbols
        ):
            raise ValueError("Original symbol differs")
        a, b, size = row["beta_windows_entry"], row["entry"], row["size"]
        first, second = fingerprint(beta, a, size), fingerprint(target, b, size)
        if first is None or second is None or first[0] != second[0]:
            raise ValueError(f"Windows code bridge differs: {b:08x}")
        if hashlib.sha256(beta.read(a, size)).hexdigest() != row["beta_body_sha256"]:
            raise ValueError("Windows demo body differs")
        if hashlib.sha256(target.read(b, size)).hexdigest() != row["target_body_sha256"]:
            raise ValueError("Retail body differs")
        if target.read(b, size) != twin.read(b, size):
            raise ValueError("Windows twins differ")
        print(f"PASS {b:08x} {row['name']}")


if __name__ == "__main__":
    main()
