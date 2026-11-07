"""Glue between providers and the game: provider selection, early rejection, world-context mapping."""

from __future__ import annotations

import os
from typing import Any

import structlog

from .models import DiplomacyContext, parse_proposal
from .validator import validate_proposal

logger = structlog.get_logger(__name__)


def world_context_from_diplomacy(ctx_dict: dict[str, Any], envoy_name: str = "", envoy_title: str = "") -> dict[str, Any]:
    """Map a sim DiplomacyContext onto the legacy world_context.v1 shape (+ current_state extras)."""
    faction = str(ctx_dict.get("faction", "british"))
    counterpart = str(ctx_dict.get("counterpart", "continental"))
    return {
        "scenario_tags": ["boston_1775", "pa250_slice"],
        "initiator_faction": {"id": f"player::{counterpart}", "name": counterpart.title()},
        "counterpart_faction": {"id": f"ai::{faction}", "name": faction.title()},
        "current_state": {
            "relations": max(-100, min(100, int(ctx_dict.get("relation", 0)))),
            "diplomacy_context": ctx_dict,
            "envoy": {"name": envoy_name, "title": envoy_title},
        },
    }


def finalize_proposal(ctx: DiplomacyContext, raw: Any) -> tuple[dict[str, Any], list[str]]:
    """Validate an untrusted proposal. Returns (wire_proposal, errors).

    All-or-nothing, matching the sim: any error empties the clauses (the spoken line is kept for
    display). The sim re-validates before applying anything.
    """
    proposal, errors = parse_proposal(raw)
    errors.extend(validate_proposal(ctx, proposal))
    if errors:
        logger.warning("diplomacy proposal rejected early", errors=errors)
        return {"spoken_line": proposal.spoken_line, "clauses": []}, errors
    return proposal.to_wire(), []


def llm_provider_name() -> str:
    """'claude' when a key is configured and PA250_LLM is not forced to 'mock'; else 'mock'."""
    forced = os.getenv("PA250_LLM", "").lower()
    if forced == "mock":
        return "mock"
    if os.getenv("ANTHROPIC_API_KEY"):
        return "claude"
    return "mock"
