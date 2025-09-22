"""Tests for session manager."""

import pytest
from datetime import datetime

from core.session_manager import SessionManager, NegotiationSession
from schemas.models import WorldContextModel, SpeakerTurnModel


@pytest.fixture
def session_manager():
    """Session manager fixture."""
    return SessionManager()


@pytest.fixture
def world_context():
    """World context fixture."""
    return WorldContextModel(
        scenario_tags=["test", "diplomacy"],
        initiator_faction={"id": "faction_1", "name": "Test Faction 1"},
        counterpart_faction={"id": "faction_2", "name": "Test Faction 2"}
    )


@pytest.mark.asyncio
async def test_create_session(session_manager, world_context):
    """Test session creation."""
    session_id = "test_session_123"

    await session_manager.create_session(session_id, world_context)

    assert await session_manager.session_exists(session_id)
    assert session_id in session_manager.sessions


@pytest.mark.asyncio
async def test_session_exists(session_manager, world_context):
    """Test session existence check."""
    session_id = "test_session_123"

    # Should return False for non-existent session
    assert not await session_manager.session_exists(session_id)

    # Create session
    await session_manager.create_session(session_id, world_context)

    # Should return True for existing session
    assert await session_manager.session_exists(session_id)


@pytest.mark.asyncio
async def test_end_session(session_manager, world_context):
    """Test ending a session."""
    session_id = "test_session_123"

    await session_manager.create_session(session_id, world_context)

    # End session
    report = await session_manager.end_session(session_id)

    assert report is not None
    assert report.session_id == session_id
    assert not await session_manager.session_exists(session_id)
    assert session_id not in session_manager.sessions


@pytest.mark.asyncio
async def test_get_session_report(session_manager, world_context):
    """Test getting session report."""
    session_id = "test_session_123"

    await session_manager.create_session(session_id, world_context)

    # Should return None for active session
    report = await session_manager.get_session_report(session_id)
    assert report is None

    # End session
    await session_manager.end_session(session_id)

    # Should return report for ended session
    report = await session_manager.get_session_report(session_id)
    assert report is not None
    assert report.session_id == session_id


@pytest.mark.asyncio
async def test_update_mic_state(session_manager, world_context):
    """Test updating microphone state."""
    session_id = "test_session_123"

    await session_manager.create_session(session_id, world_context)

    # Update mic state
    await session_manager.update_mic_state(session_id, True)

    # Check that session exists
    assert await session_manager.session_exists(session_id)


@pytest.mark.asyncio
async def test_handle_text_message(session_manager, world_context):
    """Test handling text messages."""
    session_id = "test_session_123"

    await session_manager.create_session(session_id, world_context)

    # Handle text message
    await session_manager.handle_text_message(session_id, {
        "speaker_id": "test_speaker",
        "text": "Hello, world!"
    })

    # Check that transcript was updated
    session = session_manager.sessions[session_id]
    assert len(session.transcript) == 1
    assert session.transcript[0].text == "Hello, world!"


@pytest.mark.asyncio
async def test_inject_intents(session_manager, world_context):
    """Test injecting intents."""
    session_id = "test_session_123"

    await session_manager.create_session(session_id, world_context)

    intents = [
        {
            "type": "proposal",
            "speaker_id": "test_speaker",
            "content": "Test proposal",
            "intent_type": "trade",
            "terms": {"test": "value"},
            "timestamp": datetime.now()
        }
    ]

    # Inject intents
    await session_manager.inject_intents(session_id, intents)

    # Check that intents were added
    session = session_manager.sessions[session_id]
    assert len(session.intents) == 1
