"""Campaign contracts: the rules and mission data files against protocol/schemas.

The sim (PA250Sim, C++) reads the same two files and refuses what its closed predicate and effect
sets do not know; ctest covers that side. Here the files themselves must obey the published
schemas and name only towns and missions that exist. No service code is involved.
"""

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
SCHEMA_DIR = ROOT / "protocol" / "schemas"
DATA_DIR = ROOT / "unreal" / "PA250Sim" / "data"


def _validator(name: str) -> Draft7Validator:
    schema = yaml.safe_load((SCHEMA_DIR / name).read_text())
    Draft7Validator.check_schema(schema)
    return Draft7Validator(schema)


def _data(name: str) -> dict[str, Any]:
    doc: dict[str, Any] = json.loads((DATA_DIR / name).read_text())
    return doc


RULES = _data("campaign_rules_boston_1775.json")
MISSIONS = _data("missions_boston_1775.json")["missions"]
TOWNS = {p["id"] for p in _data("boston1775.json")["provinces"]}


def _provinces(node: Any) -> list[str]:
    """Every town a predicate or effect names, however deeply nested."""
    found: list[str] = []
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "province" and isinstance(value, str):
                found.append(value)
            elif key == "provinces" and isinstance(value, list):
                found.extend(value)
            else:
                found.extend(_provinces(value))
    elif isinstance(node, list):
        for item in node:
            found.extend(_provinces(item))
    return found


def test_campaign_rules_obey_the_schema() -> None:
    errors = [e.message for e in _validator("campaign_rules.yaml").iter_errors(RULES)]
    assert not errors, errors


def test_missions_obey_the_schema() -> None:
    errors = [
        e.message
        for e in _validator("missions.yaml").iter_errors({"missions": MISSIONS})
    ]
    assert not errors, errors


def test_an_unknown_predicate_is_refused() -> None:
    bad = json.loads(json.dumps(MISSIONS[0]))
    bad["complete"] = {"type": "win_the_war"}
    errors = list(_validator("missions.yaml").iter_errors({"missions": [bad]}))
    assert errors


def test_an_unknown_effect_is_refused() -> None:
    bad = json.loads(json.dumps(MISSIONS[0]))
    bad["reward"] = {"give_boston": True}
    errors = list(_validator("missions.yaml").iter_errors({"missions": [bad]}))
    assert errors


def test_rules_name_real_towns_and_a_sane_window() -> None:
    for role in ("patriot_hq", "british_base", "heights"):
        assert RULES["towns"][role] in TOWNS, role
    start, end = RULES["window"]["start"], RULES["window"]["end"]
    assert (start["y"], start["m"], start["d"]) == (1775, 4, 19)
    assert (end["y"], end["m"], end["d"]) == (1776, 3, 31)
    warn, expiry = RULES["enlistment"]["warning"], RULES["enlistment"]["expiry"]
    assert (warn["y"], warn["m"], warn["d"]) == (1775, 12, 1)
    assert (expiry["y"], expiry["m"], expiry["d"], expiry.get("h", 0)) == (
        1776,
        1,
        1,
        0,
    )


def test_mission_ids_are_unique_and_chained() -> None:
    ids = [m["id"] for m in MISSIONS]
    assert len(ids) == len(set(ids))
    for m in MISSIONS:
        prerequisite = m.get("prerequisite", "")
        if prerequisite:
            assert prerequisite in ids, (m["id"], prerequisite)
            before = next(x for x in MISSIONS if x["id"] == prerequisite)
            assert before["faction"] == m["faction"], "a chain stays on one side"


@pytest.mark.parametrize("faction,count", [("continental", 6), ("british", 5)])
def test_each_side_has_its_chain(faction: str, count: int) -> None:
    chain = [m for m in MISSIONS if m["faction"] == faction]
    assert len(chain) == count
    assert sum(1 for m in chain if not m.get("prerequisite")) == 1, (
        "one opening mission a side"
    )


@pytest.mark.parametrize("mission", MISSIONS, ids=lambda m: m["id"])
def test_mission_names_only_real_towns(mission: dict[str, Any]) -> None:
    named = _provinces(mission) + list(mission.get("target_provinces", []))
    assert named, "every mission points at a town"
    for town in named:
        assert town in TOWNS, town
    assert mission.get("goal"), "the tracker needs plain words for the goal"
