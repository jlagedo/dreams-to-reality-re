"""Collate record_opens.py recordings: which files opened, which failed.

    uv run python recomp/web/demo/collate_opens.py [out/recomp/web/demo/opens-*.json ...]

Prints every file that opened successfully in any recording (with the
recordings it appeared in) and every name that only ever failed (the game
tolerated its absence in the retail data too).
"""

from __future__ import annotations

import glob
import json
import sys

import demo_common as dc


def collate(files: list[str]):
    ok: dict[str, set[str]] = {}
    failed: dict[str, set[str]] = {}
    for f in files:
        tag = f.replace("\\", "/").rsplit("opens-", 1)[-1][:-5]
        for e in json.load(open(f))["events"]:
            if e.get("kind") != "open" or e.get("write"):
                continue
            key = e["path"].replace("/", "\\").lower()
            (ok if e["ok"] else failed).setdefault(key, set()).add(tag)
    return ok, failed


def main() -> int:
    files = sys.argv[1:] or sorted(glob.glob(str(dc.REC_OUT / "opens-demo-rec*.json")))
    ok, failed = collate(files)
    print(f"{len(files)} recordings; {len(ok)} files opened")
    for k in sorted(ok):
        who = sorted(ok[k])
        print(f"  {k:48s} {'all' if len(who) == len(files) else ' '.join(who)}")
    print("only ever failed:")
    for k in sorted(failed):
        if k not in ok:
            print(f"  {k}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
