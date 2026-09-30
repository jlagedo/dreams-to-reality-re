"""Reload accounting must observe actual save reads and resumed scenes."""

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "render_thumbnail_smoke", ROOT / "recomp/windream/debug/render_thumbnail_smoke.py"
)
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def cycle(first_scene, resources=41):
    return (
        f'[save] open R "game6.dat" -> sandbox/game6.dat\n'
        f"[direct] scene={first_scene} capture=1ms resources={resources}\n"
        f"[direct] scene={first_scene + 25} capture=1ms resources={resources}\n"
    )


def test_ten_reloads_require_ten_reads_and_scene_progress():
    result = smoke.reload_evidence("".join(cycle(50 + i * 50) for i in range(10)), "game6.dat", 10)
    assert result["completed_reloads"] == 10
    assert result["resource_baseline"] == 41


def test_save_events_survive_general_log_budget_and_are_not_double_counted():
    general = '[files] open R "asset.dan" -> source/asset.dan\n' * 200
    general += '[files] open R "game6.dat" -> sandbox/game6.dat\n'
    observed = "".join(cycle(50 + i * 50) for i in range(10))
    assert smoke.reload_evidence(general + observed, "game6.dat", 10)["completed_reloads"] == 10


def test_timeout_and_one_load_do_not_count_as_ten():
    with pytest.raises(AssertionError, match="actual save reads"):
        smoke.reload_evidence(cycle(50), "game6.dat", 10)


def test_read_without_resumed_scene_fails():
    with pytest.raises(AssertionError, match="did not resume"):
        smoke.reload_evidence(
            cycle(50) + '[save] open R "game6.dat" -> sandbox/game6.dat\n', "game6.dat", 2
        )


def test_settled_resource_growth_fails():
    with pytest.raises(AssertionError, match="resources grew"):
        smoke.reload_evidence(cycle(50) + cycle(100, 42), "game6.dat", 2)


def test_installation_save_read_does_not_count():
    with pytest.raises(AssertionError, match="actual save reads"):
        smoke.reload_evidence(cycle(50).replace("sandbox", "installation"), "game6.dat", 1)


def test_failed_save_read_does_not_count():
    with pytest.raises(AssertionError, match="actual save reads"):
        smoke.reload_evidence(
            cycle(50).replace("sandbox/game6.dat", "sandbox/game6.dat (FAILED)"), "game6.dat", 1
        )


def test_repeated_loads_are_state_driven_not_blind_timed_keys():
    keys, duration = smoke.reload_schedule(10)
    events = [(int(event.split(":")[0]), event.split(":")[1]) for event in keys.split(",")]
    assert [time for time, _ in events] == sorted(time for time, _ in events)
    assert duration == 260
    assert len(events) == 4
    assert events[-1] == (20000, "ESC")
    assert smoke.reload_schedule(0)[1] == 43
    assert smoke.reload_schedule(1)[1] == 80


def test_cleared_caption_flag_requires_new_gameplay_frames():
    samples = [("150", "79"), ("175", "41"), ("200", "41")]
    assert not smoke.resumed_reload(samples, 0, 200)
    samples.append(("225", "41"))
    assert smoke.resumed_reload(samples, 0, 200)
    assert not smoke.resumed_reload(samples, 2, 200)


def test_caption_scope_tracks_callback_lifetime_and_nesting():
    text = "[direct] caption_scope=begin depth=1\n[direct] caption_scope=begin depth=2\n"
    assert smoke.caption_depth(text) == 2
    text += "[direct] caption_scope=end depth=1\n"
    assert smoke.caption_depth(text) == 1
    assert smoke.caption_depth(text + "[direct] caption_scope=end depth=0\n") == 0


def test_caption_interrupt_is_not_mistaken_for_stale_load_page():
    assert smoke.load_page_ready(0x40E75C, 2, 1, 0)
    assert not smoke.load_page_ready(0x40E75C, 2, 1, 1)
    assert not smoke.load_page_ready(0x416D45, 2, 1, 0)


def test_cancel_each_caption_lifetime_once_and_handle_nested_return():
    assert smoke.caption_needs_cancel(1, (3, 1), None)
    assert not smoke.caption_needs_cancel(1, (3, 1), (3, 1))
    assert smoke.caption_needs_cancel(2, (4, 2), (3, 1))
    assert smoke.caption_needs_cancel(1, (4, 1), (4, 2))
    assert not smoke.caption_needs_cancel(0, (4, 0), (4, 1))


def test_return_does_not_mean_save_started_when_dialogue_intervenes():
    assert smoke.caption_interrupts_navigation("loading", False)
    assert not smoke.caption_interrupts_navigation("loading", True)
    assert smoke.caption_interrupts_navigation("slots", False)
    log = "".join(cycle(100 + i * 75) for i in range(4))
    # Fifth Return races a newly triggered dialogue; old scene submissions continue.
    log += "[direct] scene=400 capture=1ms resources=41\n"
    log += "[direct] caption_scope=begin depth=1\n"
    assert smoke.reload_evidence(log, "game6.dat", 4)["completed_reloads"] == 4
    with pytest.raises(AssertionError, match="expected 5 actual save reads, found 4"):
        smoke.reload_evidence(log, "game6.dat", 5)
