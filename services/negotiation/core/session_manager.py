"""Session management for negotiation service."""

import asyncio
import uuid
from datetime import datetime, timedelta
from typing import Dict, List, Any, Optional

import structlog

from schemas.models import (
    WorldContextModel,
    NegotiationReportModel,
    SpeakerTurnModel,
    IntentModel
)
from core.logging_config import get_logger_with_correlation
from core.settings import settings


class NegotiationSession:
    """Represents a negotiation session."""

    def __init__(self, session_id: str, world_context: WorldContextModel):
        self.session_id = session_id
        self.world_context = world_context
        self.start_time = datetime.now()
        self.end_time: Optional[datetime] = None
        self.transcript: List[SpeakerTurnModel] = []
        self.intents: List[IntentModel] = []
        self.justifications: List[Dict[str, Any]] = []
        self.is_active = True
        self.mic_enabled = False
        self.push_to_talk_active = False
        self.last_activity = datetime.now()

        # Create logger with correlation ID
        self.logger = get_logger_with_correlation(session_id)

    def update_activity(self):
        """Update last activity timestamp."""
        self.last_activity = datetime.now()

    def is_expired(self) -> bool:
        """Check if session has expired."""
        timeout = timedelta(minutes=settings.session_timeout_minutes)
        return datetime.now() - self.last_activity > timeout


class SessionManager:
    """Manages negotiation sessions."""

    def __init__(self):
        self.sessions: Dict[str, NegotiationSession] = {}
        self.logger = structlog.get_logger(__name__)
        self._cleanup_task: Optional[asyncio.Task] = None
        self._start_cleanup_task()

    def _start_cleanup_task(self):
        """Start the background cleanup task."""
        try:
            if self._cleanup_task is None or self._cleanup_task.done():
                self._cleanup_task = asyncio.create_task(self._cleanup_expired_sessions())
        except RuntimeError:
            # No event loop running, task will be started when needed
            pass

    async def _cleanup_expired_sessions(self):
        """Background task to clean up expired sessions."""
        while True:
            try:
                expired_sessions = [
                    session_id for session_id, session in self.sessions.items()
                    if session.is_expired()
                ]
                
                for session_id in expired_sessions:
                    self.logger.info("Cleaning up expired session", session_id=session_id)
                    await self.end_session(session_id)
                
                # Check for max concurrent sessions
                if len(self.sessions) > settings.max_concurrent_sessions:
                    # Remove oldest sessions
                    oldest_sessions = sorted(
                        self.sessions.items(),
                        key=lambda x: x[1].last_activity
                    )
                    sessions_to_remove = len(self.sessions) - settings.max_concurrent_sessions
                    
                    for session_id, _ in oldest_sessions[:sessions_to_remove]:
                        self.logger.info("Removing session due to limit", session_id=session_id)
                        await self.end_session(session_id)
                
                await asyncio.sleep(60)  # Check every minute
            except Exception as e:
                self.logger.error("Error in cleanup task", error=str(e))
                await asyncio.sleep(60)

    async def create_session(self, session_id: str, world_context: WorldContextModel) -> None:
        """Create a new negotiation session."""
        if session_id in self.sessions:
            raise ValueError(f"Session {session_id} already exists")

        # Ensure cleanup task is running
        if self._cleanup_task is None or self._cleanup_task.done():
            try:
                self._cleanup_task = asyncio.create_task(self._cleanup_expired_sessions())
            except RuntimeError:
                pass  # No event loop yet

        session = NegotiationSession(session_id, world_context)
        self.sessions[session_id] = session

        self.logger.info("Created session", session_id=session_id)

    async def session_exists(self, session_id: str) -> bool:
        """Check if a session exists."""
        return session_id in self.sessions

    async def end_session(self, session_id: str) -> Optional[NegotiationReportModel]:
        """End a session and return the report."""
        session = self.sessions.get(session_id)
        if not session:
            return None

        session.end_time = datetime.now()
        session.is_active = False

        # Generate report
        report = NegotiationReportModel(
            session_id=session_id,
            start_time=session.start_time,
            end_time=session.end_time,
            initiator_faction=session.world_context.initiator_faction,
            counterpart_faction=session.world_context.counterpart_faction,
            transcript=session.transcript,
            intents=session.intents,
            justifications=session.justifications,
            outcome=self._generate_outcome(session),
            content_safety_report=None  # TODO: Implement content safety
        )

        # Clean up session
        del self.sessions[session_id]

        session.logger.info("Session ended")
        return report

    async def get_session_report(self, session_id: str) -> Optional[NegotiationReportModel]:
        """Get the report for a session (if ended)."""
        session = self.sessions.get(session_id)
        if not session or session.is_active:
            return None

        return NegotiationReportModel(
            session_id=session_id,
            start_time=session.start_time,
            end_time=session.end_time,
            initiator_faction=session.world_context.initiator_faction,
            counterpart_faction=session.world_context.counterpart_faction,
            transcript=session.transcript,
            intents=session.intents,
            justifications=session.justifications,
            outcome=self._generate_outcome(session),
            content_safety_report=None
        )

    async def update_mic_state(self, session_id: str, enabled: bool) -> None:
        """Update microphone state for a session."""
        session = self.sessions.get(session_id)
        if session:
            session.mic_enabled = enabled
            session.update_activity()
            session.logger.info("Mic state updated", enabled=enabled)

    async def handle_push_to_talk(self, session_id: str, data: Dict[str, Any]) -> None:
        """Handle push-to-talk events."""
        session = self.sessions.get(session_id)
        if session:
            session.push_to_talk_active = data.get("active", False)
            session.update_activity()
            session.logger.info("PTT state updated", active=session.push_to_talk_active)

    async def handle_text_message(self, session_id: str, data: Dict[str, Any]) -> None:
        """Handle text messages from client."""
        session = self.sessions.get(session_id)
        if session:
            # Add to transcript
            turn = SpeakerTurnModel(
                speaker_id=data.get("speaker_id", "unknown"),
                text=data.get("text", ""),
                timestamp=datetime.now(),
                confidence=1.0  # Text input has full confidence
            )
            session.transcript.append(turn)
            session.update_activity()
            session.logger.info("Text message added to transcript", speaker_id=turn.speaker_id)

    async def inject_intents(self, session_id: str, intents: List[IntentModel]) -> None:
        """Inject intents into a session (for development/testing)."""
        session = self.sessions.get(session_id)
        if session:
            session.intents.extend(intents)
            session.update_activity()
            session.logger.info("Injected intents", count=len(intents))

    def _generate_outcome(self, session: NegotiationSession) -> Dict[str, Any]:
        """Generate a simple outcome based on session data."""
        # This is a placeholder - in a real implementation, this would use
        # the negotiation provider to determine the actual outcome
        return {
            "result": "partial_success",
            "final_relations": 0,  # Neutral
            "agreements": []
        }
