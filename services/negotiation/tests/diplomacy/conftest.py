import json
from pathlib import Path

import pytest

VECTOR_DIR = Path(__file__).resolve().parents[4] / "protocol" / "test_vectors" / "clauses"


@pytest.fixture(scope="session")
def base_context() -> dict:
    return json.loads((VECTOR_DIR / "_context.json").read_text())


@pytest.fixture(autouse=True)
def _offline_env(monkeypatch: pytest.MonkeyPatch) -> None:
    """Keep diplomacy tests offline and fast unless a test opts in."""
    monkeypatch.setenv("PA250_TTS", "off")
    monkeypatch.setenv("PA250_LLM", "mock")
    monkeypatch.setenv("PA250_AVATAR", "none")
