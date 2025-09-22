"""Tests for the main FastAPI application."""

import pytest
from fastapi.testclient import TestClient

from app.main import app


@pytest.fixture
def client():
    """FastAPI test client."""
    return TestClient(app)


def test_health_check(client):
    """Test health check endpoint."""
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json() == {"status": "healthy"}


def test_create_session(client):
    """Test session creation."""
    request_data = {
        "initiator_info": {
            "id": "faction_1",
            "name": "Test Faction"
        },
        "counterpart_faction_id": "faction_2",
        "scenario_tags": ["diplomacy", "trade"]
    }

    response = client.post("/v1/session", json=request_data)
    assert response.status_code == 200

    response_data = response.json()
    assert "session_id" in response_data
    assert response_data["status"] == "created"
    assert len(response_data["session_id"]) > 0


def test_create_session_invalid_data(client):
    """Test session creation with invalid data."""
    request_data = {
        "initiator_info": {},
        "counterpart_faction_id": "",
        "scenario_tags": []
    }

    response = client.post("/v1/session", json=request_data)
    assert response.status_code == 422  # Validation error


def test_end_session_not_found(client):
    """Test ending a non-existent session."""
    response = client.post("/v1/session/invalid_id/end")
    assert response.status_code == 404


def test_get_session_report_not_found(client):
    """Test getting report for non-existent session."""
    response = client.get("/v1/session/invalid_id/report")
    assert response.status_code == 404


def test_webrtc_offer_invalid_session(client):
    """Test WebRTC offer for invalid session."""
    offer_data = {
        "sdp": "mock_sdp",
        "type": "offer"
    }

    response = client.post("/v1/session/invalid_id/webrtc/offer", json=offer_data)
    assert response.status_code == 404


def test_inject_intents_invalid_session(client):
    """Test injecting intents for invalid session."""
    intents = [{
        "type": "proposal",
        "speaker_id": "test_speaker",
        "content": "Test proposal",
        "intent_type": "trade",
        "terms": {"test": "value"},
        "timestamp": "2025-01-01T00:00:00Z"
    }]

    response = client.post("/v1/session/invalid_id/proposed-intents", json=intents)
    assert response.status_code == 404
