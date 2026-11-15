"""Campaign order contracts: protocol/schemas/campaign_orders.yaml and its vectors.

The sim (PA250Sim, C++) runs the same vectors in ctest (`halt_orders` and the later order cases).
Here each vector's order must obey the published schema, and the towns, armies and roads a vector
names must exist in the shipped scenario. No service code is involved.
"""

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
SCHEMA = ROOT / "protocol" / "schemas" / "campaign_orders.yaml"
VECTOR_DIR = ROOT / "protocol" / "test_vectors" / "campaign"
SCENARIO: dict[str, Any] = json.loads(
    (ROOT / "unreal" / "PA250Sim" / "data" / "boston1775.json").read_text()
)
TOWNS = {p["id"] for p in SCENARIO["provinces"]}
ARMIES = {a["id"] for a in SCENARIO["armies"]}
EDGES = {e["id"]: e for e in SCENARIO["edges"]}


def _validator() -> Draft7Validator:
    schema = yaml.safe_load(SCHEMA.read_text())
    Draft7Validator.check_schema(schema)
    return Draft7Validator(schema)


def _vectors(prefix: str = "") -> list[dict[str, Any]]:
    docs: list[dict[str, Any]] = []
    for path in sorted(VECTOR_DIR.glob("*.json")):
        doc: dict[str, Any] = json.loads(path.read_text())
        assert doc["name"] == path.stem, path.name
        if doc["name"].startswith(prefix) or doc["name"].startswith("reject_" + prefix):
            docs.append(doc)
    return docs


ORDER_VECTORS = [v for v in _vectors() if "order" in v]


def test_there_are_order_vectors() -> None:
    assert len(_vectors("halt")) >= 5


@pytest.mark.parametrize("vector", ORDER_VECTORS, ids=lambda v: v["name"])
def test_vector_order_obeys_the_schema(vector: dict[str, Any]) -> None:
    errors = [e.message for e in _validator().iter_errors(vector["order"])]
    if vector.get("schema_valid", True):
        assert not errors, errors
    else:
        assert errors, "the schema should refuse this order"


def test_schema_refuses_an_unknown_order_and_stray_fields() -> None:
    validator = _validator()
    assert list(
        validator.iter_errors({"order": "teleport", "army_id": "continental_militia"})
    )
    assert list(
        validator.iter_errors(
            {"order": "halt", "army_id": "continental_militia", "speed": 3}
        )
    )
    assert list(validator.iter_errors({"army_id": "continental_militia"}))


@pytest.mark.parametrize("vector", _vectors("halt"), ids=lambda v: v["name"])
def test_halt_vectors_name_real_towns_and_roads(vector: dict[str, Any]) -> None:
    if "expect_error" in vector:
        assert vector["order"]["army_id"] not in ARMIES
        return
    assert vector["order"]["army_id"] in ARMIES
    for key in ("army_province", "march_to"):
        assert vector[key] in TOWNS, vector[key]
    if "then_march_to" in vector:
        assert vector["then_march_to"] in TOWNS
    after = vector["expect_after_order"]
    assert after["province"] in TOWNS and vector["expect"]["province"] in TOWNS
    if after["returning_hours"] > 0:
        edge = EDGES[after["returning_edge"]]
        # Turning back never takes longer than the leg, and ends where the leg began.
        assert 0 < after["returning_hours"] < edge["march_hours"]
        assert after["province"] in (edge["a"], edge["b"])
        assert vector["hours_after"] >= after["returning_hours"]
        assert vector["expect"]["returning_hours"] == 0
    else:
        assert vector["march_hours_before_halt"] == 0 or "returning_edge" not in after
