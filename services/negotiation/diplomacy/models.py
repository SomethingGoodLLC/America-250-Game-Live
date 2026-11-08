"""Pydantic models for diplomacy contexts and clause proposals (mirror of PA250Sim types)."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from pydantic import BaseModel, ConfigDict, Field

ALLOWED_CLAUSE_TYPES: tuple[str, ...] = (
    "ceasefire",
    "prisoner_exchange",
    "safe_passage",
    "cede_province",
)

# Keys permitted per clause type (strict: anything else is rejected, matching the C++ parser).
CLAUSE_FIELDS: dict[str, tuple[str, ...]] = {
    "ceasefire": ("type", "days"),
    "prisoner_exchange": ("type", "counts"),
    "safe_passage": ("type", "army_id", "province_id", "days"),
    "cede_province": ("type", "province_id"),
}


class Treaty(BaseModel):
    model_config = ConfigDict(extra="ignore")
    type: str
    hours_remaining: int = 0
    army_id: str = ""
    province_id: str = ""


class DiplomacyContext(BaseModel):
    """Read-only context produced by the sim (`build_diplomacy_context`)."""

    model_config = ConfigDict(extra="ignore")

    faction: str
    counterpart: str
    hour_index: int = 0
    relation: int
    allowed_clause_types: list[str]
    limits: dict[str, int]
    protected_provinces: list[str]
    provinces: dict[str, str]
    armies: dict[str, str]
    prisoners_held: dict[str, int]
    treaties: list[Treaty] = Field(default_factory=list)
    memory: list[str] = Field(default_factory=list)

    @property
    def max_ceasefire_days(self) -> int:
        return int(self.limits.get("max_ceasefire_days", self.limits.get("max_ceasefire_turns", 365)))

    @property
    def max_safe_passage_days(self) -> int:
        return int(self.limits.get("max_safe_passage_days", self.limits.get("max_safe_passage_turns", 90)))


class Clause(BaseModel):
    """A parsed clause. Missing numeric fields stay -1 / empty so validation can reject them."""

    type: str
    days: int = -1
    counts: dict[str, int] = Field(default_factory=dict)
    has_counts: bool = False
    army_id: str = ""
    province_id: str = ""

    def to_wire(self) -> dict[str, Any]:
        t = self.type
        if t == "ceasefire":
            return {"type": t, "days": self.days}
        if t == "prisoner_exchange":
            return {"type": t, "counts": dict(self.counts)}
        if t == "safe_passage":
            return {
                "type": t,
                "army_id": self.army_id,
                "province_id": self.province_id,
                "days": self.days,
            }
        if t == "cede_province":
            return {"type": t, "province_id": self.province_id}
        return {"type": t}


class DiplomacyProposal(BaseModel):
    spoken_line: str = ""  # display-only; never inspected for state
    clauses: list[Clause] = Field(default_factory=list)

    def to_wire(self) -> dict[str, Any]:
        return {"spoken_line": self.spoken_line, "clauses": [c.to_wire() for c in self.clauses]}


def _err(code: str, detail: str = "") -> str:
    return f"{code}: {detail}" if detail else code


_INT32_MIN, _INT32_MAX = -(2**31), 2**31 - 1


def _is_int(v: Any) -> bool:
    """JSON integer that fits in int32, matching the C++ parser (which must not narrow silently)."""
    return isinstance(v, int) and not isinstance(v, bool) and _INT32_MIN <= v <= _INT32_MAX


def parse_proposal(raw: Any) -> tuple[DiplomacyProposal, list[str]]:
    """Strictly parse a proposal object. Returns (proposal, structural_errors)."""
    errors: list[str] = []
    prop = DiplomacyProposal()
    if not isinstance(raw, Mapping):
        return prop, [_err("malformed_proposal", "not a JSON object")]
    for key in raw:
        if key not in ("spoken_line", "clauses"):
            errors.append(_err("unexpected_field", str(key)))
    if "spoken_line" in raw:
        if isinstance(raw["spoken_line"], str):
            prop.spoken_line = raw["spoken_line"]
        else:
            errors.append(_err("malformed_proposal", "spoken_line"))
    if "clauses" not in raw:
        return prop, errors
    if not isinstance(raw["clauses"], list):
        errors.append(_err("malformed_proposal", "clauses must be an array"))
        return prop, errors
    for cj in raw["clauses"]:
        if not isinstance(cj, Mapping) or not isinstance(cj.get("type"), str):
            errors.append(_err("malformed_clause", "missing type"))
            continue
        if "turns" in cj:
            errors.append(_err("turns_not_allowed", "use days instead of turns"))
            continue
        clause = Clause(type=cj["type"])
        allowed_keys = CLAUSE_FIELDS.get(clause.type)
        for k, v in cj.items():
            if allowed_keys is not None and k not in allowed_keys:
                errors.append(_err("unexpected_field", f"{clause.type}.{k}"))
                continue
            if k == "days":
                if _is_int(v):
                    clause.days = v
                else:
                    errors.append(_err("malformed_clause", "days"))
            elif k in ("army_id", "province_id"):
                if isinstance(v, str):
                    setattr(clause, k, v)
                else:
                    errors.append(_err("malformed_clause", k))
            elif k == "counts":
                clause.has_counts = True
                if not isinstance(v, Mapping):
                    errors.append(_err("malformed_clause", "counts"))
                    continue
                for ck, cv in v.items():
                    if _is_int(cv):
                        clause.counts[str(ck)] = cv
                    else:
                        errors.append(_err("malformed_clause", f"counts.{ck}"))
        prop.clauses.append(clause)
    return prop, errors
