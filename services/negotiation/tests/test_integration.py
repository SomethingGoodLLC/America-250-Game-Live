"""Integration tests for the negotiation service."""

import asyncio
import json
import pytest
from fastapi.testclient import TestClient
from unittest.mock import AsyncMock, patch

from app.main import app
from core.yaml_utils import yaml_helper


@pytest.fixture
def client():
    """FastAPI test client."""
    return TestClient(app)


def test_full_negotiation_flow_json(client):
    """Test a complete negotiation flow using JSON."""
    # Create session
    session_data = {
        "initiator_info": {
            "id": "faction_usa",
            "name": "United States"
        },
        "counterpart_faction_id": "faction_france",
        "scenario_tags": ["trade", "diplomacy", "1800s"]
    }
    
    response = client.post("/v1/session", json=session_data)
    assert response.status_code == 200
    session_id = response.json()["session_id"]
    
    # Inject some test intents
    intents = [{
        "type": "proposal",
        "speaker_id": "faction_usa",
        "content": "We propose a trade agreement",
        "intent_type": "trade",
        "terms": {"duration": "5 years"},
        "timestamp": "2025-01-01T00:00:00Z"
    }]
    
    response = client.post(f"/v1/session/{session_id}/proposed-intents", json=intents)
    assert response.status_code == 200
    
    # End session and get report
    response = client.post(f"/v1/session/{session_id}/end")
    assert response.status_code == 200
    
    report = response.json()
    assert "report" in report
    assert report["report"]["session_id"] == session_id


def test_full_negotiation_flow_yaml(client):
    """Test a complete negotiation flow using YAML."""
    # Create session with YAML
    session_data = {
        "initiator_info": {
            "id": "faction_britain",
            "name": "British Empire"
        },
        "counterpart_faction_id": "faction_spain",
        "scenario_tags": ["territorial", "diplomacy", "colonial"]
    }
    
    yaml_content = yaml_helper.encode(session_data)
    
    response = client.post(
        "/v1/session",
        content=yaml_content,
        headers={"content-type": "application/x-yaml"}
    )
    assert response.status_code == 200
    session_id = response.json()["session_id"]
    
    # Test YAML response
    response = client.get(
        f"/v1/session/{session_id}/report",
        headers={"accept": "application/x-yaml"}
    )
    # Should return 404 for active session, but test YAML handling
    assert response.status_code == 404


def test_webrtc_offer_flow(client):
    """Test WebRTC offer handling."""
    # Create session first
    session_data = {
        "initiator_info": {"id": "test", "name": "Test"},
        "counterpart_faction_id": "other",
        "scenario_tags": ["test"]
    }
    
    response = client.post("/v1/session", json=session_data)
    session_id = response.json()["session_id"]
    
    # Mock WebRTC offer
    offer_data = {
        "sdp": "v=0\r\no=- 123456789 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n",
        "type": "offer"
    }
    
    with patch('core.webrtc_manager.WebRTCManager.handle_offer') as mock_handle:
        mock_handle.return_value = "mock_answer_sdp"
        
        response = client.post(f"/v1/session/{session_id}/webrtc/offer", json=offer_data)
        assert response.status_code == 200
        assert "sdp" in response.json()


@pytest.mark.asyncio
async def test_websocket_control_flow():
    """Test WebSocket control message flow."""
    from fastapi.testclient import TestClient
    
    # Create session first
    session_data = {
        "initiator_info": {"id": "test", "name": "Test"},
        "counterpart_faction_id": "other",
        "scenario_tags": ["test"]
    }
    
    with TestClient(app) as client:
        response = client.post("/v1/session", json=session_data)
        session_id = response.json()["session_id"]
        
        # Test WebSocket connection
        with client.websocket_connect(f"/v1/session/{session_id}/control") as websocket:
            # Send mic state message
            websocket.send_json({
                "type": "mic_state",
                "data": {"enabled": True}
            })
            
            response = websocket.receive_json()
            assert response["type"] == "ack"
            
            # Send text message
            websocket.send_json({
                "type": "text_message",
                "data": {
                    "speaker_id": "test_user",
                    "text": "Hello, let's negotiate!"
                }
            })
            
            response = websocket.receive_json()
            assert response["type"] == "ack"


def test_content_safety_integration(client):
    """Test content safety integration."""
    # Create session
    session_data = {
        "initiator_info": {"id": "test", "name": "Test"},
        "counterpart_faction_id": "other",
        "scenario_tags": ["test"]
    }
    
    response = client.post("/v1/session", json=session_data)
    session_id = response.json()["session_id"]
    
    # Test with unsafe content
    intents = [{
        "type": "proposal",
        "speaker_id": "test_speaker",
        "content": "I will kill you all!",  # Unsafe content
        "intent_type": "war",
        "terms": {"threat": "violence"},
        "timestamp": "2025-01-01T00:00:00Z"
    }]
    
    # Should still accept (content safety is applied at provider level)
    response = client.post(f"/v1/session/{session_id}/proposed-intents", json=intents)
    assert response.status_code == 200


def test_session_timeout_and_cleanup():
    """Test session timeout and cleanup functionality."""
    from core.session_manager import SessionManager, NegotiationSession
    from schemas.models import WorldContextModel
    from datetime import datetime, timedelta
    
    # Create session manager
    manager = SessionManager()
    
    # Create a session
    world_context = WorldContextModel(
        scenario_tags=["test"],
        initiator_faction={"id": "test1", "name": "Test 1"},
        counterpart_faction={"id": "test2", "name": "Test 2"}
    )
    
    session = NegotiationSession("test_session", world_context)
    
    # Simulate expired session
    session.last_activity = datetime.now() - timedelta(hours=2)
    
    assert session.is_expired()


def test_error_handling(client):
    """Test error handling across the API."""
    # Test invalid session operations
    response = client.post("/v1/session/invalid_id/end")
    assert response.status_code == 404
    
    response = client.get("/v1/session/invalid_id/report")
    assert response.status_code == 404
    
    response = client.post("/v1/session/invalid_id/webrtc/offer", json={
        "sdp": "invalid", "type": "offer"
    })
    assert response.status_code == 404
    
    # Test invalid request data
    response = client.post("/v1/session", json={})
    assert response.status_code == 422  # Validation error


def test_yaml_schema_validation():
    """Test YAML schema loading and validation."""
    from pathlib import Path
    
    # Test loading YAML schemas
    schema_dir = Path(__file__).parent.parent.parent.parent / "protocol" / "schemas"
    
    # Load a schema
    error_schema = yaml_helper.load_schema(schema_dir / "error.yaml")
    assert error_schema["type"] == "object"
    assert "error" in error_schema["properties"]
    
    # Test encoding/decoding
    test_data = {
        "error": {
            "code": "TEST_ERROR",
            "message": "This is a test error"
        }
    }
    
    yaml_string = yaml_helper.encode(test_data)
    decoded_data = yaml_helper.decode(yaml_string)
    
    assert decoded_data == test_data
