"""tools/check_names.py: every fact kind passes when true and fails when false."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from check_names import evaluate  # noqa: E402


def feat(entry, name, **kw):
    base = {"entry": entry, "name": name, "size": 10, "consts": [], "strings": [], "calls": []}
    return base | kw


FEATS = [
    feat(
        "00401000",
        "FUN_00401000",
        size=86,
        strings=["Error: DRDF bad"],
        imports=["ReadFile"],
        calls=["00402000", "00403000"],
        consts=[0x40],
        bigconsts=[0x96000],
        refs=["005df484"],
    ),
    feat("00402000", "fprintf_"),
    feat("00403000", "fprintf_"),
    feat("00404000", "FUN_00404000", calls=["00401000"]),
]


def row(facts, name="X_Test", address="00401000"):
    return {"address": address, "name": name, "kind": "descriptive", "facts": facts}


@pytest.mark.parametrize(
    "facts",
    [
        "str:DRDF",
        "imp:ReadFile",
        "call:00402000",
        "call:fprintf_",
        "caller:00404000",
        "caller:Y_Caller",
        "const:40",
        "const:96000",
        "ref:5df484",
        "size:86",
    ],
)
def test_true_fact_passes(facts):
    rows = [row(facts), row("size:10", "Y_Caller", "00404000")]
    assert evaluate(FEATS, rows)[0][1] == []


@pytest.mark.parametrize(
    "facts",
    [
        "str:DRDX",
        "imp:WriteFile",
        "call:00404000",
        "caller:00402000",
        "const:41",
        "ref:005df488",
        "size:87",
        "bogus:1",
        "",
    ],
)
def test_false_fact_fails(facts):
    assert evaluate(FEATS, [row(facts)])[0][1]


def test_missing_function_fails():
    assert evaluate(FEATS, [row("size:1", address="00409000")])[0][1] == [
        "no function at this address"
    ]


def test_duplicate_registry_name_is_rejected():
    with pytest.raises(SystemExit):
        evaluate(FEATS, [row("size:86"), row("size:10", address="00404000")])


def test_doc_units_split_tables_lists_and_sentences():
    from sync_doc_comments import units

    text = (
        "# Title\n\n## Loader\n\n"
        "| Function | Role |\n|---|---|\n| `0x41072c` | opens DRDF |\n\n"
        "- `DRD_Open` opens it.\n- `DRD_LoadEntry` reads one.\n\n"
        "First sentence cites `0x410928`. Second one does not.\n\n"
        "```text\nFUN_00410928 in code\n```\n"
    )
    got = list(units(text))
    assert ("Loader", "| `0x41072c` | opens DRDF |") in got
    assert ("Loader", "- `DRD_Open` opens it.") in got
    assert ("Loader", "First sentence cites `0x410928`.") in got
    assert not any("in code" in u for _, u in got)
    assert not any(set(u) <= set("|-: ") for _, u in got)
