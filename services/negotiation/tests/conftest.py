"""Pytest configuration and fixtures."""

import pytest
from fastapi.testclient import TestClient
import sys
from pathlib import Path

# Add the negotiation service to the path
negotiation_path = Path(__file__).parent
sys.path.insert(0, str(negotiation_path))

import sys
from pathlib import Path

# Add the parent directory to path to allow imports
parent_path = Path(__file__).parent.parent
sys.path.insert(0, str(parent_path))

# Add schemas to path
schemas_path = Path(__file__).parent.parent / "schemas"
sys.path.insert(0, str(schemas_path))

# Add stt and tts to path
stt_path = Path(__file__).parent.parent / "stt"
tts_path = Path(__file__).parent.parent / "tts"
sys.path.insert(0, str(stt_path))
sys.path.insert(0, str(tts_path))

from app.main import app


@pytest.fixture
def client():
    """FastAPI test client."""
    return TestClient(app)
