"""Letter contracts: what each choice of each letter costs and gains.

The catalog (unreal/PA250Sim/data/opening_war_correspondence.json) keeps its writing; this checks
only the consequences against protocol/schemas/correspondence_effects.yaml. The sim case
`letters_have_consequences` takes every choice and fails on one that changes nothing; here every
choice must say what it costs and gains and carry it as data: no letter keeps a number in the
sim's code (L3, L5 and L7 did until 2026-11-15). No service code is involved.
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
SCENARIO: dict[str, Any] = json.loads((DATA_DIR / "boston1775.json").read_text())
TOWNS = {p["id"] for p in SCENARIO["provinces"]}
ARMY_UNITS = {a["id"]: a["units"] for a in SCENARIO["armies"]}
DONORS = {d["id"]: d for d in CATALOG["donor_reserve_defaults"]}


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
    assert moved, f"{where}: a choice with no effect in the data"
    assert "hardcoded" not in choice, f"{where}: its numbers belong in `effects`"
    if "factor" in effects:
        assert effects["factor"]["province"] in TOWNS, where
    if effects.get("delivery_hours"):
        assert effects.get("provisions") or effects.get("powder"), (
            f"{where}: a delivery of nothing"
        )
    if "hold_units" in effects:
        hold = effects["hold_units"]
        assert hold["army_id"] in ARMY_UNITS, where
        # Never more than the army opens with: the held units come out of its field strength.
        assert (
            0 < hold["count"] <= ARMY_UNITS[hold["army_id"]].get(hold["unit_class"], 0)
        ), where
    if "donor_transfer" in effects:
        donor = effects["donor_transfer"]
        assert donor["reserve"] in DONORS, where
        assert donor["powder"] <= DONORS[donor["reserve"]]["powder"], where


# What L3, L5 and L7 did from the sim's own code until 2026-11-15, now carried by the catalog.
MOVED_TO_DATA: dict[str, dict[str, Any]] = {
    "L3.purchase_provisions": {"gold": -25, "provisions": 8, "delivery_hours": 8},
    "L3.shared_lodging": {"clerk_load": 1},
    "L3.retain_workers": {
        "hold_units": {
            "army_id": "continental_militia",
            "unit_class": "militia",
            "count": 3,
            "hours": 72,
            "reason": "local_household_labor",
        }
    },
    "L5.guarded_removal": {
        "provisions_now": -4,
        "hold_units": {
            "army_id": "continental_militia",
            "unit_class": "militia",
            "count": 2,
            "hours": 48,
            "reason": "boat_guard_crews",
        },
    },
    "L5.negotiate_local": {"gold": -15, "provisions": 4, "delivery_hours": 8},
    "L5.evacuate_preserve_boats": {"provisions": -6},
    "L7.request_transfer": {
        "donor_transfer": {"reserve": "ny_powder", "powder": 8, "hours": 12}
    },
    "L7.fund_procurement": {"gold": -35, "powder": 6, "delivery_hours": 16},
    "L7.conserve_ammunition": {"postpone_offensive": True},
}


@pytest.mark.parametrize("where", sorted(MOVED_TO_DATA))
def test_the_three_letters_carry_their_own_numbers(where: str) -> None:
    effects = dict(_choices())[where].get("effects", {})
    for key, value in MOVED_TO_DATA[where].items():
        assert effects.get(key) == value, f"{where}: {key}"


def test_schema_refuses_an_effect_outside_the_closed_set() -> None:
    validator = _validator("effects")
    assert list(validator.iter_errors({"win_the_war": 1}))
    assert list(validator.iter_errors({"hold_units": {"count": 3}}))
    assert list(
        validator.iter_errors(
            {"donor_transfer": {"reserve": "ny_powder", "powder": 0, "hours": 12}}
        )
    )
    assert list(
        _validator("choice").iter_errors(
            {"id": "x", "label": "X", "effects_summary": "x", "hardcoded": "25 gold"}
        )
    )
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
