"""Check demo-informed UV/tail layout claims against independent retail captures.

Does not import the demo object extent into the production reader or infer that
zero tail fields are absent from every retail asset.
"""

import argparse
import json
from pathlib import Path

from mdmp import Dump
from render_smoke import node_inventory

from dreams import paths


def check(file):
    dump = Dump(file, 0)
    _, nodes = node_inventory(dump)
    uv_records = set()
    nonzero_tails = []
    vertex_ends = 0
    for node in nodes:
        count, start, uv_count, uv_start = dump.dwords(node + 0x7C, 4)
        assert uv_count < 1000000
        if uv_count:
            assert uv_start
            dump.read(uv_start, uv_count * 8)
            uv_records.update(range(uv_start, uv_start + uv_count * 8, 8))
            vertex_ends += uv_start == start + count * 40
        tail = list(dump.dwords(node + 0xD4, 2))
        if any(tail):
            nonzero_tails.append({"node": hex(node), "words": tail})
    corners = 0
    for node in nodes:
        seen = set()
        block = dump.u32(node + 0xA4)
        while block:
            assert block not in seen
            seen.add(block)
            count, start = dump.dwords(block + 0x1C, 2)
            stride = dump.u32(block + 0x2C)
            assert count < 1000000
            if stride == 68:
                for face in range(start, start + count * stride, stride):
                    for address in dump.dwords(face + 0x34, 3):
                        assert address in uv_records, (hex(face), hex(address))
                        corners += 1
            block = dump.u32(block)
    dump.f.close()
    assert corners
    return {
        "dump": str(file),
        "nodes": len(nodes),
        "uv_records": len(uv_records),
        "validated_uv_corners": corners,
        "uv_arrays_at_vertex_end": vertex_ends,
        "nonzero_virtual_vertex_tails": nonzero_tails,
        "scope": "two captured retail scenes; tail inactivity is not whole-game proof",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dumps", nargs="+", type=Path)
    reports = [check(file) for file in parser.parse_args().dumps]
    output = paths.out_dir("recomp", "debug-layout") / "results.json"
    output.write_text(json.dumps(reports, indent=2) + "\n")
    print(json.dumps(reports, indent=2))


if __name__ == "__main__":
    main()
