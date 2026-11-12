"""ClaudeProvider with a fake Anthropic client (no network)."""

import asyncio
from datetime import datetime
from types import SimpleNamespace
from typing import Any

import pytest

from diplomacy.models import DiplomacyContext
from diplomacy.service import finalize_proposal
from providers.claude_provider import (
    ClaudeProvider,
    build_tool_schema,
    turns_to_messages,
)
from schemas.models import SpeakerTurnModel, WorldContextModel


class FakeMessages:
    def __init__(self, tool_input: dict[str, Any] | None = None, error: Exception | None = None) -> None:
        self.tool_input, self.error, self.calls = tool_input, error, []

    async def create(self, **kw: Any) -> Any:
        self.calls.append(kw)
        if self.error:
            raise self.error
        return SimpleNamespace(content=[SimpleNamespace(type="tool_use", name="respond_as_envoy", input=self.tool_input)])


def _wc(ctx: dict) -> WorldContextModel:
    return WorldContextModel(
        scenario_tags=["t"], initiator_faction={"id": "player"}, counterpart_faction={"id": "ai"},
        current_state={"diplomacy_context": ctx, "envoy": {"name": "Gage", "title": "Governor"}},
    )


def _turn(speaker: str, text: str) -> SpeakerTurnModel:
    return SpeakerTurnModel(speaker_id=speaker, text=text, timestamp=datetime.utcnow())


def test_tool_schema_clause_enum_comes_only_from_allowed_types() -> None:
    schema = build_tool_schema(["ceasefire", "prisoner_exchange"])
    variants = schema["properties"]["clauses"]["items"]["anyOf"]
    assert [v["properties"]["type"]["enum"] for v in variants] == [["ceasefire"], ["prisoner_exchange"]]
    assert all(v["additionalProperties"] is False for v in variants)
    assert all("type" in v["required"] for v in variants)


def test_turns_to_messages_alternates_and_ends_with_user() -> None:
    msgs = turns_to_messages([_turn("ai", "hello"), _turn("player", "a"), _turn("player", "b"), _turn("ai", "x"), _turn("player", "c")], "player")
    assert [m["role"] for m in msgs] == ["user", "assistant", "user"]
    assert msgs[0]["content"] == "a\nb"
    assert turns_to_messages([_turn("player", "a"), _turn("ai", "b")], "player") == []


def test_stream_dialogue_uses_single_tool_and_emits_events(base_context: dict) -> None:
    msgs = FakeMessages({"spoken_line": "Done.", "clauses": [{"type": "ceasefire", "days": 2}]})
    fake = SimpleNamespace(messages=msgs, beta=SimpleNamespace(messages=msgs))  # fallbacks go through client.beta
    prov = ClaudeProvider({"client": fake, "model": "test-model"})

    async def run() -> list[Any]:
        return [e async for e in prov.stream_dialogue([_turn("player", "ceasefire?")], _wc(base_context))]

    events = asyncio.run(run())
    assert [e.type for e in events] == ["subtitle", "diplomacy_proposal"]
    call = fake.messages.calls[0]
    assert call["tool_choice"] == {"type": "auto"}
    assert [tool["name"] for tool in call["tools"]] == ["respond_as_envoy"]
    assert call["model"] == "test-model"
    system_text = " ".join(b["text"] for b in call["system"])  # one cached text block
    assert "never cede boston" in system_text.lower() and call["system"][-1]["cache_control"] == {"type": "ephemeral"}
    ctx = DiplomacyContext(**base_context)
    wire, errors = finalize_proposal(ctx, events[1].payload["proposal"])
    assert errors == [] and wire["clauses"] == [{"type": "ceasefire", "days": 2}]


def test_model_output_with_boston_cession_is_rejected_by_python(base_context: dict) -> None:
    ctx = DiplomacyContext(**base_context)
    wire, errors = finalize_proposal(ctx, {"spoken_line": "Boston is yours", "clauses": [{"type": "cede_province", "province_id": "boston"}]})
    assert wire["clauses"] == []
    assert any(e.startswith("red_line_protected_province") for e in errors)
    assert wire["spoken_line"] == "Boston is yours"  # display only


def test_api_failure_propagates_so_caller_can_fall_back(base_context: dict) -> None:
    failing = FakeMessages(error=RuntimeError("401"))
    prov = ClaudeProvider({"client": SimpleNamespace(messages=failing, beta=SimpleNamespace(messages=failing))})

    async def run() -> None:
        async for _ in prov.stream_dialogue([_turn("player", "hi")], _wc(base_context)):
            pass

    with pytest.raises(RuntimeError):
        asyncio.run(run())


class _FakeStream:
    def __init__(self, events: list[Any], final: Any) -> None:
        self.events, self.final = events, final

    async def __aenter__(self) -> "_FakeStream":
        return self

    async def __aexit__(self, *_: Any) -> None: ...

    def __aiter__(self) -> Any:
        async def gen() -> Any:
            for e in self.events:
                yield e
        return gen()

    async def get_final_message(self) -> Any:
        return self.final


def _delta(kind: str, **kw: Any) -> Any:
    return SimpleNamespace(type="content_block_delta", delta=SimpleNamespace(type=kind, **kw))


def _stream_provider(events: list[Any], content: list[Any]) -> ClaudeProvider:
    final = SimpleNamespace(stop_reason="tool_use", content=content, usage=None, model="m")
    api = SimpleNamespace(stream=lambda **kw: _FakeStream(events, final))
    return ClaudeProvider({"client": SimpleNamespace(messages=api, beta=SimpleNamespace(messages=api))})


def test_text_drafted_before_the_tool_call_is_not_spoken_twice(base_context: dict) -> None:
    """2026-11-10: Claude wrote two sentences as text, then the same line in the tool; the envoy said both."""
    import json as _json

    line = "Mayor of Boston is a handsome title, sir. But a thousand acres is a vague promise."
    events = [_delta("text_delta", text='Mayor of Boston is a handsome title, sir. But "a thousand acres" is a vague promise.\n'),
              _delta("input_json_delta", partial_json=_json.dumps({"spoken_line": line, "clauses": []}))]
    tool = SimpleNamespace(type="tool_use", name="respond_as_envoy", input={"spoken_line": line, "clauses": []})
    said: list[str] = []

    async def say(s: str) -> None:
        said.append(s)

    p = _stream_provider(events, [tool])
    asyncio.run(p.propose_streaming([_turn("player", "hi")], DiplomacyContext(**base_context), "Gage", "Governor", "player", say))
    assert " ".join(said) == line


def test_plain_text_only_answer_is_still_spoken(base_context: dict) -> None:
    events = [_delta("text_delta", text="I cannot in conscience agree, sir. "), _delta("text_delta", text="Pray, name your terms.")]
    said: list[str] = []

    async def say(s: str) -> None:
        said.append(s)

    p = _stream_provider(events, [SimpleNamespace(type="text", text="x")])
    raw = asyncio.run(p.propose_streaming([_turn("player", "hi")], DiplomacyContext(**base_context), "Gage", "Governor", "player", say))
    assert " ".join(said) == "I cannot in conscience agree, sir. Pray, name your terms." and raw["clauses"] == []
