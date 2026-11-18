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


def classify_traversal(height: float, depth: float, r: dict[str, float]) -> str:
    """The traversal rule of battle_feel.yaml, in plain Python."""
    if height < r["hurdle_min_cm"]:
        return "none"
    if height > r["mantle_max_height_cm"] or depth > r["mantle_max_depth_cm"]:
        return "none"
    if depth <= r["vault_max_depth_cm"] and height <= r["vault_max_height_cm"]:
        return "hurdle" if height <= r["hurdle_max_height_cm"] else "vault"
    return "mantle"


@pytest.mark.parametrize(
    "vector", _vectors("traversal_classify_"), ids=lambda v: v["name"]
)
def test_traversal_vectors_follow_the_rule(vector: dict[str, Any]) -> None:
    for case in vector["cases"]:
        got = classify_traversal(case["height_cm"], case["depth_cm"], vector["rules"])
        assert got == case["action"], case


def test_the_levels_obstacles_are_crossed_and_houses_are_not() -> None:
    rules = FEEL["traversal"]
    assert (
        rules["hurdle_min_cm"]
        < rules["hurdle_max_height_cm"]
        < rules["vault_max_height_cm"]
        <= rules["mantle_max_height_cm"]
    )
    assert rules["vault_max_depth_cm"] <= rules["mantle_max_depth_cm"]
    # The generated levels' walls are 110 cm high and 80 cm through; a breastwork 120 by 90.
    assert classify_traversal(110, 80, rules) == "vault"
    assert classify_traversal(120, 90, rules) == "vault"
    assert classify_traversal(300, 30, rules) == "none"
    field = next(
        v for v in _vectors("traversal_classify_") if v["name"].endswith("field")
    )
    assert field["rules"] == {k: rules[k] for k in field["rules"]}


def run_buffer(events: list[list[Any]], window: float) -> list[float]:
    """The input-buffer rule of battle_feel.yaml, in plain Python."""
    pressed: float | None = None
    fired: list[float] = []
    for kind, at in events:
        if kind == "press":
            pressed = at
        elif pressed is not None:
            if at - pressed <= window + 1e-9:
                fired.append(at)
            pressed = None
    return fired


@pytest.mark.parametrize("vector", _vectors("input_buffer_"), ids=lambda v: v["name"])
def test_input_buffer_vectors_follow_the_rule(vector: dict[str, Any]) -> None:
    for case in vector["cases"]:
        assert run_buffer(case["events"], vector["window_s"]) == case["fires_at"], case


def test_movement_and_input_values_are_sane() -> None:
    move = FEEL["movement"]
    assert move["walk_speed_cm"] < move["sprint_speed_cm"]
    # A sprint is still what sets off a traversal, and a walk is not.
    assert (
        move["walk_speed_cm"]
        < FEEL["traversal"]["min_speed_cm_s"]
        <= move["sprint_speed_cm"]
    )
    windows = FEEL["input"]["buffer_seconds"]
    assert set(windows) == {"fire", "reload", "vault", "order", "thrust"}
    assert all(0 <= w <= 1 for w in windows.values())
    # A buffered shot must be able to outlast nothing longer than the lock that causes it.
    assert windows["fire"] <= move["fire_lock_seconds"]
    assert len(_vectors("input_buffer_")) >= 3


def music_intensity(
    steady: float, reserve: float, morale_w: float, ticket_w: float
) -> float:
    """The music rule of battle_feel.yaml, in plain Python."""
    steady = min(1.0, max(0.0, steady))
    reserve = min(1.0, max(0.0, reserve))
    return min(1.0, max(0.0, morale_w * (1 - steady) + ticket_w * (1 - reserve)))


def layer_gain(intensity: float, layer: dict[str, float]) -> float:
    if layer["full"] <= layer["from"]:
        return 1.0 if intensity >= layer["from"] else 0.0
    return min(
        1.0, max(0.0, (intensity - layer["from"]) / (layer["full"] - layer["from"]))
    )


@pytest.mark.parametrize("vector", _vectors("music_layers_"), ids=lambda v: v["name"])
def test_music_vectors_follow_the_rule(vector: dict[str, Any]) -> None:
    for case in vector["cases"]:
        intensity = music_intensity(
            case["steady_share"],
            case["reserve_share"],
            vector["morale_weight"],
            vector["ticket_weight"],
        )
        assert intensity == pytest.approx(case["intensity"]), case
        gains = [layer_gain(intensity, layer) for layer in vector["layers"]]
        assert gains == pytest.approx(case["gains"]), case


def test_intensity_values_are_sane() -> None:
    feel = FEEL["intensity"]
    assert feel["round_shot"]["shake_inner_m"] < feel["round_shot"]["shake_outer_m"]
    music = feel["music"]
    assert music["morale_weight"] + music["ticket_weight"] == pytest.approx(1.0)
    # The first layer always plays; each later one comes in no earlier than the one before.
    assert music["layers"][0]["from"] == 0 and music["layers"][0]["full"] == 0
    marks = [layer["from"] for layer in music["layers"]]
    assert marks == sorted(marks)
    field = next(v for v in _vectors("music_layers_") if v["name"].endswith("field"))
    assert field["layers"] == music["layers"]
    assert field["morale_weight"] == music["morale_weight"]
    assert set(feel["shouts"]) == {"charge", "rout", "officer_down", "colours_taken"}
    assert len(_vectors("music_layers_")) >= 2


# ---- Battle Feel Pass (2026-11-16): musket feel and suppression ----


def test_musket_feel_is_bounded_and_rain_never_helps() -> None:
    musket = FEEL["musket"]
    assert 0 <= musket["misfire_chance"] <= 0.5
    assert 0 <= musket["flash_in_pan_chance"] <= 0.5
    assert musket["rain_misfire_scale"] >= 1 and musket["rain_flash_scale"] >= 1
    # A dry pull fires most of the time, the player's included.
    assert musket["misfire_chance"] + musket["flash_in_pan_chance"] < 0.2


def test_suppression_rises_and_always_falls_away() -> None:
    sup = FEEL["intensity"]["suppression"]
    assert 0 < sup["near_miss_add"] <= sup["max"] <= 1
    assert 0 <= sup["impact_add"] <= sup["max"]
    assert sup["decay_per_second"] > 0
    # From full, a man is clear inside ten seconds of quiet.
    assert sup["max"] / sup["decay_per_second"] <= 10


def test_the_schema_requires_the_musket_and_suppression_blocks() -> None:
    checker = Draft7Validator(FEEL_SCHEMA)
    for path in (("musket",), ("intensity", "suppression")):
        doc = json.loads(json.dumps(FEEL))
        parent = doc
        for key in path[:-1]:
            parent = parent[key]
        del parent[path[-1]]
        assert list(checker.iter_errors(doc)), path
    extra = json.loads(json.dumps(FEEL))
    extra["musket"]["active_reload_shorten"] = 0.12  # no field without a reader
    assert list(checker.iter_errors(extra))
    never = json.loads(json.dumps(FEEL))
    never["intensity"]["suppression"]["decay_per_second"] = 0
    assert list(checker.iter_errors(never))
