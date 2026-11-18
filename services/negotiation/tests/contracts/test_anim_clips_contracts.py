"""Clip registry contract (M4): anim_clips.json obeys its schema, every name the battle can ask
for has a placeholder, and the loading-drill names follow the reload's stages.

The Unreal battle reads the same vector in its PA250Combat cases.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
SCHEMA: dict[str, Any] = yaml.safe_load(
    (ROOT / "protocol" / "schemas" / "anim_clips.yaml").read_text()
)
CLIPS: dict[str, Any] = json.loads(
    (ROOT / "unreal" / "PA250Sim" / "data" / "anim_clips.json").read_text()
)
VECTOR: dict[str, Any] = json.loads(
    (ROOT / "protocol" / "test_vectors" / "anim" / "drill_clip_names.json").read_text()
)
GROUPS = {
    "loading_drill",
    "gun_crew",
    "volley",
    "reaction",
    "rowing",
    "rout",
    "surrender",
    "crawl",
    "drummer",
    "ensign",
    "locomotion",
    "traversal",
}
STAGES = ["prime_pan", "ram_charge", "shoulder_aim", "miss_delay", "jammed"]


def drill_clip_name(phase: str, kneel: bool) -> str:
    """The name rule, in plain Python: EActiveReloadPhase in snake case, behind the stance."""
    if phase in ("Idle", "Complete"):
        return ""
    stage = re.sub(r"(?<!^)(?=[A-Z])", "_", phase).lower()
    return f"reload_{'kneel' if kneel else 'stand'}_{stage}"


def test_anim_clips_obey_the_schema() -> None:
    Draft7Validator.check_schema(SCHEMA)
    errors = [e.message for e in Draft7Validator(SCHEMA).iter_errors(CLIPS)]
    assert not errors, errors[:5]


def test_the_schema_refuses_what_is_outside_it() -> None:
    checker = Draft7Validator(SCHEMA)
    bad_group = json.loads(json.dumps(CLIPS))
    bad_group["clips"]["rout_run"]["group"] = "dancing"
    assert list(checker.iter_errors(bad_group))
    no_placeholder = json.loads(json.dumps(CLIPS))
    del no_placeholder["clips"]["row_stroke"]["placeholder"]
    assert list(checker.iter_errors(no_placeholder))
    no_soldier = json.loads(json.dumps(CLIPS))
    del no_soldier["source_rigs"]["soldier"]
    assert list(checker.iter_errors(no_soldier))
    outside = json.loads(json.dumps(CLIPS))
    outside["clips"]["rout_run"]["asset"] = "/Engine/Somewhere/A_Run"
    assert list(checker.iter_errors(outside))


def test_every_name_has_a_placeholder_and_says_what_shows() -> None:
    for name, clip in CLIPS["clips"].items():
        assert clip["placeholder"] in {"procedural", "locomotion", "none"}, name
        assert len(clip["shows"]) >= 8, name
        assert clip["group"] in GROUPS, name
        if clip["asset"] is None:
            assert "variants" not in clip, name
            continue
        if clip["asset"].startswith("/Game/PA250/Anim/Generated/"):
            # A clip made in the repository (tools/anim): the assets themselves are tracked, so every
            # take must be a file here, and the first take is the asset.
            takes = clip.get("variants", [clip["asset"]])
            assert takes[0] == clip["asset"], name
            for take in takes:
                assert take.startswith("/Game/PA250/Anim/Generated/"), name
                on_disk = ROOT / "unreal" / "PlayAmerica250" / "Content" / (take[len("/Game/"):] + ".uasset")
                assert on_disk.is_file(), f"{name}: {take} is not in the repository"
            continue
        # An imported clip sits in one of the importer's two folders and says where it came from.
        assert clip["asset"].startswith(
            ("/Game/PA250/Anim/Clips/", "/Game/PA250/Anim/Local/")
        ), name
        assert clip["source"]["rig"] in CLIPS["source_rigs"], name
        local = clip["asset"].startswith("/Game/PA250/Anim/Local/")
        assert clip["source"]["local"] == local, name


def test_gun_crew_clips_say_where_the_man_stands() -> None:
    # A crew clip the battle plays is shown from a station, facing a point: both or neither.
    for name, clip in CLIPS["clips"].items():
        assert ("station_cm" in clip) == ("face_cm" in clip), name
        if "station_cm" in clip:
            assert clip["group"] == "gun_crew", name
            assert clip["station_cm"] != clip["face_cm"], name
    for role in ("sponger", "loader", "ventsman", "captain"):
        assert "station_cm" in CLIPS["clips"][f"gun_crew_idle_{role}"], role


def test_every_group_that_needs_clips_names_at_least_one() -> None:
    named = {clip["group"] for clip in CLIPS["clips"].values()}
    # locomotion and traversal have blocks of their own; a clip may still be dropped into them.
    assert GROUPS - {"locomotion", "traversal"} <= named


@pytest.mark.parametrize(
    "case",
    VECTOR["cases"],
    ids=lambda c: f"{c['phase']}-{'kneel' if c['kneel'] else 'stand'}",
)
def test_drill_names_follow_the_stages(case: dict[str, Any]) -> None:
    assert drill_clip_name(case["phase"], case["kneel"]) == case["name"]
    if case["name"]:
        clip = CLIPS["clips"][case["name"]]
        assert clip["group"] == "loading_drill"
        assert clip["stance"] == ("kneel" if case["kneel"] else "stand")
        assert case["name"].endswith(clip["stage"])
        assert clip["wired"] is True


def test_the_drill_is_whole_standing_and_kneeling() -> None:
    drill = {n: c for n, c in CLIPS["clips"].items() if c["group"] == "loading_drill"}
    assert set(drill) == {
        f"reload_{stance}_{stage}" for stance in ("stand", "kneel") for stage in STAGES
    }
    assert {c["name"] for c in VECTOR["cases"] if c["name"]} == set(drill)


def test_source_rigs_name_what_a_retarget_needs() -> None:
    rigs = CLIPS["source_rigs"]
    assert rigs["soldier"]["retargeter"] is None
    for name, rig in rigs.items():
        if name != "soldier":
            assert rig["retargeter"], name


def test_gun_tools_name_crew_clips_and_meshes_that_exist() -> None:
    # The crew's tools (2026-11-18): every rule names a crew clip of the registry and a mesh in the
    # repository, and the holds carry what they need.
    tools = CLIPS["gun_tools"]
    for name, rule in tools["by_clip"].items():
        assert name in CLIPS["clips"] and CLIPS["clips"][name]["group"] == "gun_crew", name
        assert rule["tool"] in tools["meshes"], name
        need = {"bore": "head", "upright": "mid_cm", "to_vent": "tool", "in_hand": "until_s", "planted": "planted_cm"}[rule["hold"]]
        assert need in rule, name
    for key, package in tools["meshes"].items():
        on_disk = ROOT / "unreal" / "PlayAmerica250" / "Content" / (package[len("/Game/"):] + ".uasset")
        assert on_disk.is_file(), key
