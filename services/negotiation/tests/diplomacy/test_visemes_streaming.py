"""Mouth curves from character timings, and sentence-by-sentence streaming of spoken_line."""

from __future__ import annotations

import json

from diplomacy.streaming import SentenceStreamer, spoken_so_far
from diplomacy.visemes import FPS, approximate_alignment, curves, curves_from_alignment


def _align(text: str, per: float = 0.08) -> dict:
    return {"characters": list(text), "character_start_times_seconds": [i * per for i in range(len(text))],
            "character_end_times_seconds": [i * per + per for i in range(len(text))]}


def test_closure_shuts_the_lips_between_vowels() -> None:
    c = curves_from_alignment(_align("amam"), 0.4)
    assert c is not None and c["fps"] == FPS
    k_m = round(0.12 * FPS)  # middle of the first "m"
    k_a = round(0.04 * FPS)
    assert c["press"][k_m] > 0.8 and c["jaw"][k_m] < c["jaw"][k_a]


def test_shapes_point_the_right_way() -> None:
    oo = curves(list("ooo"), [0, 0.1, 0.2], [0.1, 0.2, 0.3], 0.3)
    ee = curves(list("eee"), [0, 0.1, 0.2], [0.1, 0.2, 0.3], 0.3)
    k = round(0.15 * FPS)
    assert oo["round"][k] > ee["round"][k] and ee["wide"][k] > oo["wide"][k]


def test_rest_before_and_after_speech() -> None:
    c = curves_from_alignment(_align("ah", 0.1), 2.0)
    assert c is not None and c["jaw"][-1] == 0.0 and max(c["jaw"]) > 0.5


def test_approximate_alignment_spans_the_clip() -> None:
    a = approximate_alignment("Pray, name your terms.", 2.0)
    assert len(a["characters"]) == len(a["character_start_times_seconds"])
    assert 0 < a["character_start_times_seconds"][0] < a["character_end_times_seconds"][-1] <= 2.0


def test_streamer_emits_sentences_before_the_json_ends() -> None:
    full = json.dumps({"spoken_line": 'Sir. You ask a great deal; I cannot grant it! Pray, name your "terms".',
                       "clauses": []})
    s, got = SentenceStreamer(), []
    first_at = None
    for i in range(1, len(full) + 1):
        new = s.feed(full[:i])
        if new and first_at is None:
            first_at = i
        got += new
    got += s.finish()
    assert first_at is not None and first_at < len(full) // 2
    assert got[0].startswith("Sir. You ask")  # the short opener is joined to the next sentence
    assert " ".join(got) == json.loads(full)["spoken_line"]


def test_partial_escape_is_not_decoded_early() -> None:
    assert spoken_so_far('{"spoken_line": "He said \\\\') == ("", False) or spoken_so_far('{"spoken_line": "He said \\')[0] == "He said "
