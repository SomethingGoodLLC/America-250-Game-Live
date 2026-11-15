"""Campaign pass contracts: persons, bodies, measures and decisions against protocol/schemas.

The sim (PA250Sim, C++) reads the same files and refuses what its closed sets do not know; ctest
covers that side (`pass_people`, `pass_politics`, `pass_decisions`, and the shared vectors in
`pass_order_vectors` and `pass_commander_vectors`). Here the files themselves must obey the
published schemas, name only things that exist, and mark every historical claim as documented or
invented. No service code is involved.
"""

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT7

ROOT = Path(__file__).resolve().parents[4]
SCHEMA_DIR = ROOT / "protocol" / "schemas"
DATA_DIR = ROOT / "unreal" / "PA250Sim" / "data"
VECTOR_DIR = ROOT / "protocol" / "test_vectors" / "campaign"


def _schema(name: str) -> dict[str, Any]:
    doc: dict[str, Any] = yaml.safe_load((SCHEMA_DIR / name).read_text())
    return doc


EFFECTS = _schema("effects.yaml")


def _validator(name: str, definition: str | None = None) -> Draft7Validator:
    schema = _schema(name)
    Draft7Validator.check_schema(schema)
    Draft7Validator.check_schema(EFFECTS)
    registry = Registry().with_resource(
        "effects.yaml", Resource.from_contents(EFFECTS, default_specification=DRAFT7)
    )
    target = (
        {"$ref": f"#/definitions/{definition}", "definitions": schema["definitions"]}
        if definition
        else schema
    )
    return Draft7Validator(target, registry=registry)


def _data(name: str) -> dict[str, Any]:
    doc: dict[str, Any] = json.loads((DATA_DIR / name).read_text())
    return doc


PERSONS = _data("persons_boston_1775.json")
BODIES = _data("bodies_boston_1775.json")
MEASURES = _data("measures_boston_1775.json")["measures"]
DECISIONS = _data("decisions_boston_1775.json")["decisions"]
SCENARIO = _data("boston1775.json")
TOWNS = {p["id"] for p in SCENARIO["provinces"]}
ARMIES = {a["id"] for a in SCENARIO["armies"]}
CLASSES = {
    c for faction in _data("unit_classes.json")["factions"].values() for c in faction
}
PERSON_IDS = {p["id"] for p in PERSONS["persons"]}
BLOCS = {b["id"]: body["id"] for body in BODIES["bodies"] for b in body["blocs"]}
BODY_IDS = {b["id"] for b in BODIES["bodies"]}
MEASURE_IDS = {m["id"] for m in MEASURES}
DECISION_IDS = {d["id"] for d in DECISIONS}
BASIS = {"documented", "invented"}


def _effect_sets() -> list[tuple[str, dict[str, Any]]]:
    found: list[tuple[str, dict[str, Any]]] = []
    for m in MEASURES:
        found.append((m["id"] + ".passed", m["passed"]))
        found.append((m["id"] + ".failed", m["failed"]))
        if "passed_later" in m:
            found.append((m["id"] + ".passed_later", m["passed_later"]["effects"]))
    for d in DECISIONS:
        for o in d["options"]:
            where = d["id"] + "." + o["id"]
            found.append((where + ".cost", o["cost"]))
            found.append((where + ".gain", o["gain"]))
            found.append((where + ".later", o["later"]["effects"]))
    return found


def test_persons_obey_the_schema() -> None:
    errors = [e.message for e in _validator("persons.yaml").iter_errors(PERSONS)]
    assert not errors, errors


def test_bodies_and_measures_obey_the_schema() -> None:
    errors = [
        e.message
        for e in _validator("politics.yaml", "bodies_file").iter_errors(BODIES)
    ]
    assert not errors, errors
    errors = [
        e.message
        for e in _validator("politics.yaml", "measures_file").iter_errors(
            {"measures": MEASURES}
        )
    ]
    assert not errors, errors


def test_decisions_obey_the_schema() -> None:
    errors = [
        e.message
        for e in _validator("decisions.yaml").iter_errors({"decisions": DECISIONS})
    ]
    assert not errors, errors


def test_schemas_refuse_what_is_outside_the_closed_sets() -> None:
    effects = Draft7Validator(EFFECTS)
    assert list(effects.iter_errors({"teleport_army": 1}))
    assert list(
        effects.iter_errors(
            {"battle_factors": [{"province": "boston", "label": "luck", "value": 1}]}
        )
    )
    bad_person = dict(PERSONS["persons"][0], roles=["spy"])
    assert list(
        _validator("persons.yaml").iter_errors(dict(PERSONS, persons=[bad_person]))
    )
    unmarked = {k: v for k, v in PERSONS["persons"][0].items() if k != "basis"}
    assert list(
        _validator("persons.yaml").iter_errors(dict(PERSONS, persons=[unmarked]))
    )
    two_blocs = json.loads(json.dumps(BODIES))
    two_blocs["bodies"][0]["blocs"].pop()
    assert list(_validator("politics.yaml", "bodies_file").iter_errors(two_blocs))
    three_options = json.loads(json.dumps(DECISIONS[0]))
    three_options["options"].append(three_options["options"][0])
    assert list(
        _validator("decisions.yaml").iter_errors({"decisions": [three_options]})
    )


def test_every_claim_is_marked_documented_or_invented() -> None:
    for p in PERSONS["persons"]:
        assert p["basis"] in BASIS and p["note"], p["id"]
    for b in BODIES["bodies"]:
        assert b["basis"] in BASIS and b["note"], b["id"]
    assert BODIES["dispatch"]["basis"] in BASIS and BODIES["ministry"]["basis"] in BASIS
    for m in MEASURES:
        assert m["basis"] in BASIS and m["note"], m["id"]
        for term in m.get("terms", []):
            assert term["basis"] in BASIS
    for d in DECISIONS:
        assert d["basis"] in BASIS and d["note"], d["id"]


def test_the_roster_the_task_names_is_all_there() -> None:
    wanted = """artemas_ward israel_putnam william_prescott john_stark william_heath john_thomas
        nathanael_greene benedict_arnold george_washington joseph_warren john_adams samuel_adams
        john_hancock paul_revere william_dawes richard_gridley henry_knox john_glover daniel_morgan
        mercy_otis_warren benjamin_church thomas_gage hugh_percy francis_smith john_pitcairn
        samuel_graves henry_de_berniere timothy_ruggles william_howe henry_clinton john_burgoyne""".split()
    assert set(wanted) == PERSON_IDS
    by_id = {p["id"]: p for p in PERSONS["persons"]}
    for general in (p for p in PERSONS["persons"] if "general" in p["roles"]):
        assert len(general["traits"]) == 2, general["id"]
    assert by_id["george_washington"]["requires_measure"] == "commander_in_chief"
    for g in ("william_howe", "henry_clinton", "john_burgoyne"):
        assert by_id[g]["requires_measure"] == "reinforcement"
    assert by_id["daniel_morgan"]["available_from"] == {"y": 1775, "m": 8, "d": 6}
    assert by_id["benjamin_church"]["informer"] == {
        "for": "british",
        "exposed": {"y": 1775, "m": 10, "d": 4},
    }
    abilities = {p["id"]: p.get("ability") for p in PERSONS["persons"]}
    assert abilities["paul_revere"] == abilities["william_dawes"] == "alarm"
    assert abilities["richard_gridley"] == abilities["henry_knox"] == "engineer"
    assert abilities["john_glover"] == "boats" and abilities["daniel_morgan"] == "scout"
    assert (
        abilities["mercy_otis_warren"] == "pen"
        and abilities["henry_de_berniere"] == "scout"
    )


def test_persons_name_only_things_that_exist() -> None:
    for p in PERSONS["persons"]:
        if "requires_measure" in p:
            assert p["requires_measure"] in MEASURE_IDS, p["id"]
            unlocks = [
                m
                for m in MEASURES
                if p["id"] in m["passed"].get("persons", [])
                or p["id"]
                in m.get("passed_later", {}).get("effects", {}).get("persons", [])
            ]
            assert [m["id"] for m in unlocks] == [p["requires_measure"]], p["id"]
        else:
            assert "available_from" in p, p["id"]
        if "starts_as" in p and "army_id" in p["starts_as"]:
            assert p["starts_as"]["army_id"] in ARMIES, p["id"]
        if "ability" in p:
            assert p["ability"] in PERSONS["abilities"] and "agent" in p["roles"], p[
                "id"
            ]
    for replacement in BODIES["ministry"]["replacements"]:
        assert replacement in PERSON_IDS


def test_measures_name_only_things_that_exist() -> None:
    dispatch_bodies = {b["id"] for b in BODIES["bodies"] if b.get("dispatch")}
    for m in MEASURES:
        assert m["body"] in BODY_IDS, m["id"]
        for bloc in m["bias"]:
            assert BLOCS.get(bloc) == m["body"], (m["id"], bloc)
        if m["body"] in dispatch_bodies:
            assert "arrives" in m or "earliest" in m, m["id"]
        else:
            assert "earliest" in m and "vote" in m, m["id"]
            e, v = m["earliest"], m["vote"]
            assert (e["y"], e["m"], e["d"]) <= (v["y"], v["m"], v["d"]), m["id"]


@pytest.mark.parametrize(
    "where,effects", _effect_sets(), ids=lambda v: v if isinstance(v, str) else ""
)
def test_effects_name_only_things_that_exist(
    where: str, effects: dict[str, Any]
) -> None:
    assert effects.get("summary"), f"{where}: no player-facing summary"
    for key in ("stores", "monthly", "fort", "lure"):
        if key in effects:
            assert effects[key]["province"] in TOWNS, where
    for unit in effects.get("units", []):
        assert unit["province"] in TOWNS and unit["class"] in CLASSES, where
    for factor in effects.get("battle_factors", []):
        assert factor["province"] in TOWNS, where
        assert "days" in factor or factor.get("one_battle"), (
            f"{where}: a factor must end"
        )
    for bloc in effects.get("bloc_shift", {}):
        assert bloc in BLOCS, (where, bloc)
    for person in effects.get("persons", []):
        assert person in PERSON_IDS, (where, person)


def test_the_measures_the_task_names_carry_their_dates() -> None:
    by_id = {m["id"]: m for m in MEASURES}
    votes = {
        "raise_the_army": (1775, 4, 23),
        "adopt_the_army": (1775, 6, 14),
        "rifle_companies": (1775, 6, 14),
        "commander_in_chief": (1775, 6, 15),
        "bills_of_credit": (1775, 6, 22),
        "olive_branch_petition": (1775, 7, 5),
        "powder_committee": (1775, 9, 18),
        "armed_vessels": (1775, 10, 13),
    }
    for mid, (y, mo, d) in votes.items():
        assert by_id[mid]["vote"] == {"y": y, "m": mo, "d": d}, mid
    lands = {
        "reinforcement": (1775, 5, 25),
        "martial_law": (1775, 6, 12),
        "proclaim_rebellion": (1775, 8, 23),
    }
    for mid, (y, mo, d) in lands.items():
        assert by_id[mid]["arrives"] == {"y": y, "m": mo, "d": d}, mid
    assert by_id["coastal_reprisals"]["arrives"]["m"] == 10
    assert {
        "provision_convoy",
        "leave_to_evacuate",
        "stockbridge_company",
        "call_on_neighbours",
    } <= set(by_id)
    assert (
        BODIES["dispatch"]["days_out"] == 38 and BODIES["dispatch"]["days_back"] == 38
    )
    until = next(b for b in BODIES["bodies"] if b["id"] == "provincial_congress")[
        "until"
    ]
    assert until == {"y": 1775, "m": 5, "d": 10}
    # The Stockbridge company is taken on its own terms, each term marked, and it is paid for.
    stockbridge = by_id["stockbridge_company"]
    assert (
        stockbridge["passed"]["gold"] < 0
        and stockbridge["passed"]["units"][0]["own_army"]
    )
    assert {t["basis"] for t in stockbridge["terms"]} == BASIS


def test_decisions_name_only_things_that_exist() -> None:
    wanted = {
        "continental": {
            "which_heights",
            "canada_expedition",
            "assault_or_guns",
            "reenlist_how",
        },
        "british": {"take_the_heights_how", "burn_charlestown", "hold_or_sail"},
    }
    for side, ids in wanted.items():
        assert {d["id"] for d in DECISIONS if d["side"] == side} == ids
    flags = {m["passed"].get("set_flag") for m in MEASURES} - {None}
    for d in DECISIONS:
        assert d["ai_option"] in {o["id"] for o in d["options"]}, d["id"]
        for r in d.get("requires", []):
            if "province" in r:
                assert r["province"] in TOWNS, d["id"]
            if r["type"] == "flag":
                assert r["flag"] in flags, d["id"]
            if r["type"] == "decision_taken":
                assert r["decision"] in DECISION_IDS, d["id"]
            if r["type"] == "measure_passed":
                assert r["measure"] in MEASURE_IDS, d["id"]
    assert any(
        "commit" in o
        for d in DECISIONS
        for o in d["options"]
        if d["id"] == "canada_expedition"
    )


def _vectors(prefix: str) -> list[dict[str, Any]]:
    docs: list[dict[str, Any]] = []
    for path in sorted(VECTOR_DIR.glob(prefix + "*.json")):
        doc: dict[str, Any] = json.loads(path.read_text())
        assert doc["name"] == path.stem, path.name
        docs.append(doc)
    return docs


@pytest.mark.parametrize(
    "vector", _vectors("order_") + _vectors("reject_"), ids=lambda v: v["name"]
)
def test_order_vectors_name_only_things_that_exist(vector: dict[str, Any]) -> None:
    order = vector["order"]
    if vector.get("schema_valid", True) is False or vector.get("expect_error") in {
        "unknown_army"
    }:
        return
    if "person" in order:
        assert order["person"] in PERSON_IDS
    if "province" in order:
        assert order["province"] in TOWNS
    if "army_id" in order:
        assert order["army_id"] in ARMIES
    if "bloc" in order:
        assert order["bloc"] in BLOCS
    if "measure" in order:
        assert order["measure"] in MEASURE_IDS
    if "decision" in order:
        options = {
            o["id"]
            for d in DECISIONS
            if d["id"] == order["decision"]
            for o in d["options"]
        }
        assert order["option"] in options


@pytest.mark.parametrize("vector", _vectors("battle_"), ids=lambda v: v["name"])
def test_commander_battle_vectors_obey_the_battle_schemas(
    vector: dict[str, Any],
) -> None:
    for army, person in vector["generals"].items():
        assert army in ARMIES and person in PERSON_IDS
    if "result" in vector:
        result = Draft7Validator(_schema("battle_result.yaml"))
        errors = [e.message for e in result.iter_errors(vector["result"])]
        assert (not errors) == vector["expect"]["schema_valid"], errors
    else:
        setup = _schema("battle_setup.yaml")
        labels = setup["definitions"]["factor"]["properties"]["label"]["enum"]
        for row in vector["expect_setup"]["factor_rows"]:
            assert row["label"] in labels, row
        side = setup["definitions"]["side"]["properties"]
        assert {"commander", "alarmed", "intel"} <= set(side)
