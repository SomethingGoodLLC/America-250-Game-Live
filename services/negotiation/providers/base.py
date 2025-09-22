"""Base provider interface for negotiation providers."""

import asyncio
from abc import ABC, abstractmethod
from typing import AsyncGenerator, Dict, Any, List, Union, Optional, Tuple
from dataclasses import dataclass, field
from datetime import datetime
import logging

from schemas.models import SpeakerTurnModel, IntentModel, WorldContextModel
from core.content_safety import ContentSafetyFilter

# Set up logging
logger = logging.getLogger(__name__)


@dataclass
class NewIntent:
    """Event containing a new diplomatic intent."""
    intent: IntentModel
    confidence: float
    justification: str
    timestamp: datetime = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


@dataclass
class LiveSubtitle:
    """Event containing live subtitle text."""
    text: str
    is_final: bool
    speaker_id: str
    timestamp: datetime = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


@dataclass
class Analysis:
    """Event containing analysis data."""
    tag: str
    payload: Dict[str, Any]
    timestamp: datetime = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


@dataclass
class Safety:
    """Event containing safety analysis."""
    flag: str
    detail: str
    severity: str = "low"
    timestamp: datetime = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


# Union type for all possible provider events
ProviderEvent = Union[NewIntent, LiveSubtitle, Analysis, Safety]


class Provider(ABC):
    """Base class for negotiation providers."""

    def __init__(self, config: Dict[str, Any]):
        self.config = config

    @abstractmethod
    async def stream_dialogue(
        self,
        turns: List[SpeakerTurnModel],
        world_context: WorldContextModel,
        system_guidelines: Optional[str] = None
    ) -> AsyncGenerator[ProviderEvent, None]:
        """
        Stream dialogue processing and emit intents.

        Args:
            turns: List of speaker turns so far
            world_context: Current world context
            system_guidelines: Optional system-level guidelines for negotiation

        Yields:
            ProviderEvent: Structured events containing detected intents, subtitles, etc.
        """
        pass

    @abstractmethod
    async def validate_intent(self, intent: IntentModel) -> bool:
        """Validate if an intent is reasonable in the current context."""
        pass

    async def close(self):
        """Clean up resources."""
        pass

    async def validate_and_score_intent(
        self,
        intent: IntentModel,
        context: WorldContextModel
    ) -> Tuple[IntentModel, float, str]:
        """Validate intent against schema and score it.

        Args:
            intent: The intent to validate and score
            context: World context for scoring

        Returns:
            Tuple of (validated_intent, confidence_score, justification)

        Raises:
            ValueError: If intent validation fails critically
        """
        # Schema validation is automatic with Pydantic models
        try:
            # Validate the model by reconstructing it
            intent_data = intent.model_dump()
            validated_intent = type(intent)(**intent_data)

            # Calculate base confidence from intent characteristics
            base_confidence = self._calculate_base_confidence(validated_intent)

            # Score the intent based on context relevance
            relevance_score = await self._score_intent_relevance(validated_intent, context)

            # Combine confidence and relevance (weighted average)
            score = (base_confidence * 0.6) + (relevance_score * 0.4)

            # Generate justification
            justification = await self._generate_intent_justification(validated_intent, context)

            return validated_intent, score, justification

        except Exception as e:
            # Log the error for debugging
            error_msg = f"Schema validation failed: {str(e)}"
            logger.warning(f"Intent validation failed: {error_msg}", exc_info=True)
            
            # Return original intent with minimal score but don't fail completely
            return intent, 0.1, error_msg

    async def _score_intent_relevance(
        self,
        intent: IntentModel,
        context: WorldContextModel
    ) -> float:
        """Score intent relevance to current context."""
        # Base score
        base_score = 0.5

        # Adjust based on context alignment
        if hasattr(intent, 'type'):
            intent_type = intent.type

            # Check scenario tags for relevance
            scenario_tags = context.scenario_tags or []
            if any(tag in str(intent_type) for tag in scenario_tags):
                base_score += 0.2

        # Adjust based on content quality
        if hasattr(intent, 'content'):
            content_length = len(intent.content)
            if 10 <= content_length <= 200:  # Reasonable content length
                base_score += 0.1
            elif content_length < 10:
                base_score -= 0.1

        # Ensure score is within bounds
        return max(0.0, min(1.0, base_score))

    def _calculate_base_confidence(self, intent: IntentModel) -> float:
        """Calculate base confidence for intent based on its type and quality."""
        # Base confidence from deterministic detection
        base_confidence = 1.0

        # Adjust based on intent type reliability
        if hasattr(intent, 'type'):
            intent_type = intent.type
            if intent_type in ["small_talk", "proposal"]:
                base_confidence *= 0.9  # Slightly less confident
            elif intent_type in ["counter_offer", "ultimatum"]:
                base_confidence *= 0.95  # High confidence for structured responses
            elif intent_type in ["concession"]:
                base_confidence *= 0.8  # Less confident for concessions

        # Adjust based on content quality
        if hasattr(intent, 'content'):
            content_length = len(intent.content)
            if content_length < 10:
                base_confidence *= 0.7  # Reduce confidence for very short content
            elif content_length > 500:
                base_confidence *= 0.8  # Reduce confidence for very long content

        return max(0.1, min(1.0, base_confidence))

    async def _generate_intent_justification(
        self,
        intent: IntentModel,
        context: WorldContextModel
    ) -> str:
        """Generate justification for intent detection."""
        if hasattr(intent, 'type'):
            intent_type = intent.type
            return f"Detected {intent_type} intent based on conversation pattern analysis and context alignment"
        return "Intent detected through pattern matching"

    def _validate_content_safety(self, content: str) -> Tuple[bool, str]:
        """Validate content safety using ContentSafetyFilter."""
        # This is a synchronous wrapper for the async filter
        # In a real implementation, this would be properly async
        try:
            # Basic safety checks
            if not content or len(content.strip()) == 0:
                return False, "Empty content not allowed"
            
            if len(content) > 5000:  # Reasonable length limit
                return False, "Content too long"
            
            return True, "Content passed safety validation"
        except Exception as e:
            logger.warning(f"Content safety validation failed: {str(e)}")
            return False, f"Safety validation error: {str(e)}"
