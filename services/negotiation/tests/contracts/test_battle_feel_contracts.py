"""Battle feel contract: the data file obeys its schema, and the shared vectors
(protocol/test_vectors/anim) agree with the rules the schema states.

The Unreal battle reads the same vectors in its PA250Combat cases; the small reference
functions here are the rules in plain Python, so the vectors cannot drift from them.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
SCHEMA_DIR = ROOT / "protocol" / "schemas"
DATA_DIR = ROOT / "unreal" / "PA250Sim" / "data"
VECTOR_DIR = ROOT / "protocol" / "test_vectors" / "anim"
PLATFORMS = {"win64", "mac"}


def _schema(name: str) -> dict[str, Any]:
    doc: dict[str, Any] = yaml.safe_load((SCHEMA_DIR / name).read_text())
    return doc


def _data(name: str) -> dict[str, Any]:
    doc: dict[str, Any] = json.loads((DATA_DIR / name).read_text())
    return doc


def _vectors(prefix: str) -> list[dict[str, Any]]:
    docs: list[dict[str, Any]] = []
    for path in sorted(VECTOR_DIR.glob(prefix + "*.json")):
        doc: dict[str, Any] = json.loads(path.read_text())
        assert doc["name"] == path.stem, path.name
        docs.append(doc)
    return docs


FEEL = _data("battle_feel.json")
FEEL_SCHEMA = _schema("battle_feel.yaml")


def test_battle_feel_obeys_the_schema() -> None:
    Draft7Validator.check_schema(FEEL_SCHEMA)
    errors = [e.message for e in Draft7Validator(FEEL_SCHEMA).iter_errors(FEEL)]
    assert not errors, errors


def test_the_schema_refuses_what_is_outside_it() -> None:
    checker = Draft7Validator(FEEL_SCHEMA)
    extra = json.loads(json.dumps(FEEL))
    extra["animation"]["platforms"]["linux"] = {
        "full_quality_max": 1,
        "switch_distance_m": 0,
        "measured": None,
    }
    assert list(checker.iter_errors(extra))
    nobody = json.loads(json.dumps(FEEL))
    nobody["animation"]["platforms"]["win64"]["full_quality_max"] = 0
    assert list(checker.iter_errors(nobody))
    unknown = dict(FEEL, tuning_in_code=True)
    assert list(checker.iter_errors(unknown))


def test_budgets_are_per_platform_and_an_unmeasured_one_is_lowest() -> None:
    platforms = FEEL["animation"]["platforms"]
    assert set(platforms) == PLATFORMS
    for name, budget in platforms.items():
        if budget["measured"] is None and name != "win64":
            # Not measured there: the player alone at full quality until someone measures.
            assert budget["full_quality_max"] == 1, name
            assert budget["switch_distance_m"] == 0, name
        if budget["measured"] is not None:
            year, month, day = (int(x) for x in budget["measured"].split("-"))
            assert year >= 2026 and 1 <= month <= 12 and 1 <= day <= 31, name
    rate = FEEL["animation"]
    assert rate["play_rate_min"] <= 1 <= rate["play_rate_max"]
    bands = rate["gait_speed_cm_s"]
    assert bands["idle_max"] < bands["walk_max"] < bands["run_max"]


def pick_full(vector: dict[str, Any]) -> list[str]:
    """The tier rule: the player, then the nearest inside the distance, up to the count."""
    if not vector["motion_matching"]:
        return []
    budget = vector["budget"]
    full = [s["id"] for s in vector["soldiers"] if s["player"]]
    others = sorted(
        (s for s in vector["soldiers"] if not s["player"]),
        key=lambda s: (s["distance_m"], s["id"]),
    )
    for s in others:
        if len(full) >= budget["full_quality_max"]:
            break
        reach = budget["switch_distance_m"] + (
            vector["hysteresis_m"] if s["was_full"] else 0
        )
        if s["distance_m"] <= reach:
            full.append(s["id"])
    return full


def pick_gait(speed: float, bands: dict[str, float]) -> str:
    if speed <= bands["idle_max"]:
        return "idle"
    if speed <= bands["walk_max"]:
        return "walk"
    if speed <= bands["run_max"]:
        return "run"
    return "sprint"


@pytest.mark.parametrize("vector", _vectors("tier_pick_"), ids=lambda v: v["name"])
def test_tier_vectors_follow_the_rule(vector: dict[str, Any]) -> None:
    assert sum(1 for s in vector["soldiers"] if s["player"]) == 1
    assert pick_full(vector) == vector["expect_full"]
    assert len(vector["expect_full"]) <= vector["budget"]["full_quality_max"]


@pytest.mark.parametrize("vector", _vectors("gait_pick_"), ids=lambda v: v["name"])
def test_gait_vectors_follow_the_rule(vector: dict[str, Any]) -> None:
    for case in vector["cases"]:
        assert pick_gait(case["speed_cm_s"], vector["bands"]) == case["gait"], case


def test_there_are_vectors_for_each_rule() -> None:
    assert len(_vectors("tier_pick_")) >= 6
    assert len(_vectors("gait_pick_")) >= 2
