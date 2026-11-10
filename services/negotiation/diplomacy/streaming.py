"""Sentences out of a streaming tool call, so the envoy can start speaking before Claude has finished.

Claude answers through the `respond_as_envoy` tool; its arguments arrive as partial JSON. `spoken_line` is the
first property of the schema, so it streams first. `SentenceStreamer.feed(partial_json)` returns each sentence of
`spoken_line` as soon as it is complete; `finish()` returns the remainder. The clauses still arrive whole at the
end and are validated as before: speaking early never changes what is binding.
"""

from __future__ import annotations

import json
import re

_KEY = re.compile(r'"spoken_line"\s*:\s*"')
_END = re.compile(r'[.!?;]["\')\]]?(?=\s)')
MIN_CHUNK = 14  # a very short opener ("Sir.") is joined to the next sentence


def spoken_so_far(partial_json: str) -> tuple[str, bool]:
    """Decoded spoken_line text present in a partial JSON object, and whether the string has closed."""
    m = _KEY.search(partial_json)
    if not m:
        return "", False
    i, raw, closed = m.end(), [], False
    while i < len(partial_json):
        c = partial_json[i]
        if c == "\\":
            if i + 1 >= len(partial_json):
                break  # escape cut in half: wait for more
            if partial_json[i + 1] == "u" and i + 6 > len(partial_json):
                break
            step = 6 if partial_json[i + 1] == "u" else 2
            raw.append(partial_json[i:i + step])
            i += step
            continue
        if c == '"':
            closed = True
            break
        raw.append(c)
        i += 1
    try:
        return json.loads('"' + "".join(raw) + '"'), closed
    except json.JSONDecodeError:
        return "", False


class SentenceStreamer:
    def __init__(self) -> None:
        self.emitted = 0  # characters of spoken_line already handed out
        self.closed = False
        self._text = ""

    def feed(self, partial_json: str) -> list[str]:
        text, closed = spoken_so_far(partial_json)
        return self.feed_text(text, closed)

    def feed_text(self, text: str, closed: bool = False) -> list[str]:
        """Plain text so far (the model answered without the tool): same sentence rules."""
        self._text, self.closed = text, closed
        out: list[str] = []
        start = self.emitted
        for m in _END.finditer(text, start):
            end = m.end()
            if end - start < MIN_CHUNK and not out:
                continue  # too short to say alone; extend to the next sentence end
            chunk = text[start:end].strip()
            if chunk:
                out.append(chunk)
            start = end
        self.emitted = start
        return out

    def finish(self) -> list[str]:
        rest = self._text[self.emitted:].strip()
        self.emitted = len(self._text)
        return [rest] if rest else []

    @property
    def text(self) -> str:
        return self._text
