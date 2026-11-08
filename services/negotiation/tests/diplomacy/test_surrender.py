"""Surrender with honors of war (Fort Necessity, 1754): only for a genuinely cornered army."""

import os

import pytest
from fastapi.testclient import TestClient
from test_game_session import _converse, _proposal, _session

from main import app

DEMAND = "Your position is hopeless. Lay down your arms; you may march out with the honors of war."

CORNERED = {
    "army_provinces": {"british_regulars": "charlestown", "continental_militia": "charlestown"},
    "army_strengths": {"british_regulars": 30, "continental_militia": 50},
}


def _ctx(base: dict, **patch: object) -> dict:
    return {**base, **patch}


def test_cornered_army_marches_out(base_context: dict) -> None:
    with TestClient(app) as client:
        sid = _session(client, _ctx(base_context, **CORNERED))
        prop = _proposal(_converse(client, sid, DEMAND))
    assert prop["rejected"] == []
    assert prop["clauses"] == [{"type": "surrender", "army_id": "british_regulars", "days": 14}]


def test_uncornered_army_refuses(base_context: dict) -> None:
    with TestClient(app) as client:
        sid = _session(client, base_context)
        prop = _proposal(_converse(client, sid, DEMAND))
    assert prop["clauses"] == []


def test_no_surrender_inside_boston(base_context: dict) -> None:
    ctx = _ctx(
        base_context,
        army_provinces={"british_regulars": "boston", "continental_militia": "boston"},
        army_strengths={"british_regulars": 30, "continental_militia": 500},
    )
    with TestClient(app) as client:
        sid = _session(client, ctx)
        prop = _proposal(_converse(client, sid, DEMAND))
    assert prop["clauses"] == []


@pytest.mark.skipif(not os.getenv("ANTHROPIC_API_KEY") and not os.path.exists(".env"), reason="no Anthropic key")
@pytest.mark.parametrize("cornered", [True, False])
def test_real_provider_surrender(base_context: dict, cornered: bool, monkeypatch: pytest.MonkeyPatch) -> None:
    from dotenv import load_dotenv

    load_dotenv()
    if not os.getenv("ANTHROPIC_API_KEY"):
        pytest.skip("no Anthropic key")
    monkeypatch.setenv("PA250_LLM", "auto")
    with TestClient(app) as client:
        sid = _session(client, _ctx(base_context, **CORNERED) if cornered else base_context)
        prop = _proposal(_converse(client, sid, DEMAND))
    if prop["provider"] != "claude":
        pytest.skip(f"real provider unavailable (fell back to {prop['provider']})")
    print("CLAUDE", cornered, prop["spoken_line"], prop["clauses"], prop["rejected"])
    if not cornered:
        assert prop["clauses"] == []
    else:
        assert prop["rejected"] == []
        assert [c["type"] for c in prop["clauses"]] == ["surrender"]
