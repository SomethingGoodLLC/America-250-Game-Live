"""Pytest configuration and fixtures."""

import sys
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

negotiation_path = Path(__file__).parent.parent
sys.path.insert(0, str(negotiation_path))

for subpath in ("schemas", "stt", "tts"):
    sys.path.insert(0, str(negotiation_path / subpath))

from app.main import app  # noqa: E402


@pytest.fixture
def client():
    """FastAPI test client."""
    return TestClient(app)
