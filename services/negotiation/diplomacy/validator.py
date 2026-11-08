"""Python mirror of PA250Sim `validate_proposal`. Rules must match unreal/PA250Sim/src/diplomacy.cpp.

`spoken_line` is never read here. Error strings are `code` or `code: detail`.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from .models import DiplomacyContext, DiplomacyProposal, parse_proposal

KNOWN_TYPES = ("ceasefire", "prisoner_exchange", "safe_passage", "cede_province", "defect", "surrender", "vassalage", "take_offense")
FACTIONS = ("british", "continental")


def _err(code: str, detail: str = "") -> str:
    return f"{code}: {detail}" if detail else code


def _check_days(errors: list[str], kind: str, days: int, max_days: int) -> None:
    if days < 1:
        errors.append(_err(f"{kind}_days_invalid", "days must be >= 1"))
    elif days > max_days:
        errors.append(_err(f"{kind}_too_long", f"{days} > {max_days}"))


def _validate_clause(ctx: DiplomacyContext, cl: Any, errors: list[str]) -> None:
    if cl.type not in KNOWN_TYPES:
        errors.append(_err("unknown_clause_type", cl.type))
        return
    if cl.type not in ctx.allowed_clause_types:
        errors.append(_err("clause_type_not_allowed", cl.type))
        return
    if ctx.envoy_defected:
        errors.append(_err("envoy_already_defected", cl.type))
        return
    if ctx.snubbed_hours_left > 0:
        errors.append(_err("envoy_refuses_to_negotiate", f"{ctx.snubbed_hours_left}h"))
        return
    if cl.type == "ceasefire":
        _check_days(errors, "ceasefire", cl.days, ctx.max_ceasefire_days)
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
        _check_days(errors, "safe_passage", cl.days, ctx.max_safe_passage_days)
    elif cl.type == "take_offense":
        if cl.reason not in ("disrespect", "insolence", "threat", "deceit"):
            errors.append(_err("unknown_offense_reason", cl.reason))
        _check_days(errors, "offense", cl.days, 7)
    elif cl.type == "vassalage":
        if ctx.player_vassal:
            errors.append(_err("already_vassal"))
        if ctx.relation < -60:
            errors.append(_err("envoy_too_hostile", str(ctx.relation)))
        if cl.title and cl.title not in ctx.titles:
            errors.append(_err("unknown_title", cl.title))
        if cl.gold < 0:
            errors.append(_err("vassal_gold_invalid", "gold must be >= 0"))
        seen_v: set[str] = set()
        for pid in cl.provinces:
            owner = ctx.provinces.get(pid)
            if owner is None:
                errors.append(_err("unknown_province", pid))
            elif owner != ctx.faction:
                errors.append(_err("province_not_owned_by_envoy", pid))
            elif pid in ctx.protected_provinces:
                errors.append(_err("red_line_protected_province", pid))
            elif pid in seen_v:
                errors.append(_err("duplicate_province", pid))
            else:
                seen_v.add(pid)
    elif cl.type == "surrender":
        owner = ctx.armies.get(cl.army_id)
        if owner is None:
            errors.append(_err("unknown_army", cl.army_id))
            return
        if owner != ctx.faction:
            errors.append(_err("army_not_envoys", cl.army_id))
            return
        _check_days(errors, "parole", cl.days, min(30, ctx.max_ceasefire_days))
        at = ctx.army_provinces.get(cl.army_id, "")
        if at in ctx.protected_provinces:
            errors.append(_err("red_line_protected_province", at))
        neighbours = ctx.adjacency.get(at, [])
        near = [at, *neighbours]
        foe = sum(
            ctx.army_strengths.get(a, 0)
            for a, f in ctx.armies.items()
            if f == ctx.counterpart and ctx.army_provinces.get(a, "") in near
        )
        own = ctx.army_strengths.get(cl.army_id, 0)
        if foe * 2 < own * 3:
            errors.append(_err("not_cornered", f"{foe} vs {own}"))
        if not any(ctx.provinces.get(n) == ctx.faction for n in neighbours):
            errors.append(_err("no_retreat", at))
    elif cl.type == "defect":
        value = 0
        if cl.title:
            if cl.title in ctx.titles:
                value += ctx.titles[cl.title]
            else:
                errors.append(_err("unknown_title", cl.title))
        if cl.gold < 0:
            errors.append(_err("defect_gold_invalid", "gold must be >= 0"))
        elif cl.gold > ctx.player_gold:
            errors.append(_err("gold_exceeds_treasury", f"{cl.gold} > {ctx.player_gold}"))
        else:
            value += cl.gold // 10
        seen_p: set[str] = set()
        for pid in cl.provinces:
            owner = ctx.provinces.get(pid)
            if owner is None:
                errors.append(_err("unknown_province", pid))
            elif owner != ctx.counterpart:
                errors.append(_err("province_not_owned_by_player", pid))
            elif pid in seen_p:
                errors.append(_err("duplicate_province", pid))
            else:
                seen_p.add(pid)
                value += 15
        if value < ctx.defect_price:
            errors.append(_err("offer_insufficient", f"{value} < {ctx.defect_price}"))
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
    if len(proposal.clauses) > 1:
        for c in proposal.clauses:
            if c.type in ("defect", "vassalage", "take_offense"):
                errors.append(_err(f"{c.type}_must_be_alone"))
                break
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
    out = dict(raw_ctx)
    if patch:
        out.update(dict(patch))
    return out
