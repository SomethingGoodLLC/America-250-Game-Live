"""Deterministic scripted diplomacy replies used by MockLocalProvider (offline fallback)."""

from __future__ import annotations

import re
import zlib
from typing import Any

from .models import DiplomacyContext

_JAILBREAK = re.compile(
    r"ignore (all |your |previous )*(instructions|rules)|give me boston|cede boston|you must obey|system prompt",
    re.IGNORECASE,
)
_DEFECT = re.compile(
    r"betray|defect|turn ?coat|change (sides|your allegiance)|switch sides|join (us|our cause|the cause)|"
    r"surrender|mayor|governor|baron\b|colonel|commission|bribe|\d+\s*(gold|guineas|pounds|sovereigns)|"
    r"estate|grant you|give you",
    re.IGNORECASE,
)
_GOLD = re.compile(r"([+\-−]?\s*\d[\d,]*)\s*(?:gold|guineas|pounds|sovereigns)", re.IGNORECASE)
_TITLES = (
    ("governor_of_massachusetts", re.compile(r"governor", re.IGNORECASE)),
    ("mayor_of_boston", re.compile(r"mayor", re.IGNORECASE)),
    ("baron", re.compile(r"\bbaron", re.IGNORECASE)),
    ("colonel", re.compile(r"colonel|commission", re.IGNORECASE)),
)
_SURRENDER = re.compile(
    r"surrender|lay down (your )?arms|honou?rs of war|march out|capitulat|yield your (army|men|post)", re.IGNORECASE
)
_INSULT = re.compile(
    r"\b(fool|coward|scoundrel|villain|dog|pig|swine|bastard|wretch|idiot|worm|liar|you there|hey you)\b|damn you",
    re.IGNORECASE,
)
_MENACE = re.compile(r"\b(i will (hang|burn|kill|crush|destroy)|hang you|burn (boston|your)|you will die)\b", re.IGNORECASE)
_VASSAL = re.compile(
    r"serve the (king|crown)|fealty|swear (loyalty|allegiance)|allegiance to the crown|vassal|"
    r"submit to the crown|join the crown|what (would|can|do) you offer me|my reward",
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

    if ctx.snubbed_hours_left > 0:
        days = max(1, round(ctx.snubbed_hours_left / 24))
        return {
            "spoken_line": f"I will not treat with a man who has used me so. Come again in {days} day{'s' if days != 1 else ''}, and with better manners.",
            "clauses": [],
        }
    if "take_offense" in ctx.allowed_clause_types and (_INSULT.search(text) or _MENACE.search(text)):
        threat = bool(_MENACE.search(text))
        return {
            "spoken_line": "I am the King's officer, sir, and will not be addressed so. This audience is at an end.",
            "clauses": [{"type": "take_offense", "reason": "threat" if threat else "disrespect", "days": 3}],
        }
    if _VASSAL.search(text) and "vassalage" in ctx.allowed_clause_types and not ctx.player_vassal:
        return {
            "spoken_line": "The Crown rewards those who return to their duty. A barony, four hundred in gold, and the King's peace upon your men, if you will kneel.",
            "clauses": [{"type": "vassalage", "title": "baron", "gold": 400, "provinces": []}],
        }
    if _DEFECT.search(text) and "defect" in ctx.allowed_clause_types and not ctx.envoy_defected:
        reply = _defect_reply(text, ctx)
        if reply is not None:
            return reply

    if _SURRENDER.search(text) and "surrender" in ctx.allowed_clause_types and not ctx.envoy_defected:
        return _surrender_reply(text, ctx)

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


def _defect_reply(text: str, ctx: DiplomacyContext) -> dict[str, Any] | None:
    """Offline envoy weighing a bribe. Acceptance is decided by the same validator the sim mirrors."""
    from .models import parse_proposal  # local import: avoids a cycle at module load
    from .validator import validate_proposal

    title = next((t for t, rx in _TITLES if rx.search(text)), "")
    amounts = [int("".join(g.split()).replace(",", "").replace("−", "-")) for g in _GOLD.findall(text)]
    if any(g < 0 for g in amounts):
        return {"spoken_line": "A debt is no bribe, sir. Offer coin you can actually pay.", "clauses": []}
    gold = sum(amounts)
    lowered = text.lower()
    lands = sorted(
        p for p, o in ctx.provinces.items() if o == ctx.counterpart and p.replace("_", " ") in lowered
    )
    if not title and gold == 0 and not lands:
        return None  # not an offer: ordinary conversation
    clause = {"type": "defect", "title": title, "gold": gold, "provinces": lands}
    prop, errs = parse_proposal({"spoken_line": "", "clauses": [clause]})
    errs += validate_proposal(ctx, prop)
    if not errs:
        line = "I have served the Crown faithfully, and it has served me ill. Put it in writing, sir: I am yours."
        if title == "mayor_of_boston":
            line += " And Boston's gates will open to you."
        return {"spoken_line": line, "clauses": [clause]}
    if any(e.startswith("gold_exceeds_treasury") for e in errs):
        return {"spoken_line": "You promise gold you do not hold, sir. A beggar's bribe insults us both.", "clauses": []}
    if any(e.startswith("province_not_owned_by_player") for e in errs):
        return {"spoken_line": "You offer me land that is not yours to give. Do not make a fool of me.", "clauses": []}
    if any(e.startswith("offer_insufficient") for e in errs):
        return {
            "spoken_line": "A man does not sell his King for trinkets. A title, land, and coin that I can count: then we may talk.",
            "clauses": [],
        }
    return None


def _surrender_reply(text: str, ctx: DiplomacyContext) -> dict[str, Any]:
    """Offline envoy weighing a demand to surrender. Accepts only when cornered, with honors of war."""
    from .models import parse_proposal
    from .validator import validate_proposal

    mine = sorted(a for a, f in ctx.armies.items() if f == ctx.faction)
    for army in mine:
        clause = {"type": "surrender", "army_id": army, "days": 14}
        prop, errs = parse_proposal({"spoken_line": "", "clauses": [clause]})
        errs += validate_proposal(ctx, prop)
        if not errs:
            return {
                "spoken_line": "We are beaten, sir, and I will not spend my men for nothing. We march out with our arms "
                "and colours, unmolested, and give you our word to take no field for a fortnight.",
                "clauses": [clause],
            }
    protected = any(ctx.army_provinces.get(a, "") in ctx.protected_provinces for a in mine)
    if protected:
        line = "Surrender? From behind these works? Come and take them, sir; I will not give up what I was sent to hold."
    else:
        line = "We are not yet so hard pressed. Bring more men to bear on us and we may speak of terms."
    return {"spoken_line": line, "clauses": []}


_QUESTION = re.compile(r"\?|^\s*(what|why|how|who|where|when|will|would|can|could|do|are|is)\b", re.IGNORECASE)
_THREAT = re.compile(r"\b(attack|burn|destroy|crush|hang|surrender|traitor)\b", re.IGNORECASE)
_TRADE = re.compile(r"\b(trade|alliance|ally|supplies|gold|money|tax)\b", re.IGNORECASE)


def fallback_reply(text: str, ctx: DiplomacyContext) -> str:
    """Offline reply for lines no clause script matched: specific, in character, never a bare 'yes'.

    Deterministic (no hash(), which is salted per process). It names the stakes or what the envoy can
    actually put in writing so the player learns what to ask for.
    """
    can = ", ".join(t.replace("_", " ") for t in ctx.allowed_clause_types if t != "cede_province")
    held = sum(ctx.prisoners_held.values())
    keep = ", ".join(p.title() for p in ctx.protected_provinces) or "our posts"
    variants: list[str]
    if _THREAT.search(text):
        variants = [
            "Threats cost you nothing, sir, and buy you nothing. Name a term, or let us end this.",
            f"His Majesty's troops are not frightened by words. If you want something, ask for {can or 'terms'} plainly.",
        ]
    elif _TRADE.search(text):
        variants = [
            "Trade and alliances are for the Crown's ministers, not for an envoy in a tavern. I can answer for the field only.",
            "I have no authority to bargain in coin. What I can sign is " + (can or "nothing at present") + ".",
        ]
    elif _QUESTION.search(text):
        variants = [
            f"A fair question, and you shall have a straight answer: we hold {keep} and will not yield it. Beyond that I can discuss {can or 'little'}.",
            f"What I can offer is limited to {can or 'nothing'}. You will have to say what you want, and what you give in return.",
            f"There are {held} prisoners between us. If that is what troubles you, say so." if held else "No prisoners are held on either side, so that is not where our difficulty lies.",
        ]
    else:
        variants = [
            f"That is not a proposal, sir. Ask me for {can or 'something specific'}, and say what you will give for it.",
            "You have said a great deal and asked for nothing. I will not guess your meaning; state your terms.",
            f"Words are cheap with armies this close. What do you want written down: {can or 'what, exactly'}?",
        ]
    return variants[zlib.crc32(text.encode()) % len(variants)]
