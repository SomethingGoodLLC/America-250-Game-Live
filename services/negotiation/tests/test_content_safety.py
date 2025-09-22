"""Tests for content safety filter."""

import pytest

from core.content_safety import ContentSafetyFilter
from schemas.models import SpeakerTurnModel, ContentSafetyModel


@pytest.fixture
def content_filter():
    """Content safety filter fixture."""
    return ContentSafetyFilter({})


@pytest.mark.asyncio
async def test_safe_content(content_filter):
    """Test safe content."""
    result = await content_filter.check_content("Hello, this is a safe message.")

    assert result.is_safe
    assert result.flags is None
    assert result.severity is None
    assert result.reason is None


@pytest.mark.asyncio
async def test_profanity_detection(content_filter):
    """Test profanity detection."""
    result = await content_filter.check_content("This message contains profanity: fuck!")

    assert not result.is_safe
    assert "profanity" in result.flags
    assert result.severity == "low"


@pytest.mark.asyncio
async def test_hate_speech_detection(content_filter):
    """Test hate speech detection."""
    result = await content_filter.check_content("I hate all people from that group.")

    assert not result.is_safe
    assert "hate_speech" in result.flags
    assert result.severity == "high"


@pytest.mark.asyncio
async def test_violence_detection(content_filter):
    """Test violence detection."""
    result = await content_filter.check_content("I will kill you!")

    assert not result.is_safe
    assert "violence" in result.flags
    assert result.severity == "medium"


@pytest.mark.asyncio
async def test_personal_info_detection(content_filter):
    """Test personal information detection."""
    result = await content_filter.check_content("My email is john@example.com")

    assert not result.is_safe
    assert "personal_information" in result.flags
    assert result.severity == "high"


@pytest.mark.asyncio
async def test_multiple_flags(content_filter):
    """Test content with multiple safety issues."""
    result = await content_filter.check_content("I fucking hate you and will kill you!")

    assert not result.is_safe
    assert "profanity" in result.flags
    assert "hate_speech" in result.flags
    assert "violence" in result.flags
    assert result.severity == "high"


@pytest.mark.asyncio
async def test_speaker_turn_check(content_filter):
    """Test checking a speaker turn."""
    from datetime import datetime
    turn = SpeakerTurnModel(
        speaker_id="test_speaker",
        text="This is a safe message.",
        timestamp=datetime.now()
    )

    result = await content_filter.check_turn(turn)

    assert result.is_safe


@pytest.mark.asyncio
async def test_filter_provider_output_safe(content_filter):
    """Test filtering safe provider output."""
    output = "This is a safe response from the provider."
    filtered = await content_filter.filter_provider_output(output)

    assert filtered == output


@pytest.mark.asyncio
async def test_filter_provider_output_unsafe(content_filter):
    """Test filtering unsafe provider output."""
    output = "I will kill you all!"
    filtered = await content_filter.filter_provider_output(output)

    assert "[Content filtered for safety]" in filtered
