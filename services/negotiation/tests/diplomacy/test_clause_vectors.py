"""Shared clause vectors (protocol/test_vectors/clauses) consumed by both C++ and pytest."""

import json
from pathlib import Path

import pytest

from diplomacy.models import DiplomacyContext
from diplomacy.validator import apply_context_patch, check_raw_proposal

VECTOR_DIR = Path(__file__).resolve().parents[4] / "protocol" / "test_vectors" / "clauses"
BASE_CONTEXT = json.loads((VECTOR_DIR / "_context.json").read_text())
VECTORS = sorted(p for p in VECTOR_DIR.glob("*.json") if not p.name.startswith("_"))


def test_vectors_present() -> None:
    assert len(VECTORS) >= 20


@pytest.mark.parametrize("path", VECTORS, ids=lambda p: p.stem)
def test_vector(path: Path) -> None:
    v = json.loads(path.read_text())
    ctx = DiplomacyContext(**apply_context_patch(BASE_CONTEXT, v.get("context_patch")))
    _, errors = check_raw_proposal(ctx, v["proposal"])
    expect = v["expect"]
    assert (not errors) == expect["valid"], errors
    for code in expect["error_codes"]:
        assert any(e == code or e.startswith(code + ":") for e in errors), (code, errors)
