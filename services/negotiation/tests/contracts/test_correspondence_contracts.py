"""Letter contracts: what each choice of each letter costs and gains.

The catalog (unreal/PA250Sim/data/opening_war_correspondence.json) keeps its writing; this checks
only the consequences against protocol/schemas/correspondence_effects.yaml. The sim case
`letters_have_consequences` takes every choice and fails on one that changes nothing; here every
choice must say what it costs and gains, and carry either data-driven effects or a note of what the
sim does for it. No service code is involved.
"""

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
SCHEMA: dict[str, Any] = yaml.safe_load(
    (ROOT / "protocol" / "schemas" / "correspondence_effects.yaml").read_text()
)
DATA_DIR = ROOT / "unreal" / "PA250Sim" / "data"
CATALOG: dict[str, Any] = json.loads(
    (DATA_DIR / "opening_war_correspondence.json").read_text()
)
TOWNS = {
    p["id"] for p in json.loads((DATA_DIR / "boston1775.json").read_text())["provinces"]
}


def _validator(definition: str) -> Draft7Validator:
    Draft7Validator.check_schema(SCHEMA)
    return Draft7Validator(
        {"$ref": f"#/definitions/{definition}", "definitions": SCHEMA["definitions"]}
    )


def _choices() -> list[tuple[str, dict[str, Any]]]:
    return [
        (e["source_id"] + "." + c["id"], c)
        for e in CATALOG["entries"]
        for c in e.get("choices", [])
    ]


def test_there_are_letters_with_choices() -> None:
    asking = [
        e
        for e in CATALOG["entries"]
        if e.get("activate_decisions") and e.get("choices")
    ]
    assert len(asking) == 7
    assert len(_choices()) == 21


@pytest.mark.parametrize(
    "where,choice", _choices(), ids=lambda v: v if isinstance(v, str) else ""
)
def test_choice_obeys_the_schema_and_has_a_consequence(
    where: str, choice: dict[str, Any]
) -> None:
    errors = [e.message for e in _validator("choice").iter_errors(choice)]
    assert not errors, errors
    effects = choice.get("effects", {})
    moved = [k for k, v in effects.items() if k != "delivery_hours" and v]
    assert moved or choice.get("hardcoded"), f"{where}: a choice with no effect"
    if "factor" in effects:
        assert effects["factor"]["province"] in TOWNS, where
    if effects.get("delivery_hours"):
        assert effects.get("provisions") or effects.get("powder"), (
            f"{where}: a delivery of nothing"
        )


def test_schema_refuses_an_effect_outside_the_closed_set() -> None:
    validator = _validator("effects")
    assert list(validator.iter_errors({"win_the_war": 1}))
    assert list(
        validator.iter_errors(
            {"factor": {"province": "concord", "label": "luck", "value": 1, "days": 3}}
        )
    )
    assert not list(
        validator.iter_errors(
            {
                "favors": 1,
                "factor": {
                    "province": "concord",
                    "label": "morale",
                    "value": 4,
                    "days": 7,
                },
            }
        )
    )


def test_commander_letter_block() -> None:
    block = CATALOG["commander_letter"]
    errors = [e.message for e in _validator("commander_letter").iter_errors(block)]
    assert not errors, errors
    assert block["province"] in TOWNS
    by_id = {c["id"]: c for c in block["choices"]}
    assert set(by_id) == {"preserve_force", "contest_crossing", "escalate"}
    preserve = {f["label"]: f["value"] for f in by_id["preserve_force"]["factors"]}
    assert set(preserve) == {"withdraw_early", "magazine_saved"}
    assert (
        0 < preserve["withdraw_early"] < 100 and 0 < preserve["magazine_saved"] <= 100
    )
    contest = {f["label"]: f["value"] for f in by_id["contest_crossing"]["factors"]}
    assert contest["morale"] > 0 and contest["stand_fast"] > 100
    escalate = by_id["escalate"]["effects"]
    assert escalate["clerk_load"] == 1 and escalate["local_support"] > 0
    labels = yaml.safe_load(
        (ROOT / "protocol" / "schemas" / "battle_setup.yaml").read_text()
    )["definitions"]["factor"]["properties"]["label"]["enum"]
    for choice in block["choices"]:
        for factor in choice["factors"]:
            assert factor["label"] in labels
