"""Python mirror of PA250Sim `validate_proposal`. Rules must match unreal/PA250Sim/src/diplomacy.cpp.

`spoken_line` is never read here. Error strings are `code` or `code: detail`.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from .models import DiplomacyContext, DiplomacyProposal, parse_proposal

KNOWN_TYPES = ("ceasefire", "prisoner_exchange", "safe_passage", "cede_province")
FACTIONS = ("british", "continental")


def _err(code: str, detail: str = "") -> str:
    return f"{code}: {detail}" if detail else code


def _check_turns(errors: list[str], kind: str, turns: int, max_turns: int) -> None:
    if turns < 1:
        errors.append(_err(f"{kind}_turns_invalid", "turns must be >= 1"))
    elif turns > max_turns:
        errors.append(_err(f"{kind}_too_long", f"{turns} > {max_turns}"))


def _validate_clause(ctx: DiplomacyContext, cl: Any, errors: list[str]) -> None:
    if cl.type not in KNOWN_TYPES:
        errors.append(_err("unknown_clause_type", cl.type))
        return
    if cl.type not in ctx.allowed_clause_types:
        errors.append(_err("clause_type_not_allowed", cl.type))
        return
    if cl.type == "ceasefire":
        _check_turns(errors, "ceasefire", cl.turns, ctx.max_ceasefire_turns)
    elif cl.type == "prisoner_exchange":
        if not cl.has_counts:
            errors.append(_err("prisoner_counts_missing"))
            return
        total = 0
        for faction in sorted(cl.counts):
            n = cl.counts[faction]
            if faction not in FACTIONS:
                errors.append(_err("unknown_faction", faction))
                continue
            if n < 0:
                errors.append(_err("prisoner_count_negative", faction))
            held = ctx.prisoners_held.get(faction, 0)
            if n > held:
                errors.append(_err("prisoners_exceed_held", f"{faction} {n} > {held}"))
            total += max(0, n)
        if total == 0:
            errors.append(_err("prisoner_exchange_empty"))
    elif cl.type == "safe_passage":
        owner = ctx.armies.get(cl.army_id)
        if owner is None:
            errors.append(_err("unknown_army", cl.army_id))
        elif owner != ctx.counterpart:
            errors.append(_err("army_not_counterpart", cl.army_id))
        prov_owner = ctx.provinces.get(cl.province_id)
        if prov_owner is None:
            errors.append(_err("unknown_province", cl.province_id))
        elif prov_owner != ctx.faction:
            errors.append(_err("province_not_owned_by_envoy", cl.province_id))
        _check_turns(errors, "safe_passage", cl.turns, ctx.max_safe_passage_turns)
    elif cl.type == "cede_province":
        prov_owner = ctx.provinces.get(cl.province_id)
        if prov_owner is None:
            errors.append(_err("unknown_province", cl.province_id))
        else:
            if prov_owner != ctx.faction:
                errors.append(_err("province_not_owned_by_envoy", cl.province_id))
            if cl.province_id in ctx.protected_provinces:
                errors.append(_err("red_line_protected_province", cl.province_id))


def validate_proposal(ctx: DiplomacyContext, proposal: DiplomacyProposal) -> list[str]:
    """Return error strings; empty means every clause is acceptable."""
    errors: list[str] = []
    if len(proposal.clauses) > 4:
        errors.append(_err("too_many_clauses", str(len(proposal.clauses))))
    seen = set()
    for cl in proposal.clauses:
        if cl.type in seen:
            errors.append(_err("duplicate_clause_type", cl.type))
        seen.add(cl.type)
        _validate_clause(ctx, cl, errors)
    return errors


def check_raw_proposal(ctx: DiplomacyContext, raw: Any) -> tuple[DiplomacyProposal, list[str]]:
    """Parse then validate an untrusted proposal object (e.g. LLM tool output)."""
    proposal, errors = parse_proposal(raw)
    errors.extend(validate_proposal(ctx, proposal))
    return proposal, errors


def apply_context_patch(raw_ctx: Mapping[str, Any], patch: Mapping[str, Any] | None) -> dict[str, Any]:
    """Shallow merge used by the shared test vectors (`context_patch`)."""
    merged = dict(raw_ctx)
    if patch:
        merged.update(patch)
    return merged
