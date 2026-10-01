"""vm_compare must report the first differing virtual-memory call and missing trailers."""

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "vm_compare", ROOT / "recomp/windream/verify/vm_compare.py"
)
vm_compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm_compare)

CALLS = [
    "reserve 0x31000000 -> ok",
    "commit 00010000 0x1000",
    "alloc 00000000 0x1000000 type=0x3000 prot=0x4 -> 01000000",
    "query 0010FF00 -> 28 base=00100000 alloc=00100000 aprot=0x4 size=0x200000",
    "free 01000000 0x0 type=0x8000 -> 1 err=0",
]
TRAILER = [
    "--- state at exit",
    "regions 1 next 01210000",
    "  #0 01000000+1000000 prot=4 live",
    "  00000000+10000 reserved",
    "  00010000+1000 committed",
]


def test_identical_logs_have_no_differences():
    report = vm_compare.compare(CALLS + TRAILER, CALLS + TRAILER)
    assert report.ok
    assert report.differing == 0
    assert report.trailer_compared
    assert "calls: identical (5 calls)" in report.render()


def test_differing_alloc_result_is_reported_at_its_index():
    other = list(CALLS)
    other[2] = other[2].replace("-> 01000000", "-> 02000000")
    report = vm_compare.compare(CALLS + TRAILER, other + TRAILER)
    assert not report.ok
    assert report.first_diff == 2
    assert report.differing == 1
    assert [i for i, _ in report.context_a] == [0, 1, 2, 3, 4]
    assert report.context_b[2] == (2, other[2])


def test_length_difference_and_trailer_lines_only_on_one_side():
    changed = [
        line.replace("00010000+1000 committed", "00010000+2000 committed") for line in TRAILER
    ]
    report = vm_compare.compare(CALLS + TRAILER, CALLS[:4] + changed)
    assert report.first_diff == 4
    assert report.differing == 1
    run = next(d for d in report.trailer if d.kind == "run")
    assert run.only_a == ["  00010000+1000 committed"]
    assert run.only_b == ["  00010000+2000 committed"]


def test_missing_trailer_is_reported_and_calls_still_compared():
    report = vm_compare.compare(CALLS + TRAILER, CALLS)
    assert report.ok
    assert report.has_trailer_a and not report.has_trailer_b
    assert not report.trailer_compared
    assert "only A has the exit state" in report.render()
    other = CALLS[:1] + ["commit 00020000 0x1000"] + CALLS[2:]
    assert not vm_compare.compare(CALLS + TRAILER, other).ok
