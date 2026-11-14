"""Battle contracts: protocol/schemas/battle_*.yaml against the shared vectors.

The same vectors (protocol/test_vectors/battle) are applied by the C++ sim in ctest
(`battle_vectors`). This side checks that every document in them obeys the published schema, so
the schema, the vectors and the sim cannot drift apart unnoticed. No service code is involved.
"""

import json
from pathlib import Path
from typing import Any

import pytest
import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

PROTOCOL = Path(__file__).resolve().parents[4] / "protocol"
SCHEMA_DIR = PROTOCOL / "schemas"
VECTOR_DIR = PROTOCOL / "test_vectors" / "battle"
VECTORS = sorted(p for p in VECTOR_DIR.glob("*.json") if not p.name.startswith("_"))
END_REASONS = [
    "tickets_exhausted",
    "time_limit",
    "force_preserved",
    "routed",
    "army_broke",
    "posts_swept",
]


def _validator(name: str) -> Draft7Validator:
    schema = yaml.safe_load((SCHEMA_DIR / name).read_text())
    Draft7Validator.check_schema(schema)
    return Draft7Validator(schema)


RESULT = _validator("battle_result.yaml")
SETUP = _validator("battle_setup.yaml")


def _load(path: Path) -> dict[str, Any]:
    doc: dict[str, Any] = json.loads(path.read_text())
    return doc


def test_vectors_present() -> None:
    assert len(VECTORS) >= 12


def test_every_end_reason_has_a_vector() -> None:
    seen = {_load(p)["result"]["end_reason"] for p in VECTORS if "result" in _load(p)}
    for reason in END_REASONS:
        assert reason in seen, f"no vector for end_reason {reason}"


def test_schema_end_reasons_match_this_list() -> None:
    assert RESULT.schema["properties"]["end_reason"]["enum"] == END_REASONS


@pytest.mark.parametrize("path", VECTORS, ids=lambda p: p.stem)
def test_vector(path: Path) -> None:
    v = _load(path)
    assert v["name"] == path.stem
    expect = v["expect"]
    if "result" in v:
        errors = [e.message for e in RESULT.iter_errors(v["result"])]
        assert (not errors) == expect["schema_valid"], errors
    if "setup" in v:
        errors = [e.message for e in SETUP.iter_errors(v["setup"])]
        assert (not errors) == expect["schema_valid"], errors
    if "result" not in v and "setup" not in v:
        # A setup built by the sim from `scenario`; only the C++ side can produce it.
        assert "scenario" in v and ("field_strength" in expect or "mode" in expect)


def test_setup_document_explains_its_numbers() -> None:
    """Every factor names a source, and each side accounts for the headline numbers."""
    setup = _load(VECTOR_DIR / "setup_document.json")["setup"]
    for side in setup["sides"].values():
        labels = {f["label"] for f in side["factors"]}
        for needed in (
            "men",
            "field_strength",
            "rounds_per_man",
            "morale",
            "fortification",
        ):
            assert needed in labels, (side["faction"], needed)
        assert all(f["source"].strip() for f in side["factors"])
