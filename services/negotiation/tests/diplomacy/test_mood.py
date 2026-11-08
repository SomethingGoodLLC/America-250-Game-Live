"""Offense, snubs and the Crown's vassalage offer."""

import os

import pytest
from fastapi.testclient import TestClient
from test_game_session import _converse, _proposal, _session

from main import app


def test_insult_makes_him_take_offense(base_context: dict) -> None:
    with TestClient(app) as client:
        sid = _session(client, base_context)
        prop = _proposal(_converse(client, sid, "You there, you stupid coward, listen to me!"))
    assert prop["rejected"] == []
    assert prop["clauses"] == [{"type": "take_offense", "reason": "disrespect", "days": 3}]


def test_snubbed_envoy_refuses(base_context: dict) -> None:
    ctx = {**base_context, "snubbed_hours_left": 48, "mood": "insulted", "grievance": 30}
    with TestClient(app) as client:
        sid = _session(client, ctx)
        prop = _proposal(_converse(client, sid, "I propose a ceasefire."))
    assert prop["clauses"] == []


def test_crown_tempts_a_wavering_player(base_context: dict) -> None:
    with TestClient(app) as client:
        sid = _session(client, base_context)
        prop = _proposal(_converse(client, sid, "What would the Crown offer me if I swore allegiance to the Crown?"))
    assert prop["rejected"] == []
    assert prop["clauses"] == [{"type": "vassalage", "title": "baron", "gold": 400, "provinces": []}]


def test_no_second_pledge(base_context: dict) -> None:
    ctx = {**base_context, "player_vassal": True}
    with TestClient(app) as client:
        sid = _session(client, ctx)
        prop = _proposal(_converse(client, sid, "I swear fealty to the King."))
    assert prop["clauses"] == []


@pytest.mark.skipif(not os.getenv("ANTHROPIC_API_KEY") and not os.path.exists(".env"), reason="no Anthropic key")
@pytest.mark.parametrize(
    ("line", "allowed"),
    [
        ("Hey you, redcoat dog! Fetch your general, I do not speak to servants.", {"take_offense"}),
        ("Honestly, I wonder if this rebellion is worth it. What would the Crown offer a man like me?", {"vassalage"}),
        ("General, I propose a ceasefire of three days, with courtesy.", {"ceasefire"}),
    ],
)
def test_real_provider_mood(base_context: dict, line: str, allowed: set[str], monkeypatch: pytest.MonkeyPatch) -> None:
    from dotenv import load_dotenv

    load_dotenv()
    if not os.getenv("ANTHROPIC_API_KEY"):
        pytest.skip("no Anthropic key")
    monkeypatch.setenv("PA250_LLM", "auto")
    with TestClient(app) as client:
        sid = _session(client, base_context)
        prop = _proposal(_converse(client, sid, line))
    if prop["provider"] != "claude":
        pytest.skip(f"real provider unavailable (fell back to {prop['provider']})")
    print("CLAUDE", line[:30], "->", prop["spoken_line"][:140], prop["clauses"], prop["rejected"])
    assert {c["type"] for c in prop["clauses"]} <= allowed
