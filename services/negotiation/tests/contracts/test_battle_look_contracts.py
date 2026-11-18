"""Battle look contract (M7): battle_look.json obeys its schema, every hour falls in one band,
every level has a look for every band, and an unmeasured platform runs the lowest tier.

The Unreal battle reads the same vectors in its PA250Combat cases.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
SCHEMA: dict[str, Any] = yaml.safe_load(
    (ROOT / "protocol" / "schemas" / "battle_look.yaml").read_text()
)
LOOK: dict[str, Any] = json.loads(
    (ROOT / "unreal" / "PA250Sim" / "data" / "battle_look.json").read_text()
)
VECTOR_DIR = ROOT / "protocol" / "test_vectors" / "anim"
VECTORS: list[dict[str, Any]] = [
    json.loads(p.read_text()) for p in sorted(VECTOR_DIR.glob("time_band_*.json"))
]
BANDS = ("dawn", "day", "dusk")


def band_for_hour(hour: int, bands: dict[str, list[list[int]]], default: str) -> str:
    """The band rule, in plain Python."""
    for name in BANDS:
        if any(first <= hour <= last for first, last in bands[name]):
            return name
    return default


def test_battle_look_obeys_the_schema() -> None:
    Draft7Validator.check_schema(SCHEMA)
    errors = [e.message for e in Draft7Validator(SCHEMA).iter_errors(LOOK)]
    assert not errors, errors[:5]


def test_the_schema_refuses_what_is_outside_it() -> None:
    checker = Draft7Validator(SCHEMA)
    no_band = json.loads(json.dumps(LOOK))
    del no_band["levels"]["L_Harbor"]["dusk"]
    assert list(checker.iter_errors(no_band))
    other_platform = json.loads(json.dumps(LOOK))
    other_platform["chosen"]["linux"] = {"tier": "low", "measured": None}
    assert list(checker.iter_errors(other_platform))
    late = json.loads(json.dumps(LOOK))
    late["time_bands"]["day"] = [[9, 24]]
    assert list(checker.iter_errors(late))


def test_every_hour_falls_in_one_band() -> None:
    hours = [
        h
        for name in BANDS
        for first, last in LOOK["time_bands"][name]
        for h in range(first, last + 1)
    ]
    assert sorted(hours) == list(range(24))
    for first, last in (r for name in BANDS for r in LOOK["time_bands"][name]):
        assert first <= last


@pytest.mark.parametrize("vector", VECTORS, ids=lambda v: v["name"])
def test_time_band_vectors_follow_the_rule(vector: dict[str, Any]) -> None:
    for case in vector["cases"]:
        got = band_for_hour(case["hour"], vector["time_bands"], vector["default_band"])
        assert got == case["band"], case


def test_the_field_vector_is_the_data() -> None:
    assert len(VECTORS) >= 2
    field = next(v for v in VECTORS if v["name"].endswith("field"))
    assert field["time_bands"] == LOOK["time_bands"]
    assert field["default_band"] == LOOK["default_band"]


def test_every_level_has_a_look_for_every_band() -> None:
    assert set(LOOK["levels"]) == {"L_Lexington", "L_Concord", "L_Harbor"}
    for level, looks in LOOK["levels"].items():
        assert set(looks) == set(BANDS), level
        for name, look in looks.items():
            # The sun is above the horizon in every band (a battle is fought in daylight).
            assert look["sun_rot"][0] < 0, (level, name)


def test_the_chosen_tier_exists_and_an_unmeasured_platform_is_lowest() -> None:
    names = [t["name"] for t in LOOK["tiers"]]
    assert len(names) == len(set(names))
    for platform, choice in LOOK["chosen"].items():
        assert choice["tier"] in names, platform
        if choice["measured"] is None:
            assert choice["tier"] == names[0], platform
    # Every tier sets the same variables, so going down a tier undoes going up.
    keys = {tuple(sorted(t["cvars"])) for t in LOOK["tiers"]}
    assert len(keys) == 1


# ---- Battle Feel Pass (2026-11-16): fx router, words of command, officer voices ----

SFX_DIR = ROOT / "unreal" / "PlayAmerica250" / "Content" / "PA250" / "Audio" / "SFX"
VOICE_DIR = ROOT / "unreal" / "PlayAmerica250" / "Content" / "PA250" / "Audio" / "Voice"
FX_EVENTS = {
    "CannonFired",
    "RoundShotGroundStrike",
    "CanisterFired",
    "ShellBurst",
    "ShellDud",
}


def _imported(folder: Path, name: str) -> bool:
    """A bank is imported when its asset or its first variant is in the project."""
    return (folder / f"{name}.uasset").exists() or (
        folder / f"{name}_1.uasset"
    ).exists()


def test_fx_router_has_exactly_its_five_events_and_no_grapeshot() -> None:
    fx = {k: v for k, v in LOOK["fx"].items() if k != "note"}
    assert set(fx) == FX_EVENTS
    assert not any("grape" in k.lower() for k in LOOK["fx"])
    # A dud is heard and nothing more: no flash, no shake.
    dud = fx["ShellDud"]
    assert dud["niagara"] is None and dud["shake_scale"] == 0
    # The round shot's shake has one owner, battle_feel.intensity.round_shot.
    assert fx["RoundShotGroundStrike"]["shake_scale"] == 0
    for name, entry in fx.items():
        if entry["shake_scale"] > 0:
            assert entry["outer_m"] > entry["inner_m"] >= 0, name


def test_the_schema_refuses_an_unknown_fx_event_or_a_missing_field() -> None:
    checker = Draft7Validator(SCHEMA)
    extra = json.loads(json.dumps(LOOK))
    extra["fx"]["GrapeshotFired"] = dict(LOOK["fx"]["CanisterFired"])
    assert list(checker.iter_errors(extra))
    missing = json.loads(json.dumps(LOOK))
    del missing["fx"]["ShellBurst"]["fallback_sound"]
    assert list(checker.iter_errors(missing))
    no_orders = json.loads(json.dumps(LOOK))
    del no_orders["audio"]["orders"]["halt"]
    assert list(checker.iter_errors(no_orders))
    bad_voice = json.loads(json.dumps(LOOK))
    bad_voice["officer_voices"]["british"] = ["not a voice id"]
    assert list(checker.iter_errors(bad_voice))


def test_every_sound_the_fx_and_orders_name_is_imported() -> None:
    banks: set[str] = set()
    for name, entry in LOOK["fx"].items():
        if name != "note":
            banks |= {entry["sound"], entry["fallback_sound"]}
    for cue in LOOK["audio"]["orders"].values():
        banks |= {cue["drum"], cue["fife"]}
    missing = sorted(b for b in banks if not _imported(SFX_DIR, b))
    assert not missing, f"not imported into /Game/PA250/Audio/SFX: {missing}"
    for side in ("british", "continental"):
        for cue in LOOK["audio"]["orders"].values():
            assert _imported(VOICE_DIR / side, "VO_" + cue["voice"]), (
                side,
                cue["voice"],
            )


def test_officer_lines_are_variants_of_the_shouts_they_belong_to() -> None:
    # Each generated voice is a _1 / _2 variant of a shout bank, so the game plays it.
    for side, voices in LOOK["officer_voices"].items():
        if side == "note":
            continue
        for key in ("make_ready", "present", "fire", "rally", "steady"):
            for n in range(1, len(voices) + 1):
                asset = VOICE_DIR / side / f"VO_{key}_{n}.uasset"
                assert asset.exists(), asset.relative_to(ROOT)
