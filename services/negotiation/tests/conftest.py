"""Pytest configuration and fixtures."""

import pytest
from fastapi.testclient import TestClient
import sys
from pathlib import Path

# Add the negotiation service to the path
negotiation_path = Path(__file__).parent
sys.path.insert(0, str(negotiation_path))

from app.main import app


@pytest.fixture
def client():
    """FastAPI test client."""
    return TestClient(app)
