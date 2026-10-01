"""Compare two WD_VM_LOG files from two builds of the recompiled game.

Build the game once per virtual-memory implementation (`build.py --vm win32`,
`build.py --vm ledger`), drive each with the same `run.py --vm-log --keys ...`
script, and compare the two logs. The virtual-memory decisions (which calls,
which addresses, which results) should be identical even though the game's own
memory contents differ with timing. Calls made from several threads can
interleave differently between runs, so a difference in order near thread
starts is noise, while a different address or result is not.

A log is the call lines (one per virtual-memory call), then, if the process
exited normally, a trailer made of sections that start with `--- `. The
`--- state at exit` section lists the tracked regions (`#N base+bytes ...`),
the committed/reserved/free runs (`base+bytes state`) and a summary line.
A run stopped by `run.py --seconds` is killed and writes no trailer.

usage: uv run python recomp/windream/verify/vm_compare.py A.log B.log
Exit code 0 when the calls and the available trailers match, 1 otherwise.
"""

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

CONTEXT = 3
CAP = 40
EXIT_SECTION = "state at exit"


def split_log(lines: list[str]) -> tuple[list[str], dict[str, list[str]]]:
    """Split a log into its call lines and its trailer sections (name -> lines)."""
    calls: list[str] = []
    sections: dict[str, list[str]] = {}
    current: list[str] | None = None
    for raw in lines:
        line = raw.rstrip()
        if not line:
            continue
        if line.startswith("--- "):
            current = sections.setdefault(line[4:].strip(), [])
        elif current is None:
            calls.append(line)
        else:
            current.append(line)
    return calls, sections


def kind_of(line: str) -> str:
    """Classify a trailer line: a tracked region, a memory run or something else."""
    text = line.strip()
    if text.startswith("#"):
        return "region"
    head = text.split(" ", 1)[0]
    return "run" if "+" in head else "other"


@dataclass
class Difference:
    """One category of trailer lines compared as a set and as a sequence."""

    kind: str
    only_a: list[str] = field(default_factory=list)
    only_b: list[str] = field(default_factory=list)
    order_differs: bool = False

    @property
    def same(self) -> bool:
        return not (self.only_a or self.only_b or self.order_differs)


@dataclass
class Report:
    """The result of comparing two logs."""

    calls_a: int
    calls_b: int
    first_diff: int | None = None
    context_a: list[tuple[int, str]] = field(default_factory=list)
    context_b: list[tuple[int, str]] = field(default_factory=list)
    differing: int = 0
    has_trailer_a: bool = False
    has_trailer_b: bool = False
    trailer: list[Difference] = field(default_factory=list)

    @property
    def calls_match(self) -> bool:
        return self.differing == 0

    @property
    def trailer_compared(self) -> bool:
        return self.has_trailer_a and self.has_trailer_b

    @property
    def trailer_match(self) -> bool:
        return all(d.same for d in self.trailer)

    @property
    def ok(self) -> bool:
        return self.calls_match and self.trailer_match

    def render(self, name_a: str = "A", name_b: str = "B") -> str:
        out: list[str] = []
        if self.calls_match:
            out.append(f"calls: identical ({self.calls_a} calls)")
        else:
            out.append(
                f"calls: {name_a} has {self.calls_a}, {name_b} has {self.calls_b}; "
                f"first difference at call {self.first_diff}; "
                f"{self.differing} differing positions (pairwise plus the length difference)"
            )
            for name, context in ((name_a, self.context_a), (name_b, self.context_b)):
                out.append(f"  {name}:")
                out.extend(
                    f"  {'>' if i == self.first_diff else ' '} {i:6d}  {line}"
                    for i, line in context
                )
        if self.trailer_compared:
            out.extend(self._render_trailer(name_a, name_b))
        elif self.has_trailer_a or self.has_trailer_b:
            have, lack = (name_a, name_b) if self.has_trailer_a else (name_b, name_a)
            out.append(
                f"trailer: only {have} has the exit state; {lack} has none because that run "
                "was stopped (killed by --seconds) rather than exiting normally"
            )
        else:
            out.append(
                "trailer: neither log has the exit state (both runs were stopped, "
                "or the game did not exit)"
            )
        out.append("result: " + ("match" if self.ok else "DIFFERENT"))
        return "\n".join(out)

    def _render_trailer(self, name_a: str, name_b: str) -> list[str]:
        if self.trailer_match:
            return ["trailer: exit state identical"]
        out = ["trailer: exit state differs"]
        for diff in self.trailer:
            if diff.same:
                continue
            out.append(f"  {diff.kind} lines:")
            for name, lines in ((name_a, diff.only_a), (name_b, diff.only_b)):
                out.append(f"    only in {name}: {len(lines)}")
                out.extend(f"      {line}" for line in lines[:CAP])
                if len(lines) > CAP:
                    out.append(f"      ... {len(lines) - CAP} more")
            if not diff.only_a and not diff.only_b:
                out.append("    the same lines in a different order")
        return out


def compare_trailers(a: list[str], b: list[str]) -> list[Difference]:
    """Compare two exit-state sections per line kind, as sets and as sequences."""
    result = []
    for kind in ("region", "run", "other"):
        seq_a = [line for line in a if kind_of(line) == kind]
        seq_b = [line for line in b if kind_of(line) == kind]
        set_a, set_b = set(seq_a), set(seq_b)
        result.append(
            Difference(
                kind,
                only_a=[line for line in seq_a if line not in set_b],
                only_b=[line for line in seq_b if line not in set_a],
                order_differs=set_a == set_b and seq_a != seq_b,
            )
        )
    return result


def compare(a_lines: list[str], b_lines: list[str]) -> Report:
    """Compare two logs given as lists of lines."""
    calls_a, sections_a = split_log(a_lines)
    calls_b, sections_b = split_log(b_lines)
    report = Report(len(calls_a), len(calls_b))
    shared = min(len(calls_a), len(calls_b))
    positions = [i for i in range(shared) if calls_a[i] != calls_b[i]]
    report.differing = len(positions) + abs(len(calls_a) - len(calls_b))
    if report.differing:
        first = positions[0] if positions else shared
        report.first_diff = first
        lo = max(0, first - CONTEXT)
        report.context_a = [
            (i, calls_a[i]) for i in range(lo, min(len(calls_a), first + CONTEXT + 1))
        ]
        report.context_b = [
            (i, calls_b[i]) for i in range(lo, min(len(calls_b), first + CONTEXT + 1))
        ]
    report.has_trailer_a = EXIT_SECTION in sections_a
    report.has_trailer_b = EXIT_SECTION in sections_b
    if report.trailer_compared:
        report.trailer = compare_trailers(sections_a[EXIT_SECTION], sections_b[EXIT_SECTION])
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a", type=Path, help="first WD_VM_LOG file")
    ap.add_argument("b", type=Path, help="second WD_VM_LOG file")
    args = ap.parse_args()
    for path in (args.a, args.b):
        if not path.is_file():
            ap.error(f"{path} does not exist")
    read = lambda p: p.read_text(encoding="utf-8", errors="replace").splitlines()  # noqa: E731
    report = compare(read(args.a), read(args.b))
    print(report.render(str(args.a), str(args.b)))
    return 0 if report.ok else 1


if __name__ == "__main__":
    sys.exit(main())
