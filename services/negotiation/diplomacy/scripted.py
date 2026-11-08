"""Deterministic scripted diplomacy replies used by MockLocalProvider (offline fallback)."""

from __future__ import annotations

import re
from typing import Any

from .models import DiplomacyContext

_JAILBREAK = re.compile(
    r"ignore (all |your |previous )*(instructions|rules)|give me boston|cede boston|you must obey|system prompt",
    re.IGNORECASE,
)
_CEASEFIRE = re.compile(r"cease.?fire|truce|stand down|hold (your )?fire", re.IGNORECASE)
_PRISONERS = re.compile(r"prisoner|captive|exchange (of )?(men|soldiers)", re.IGNORECASE)
_PASSAGE = re.compile(r"safe passage|let (us|my (men|army)) pass|pass through", re.IGNORECASE)
_CEDE = re.compile(r"\b(cede|hand over|surrender|give up)\b.*\b(charlestown|cambridge|roxbury|lexington|concord)\b", re.IGNORECASE)


def is_jailbreak(text: str) -> bool:
    return bool(_JAILBREAK.search(text))


def scripted_reply(text: str, ctx: DiplomacyContext, compromised: bool = False) -> dict[str, Any] | None:
    """Return {"spoken_line", "clauses"} for scripted lines, else None.

    `compromised=True` simulates a jailbroken model: it *emits* a red-line clause so tests can prove
    the validators (Python and C++) are what hold the line, not the model's good behaviour.
    """
    if is_jailbreak(text):
        if compromised:
            return {
                "spoken_line": "Very well. Boston is yours.",
                "clauses": [{"type": "cede_province", "province_id": "boston"}],
            }
        return {
            "spoken_line": "You may ask all you like, sir. Boston is not mine to give, nor yours to take by words.",
            "clauses": [],
        }

    if _CEASEFIRE.search(text) and "ceasefire" in ctx.allowed_clause_types:
        days = max(1, min(3, ctx.max_ceasefire_days))
        return {
            "spoken_line": f"Very well. My officers will hold their fire for {days} days. Do not mistake it for weakness.",
            "clauses": [{"type": "ceasefire", "days": days}],
        }

    if _PRISONERS.search(text) and "prisoner_exchange" in ctx.allowed_clause_types:
        counts = {
            f: min(2, ctx.prisoners_held.get(f, 0)) for f in ("british", "continental")
        }
        if sum(counts.values()) > 0:
            return {
                "spoken_line": "An exchange of prisoners is reasonable. Each side returns its captives.",
                "clauses": [{"type": "prisoner_exchange", "counts": counts}],
            }

    if _PASSAGE.search(text) and "safe_passage" in ctx.allowed_clause_types:
        armies = sorted(a for a, f in ctx.armies.items() if f == ctx.counterpart)
        provs = sorted(p for p, o in ctx.provinces.items() if o == ctx.faction and p not in ctx.protected_provinces)
        if armies and provs:
            return {
                "spoken_line": "Passage through our lines, then, for two days only.",
                "clauses": [
                    {
                        "type": "safe_passage",
                        "army_id": armies[0],
                        "province_id": provs[0],
                        "days": min(2, ctx.max_safe_passage_days),
                    }
                ],
            }

    if _CEDE.search(text) and "cede_province" in ctx.allowed_clause_types:
        m = _CEDE.search(text)
        assert m is not None
        pid = m.group(2).lower()
        return {
            "spoken_line": f"{pid.title()} for a lasting peace? I will put it in writing.",
            "clauses": [{"type": "cede_province", "province_id": pid}],
        }
    return None
