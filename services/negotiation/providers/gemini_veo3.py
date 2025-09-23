"""Gemini Veo3 provider for video+dialogue negotiation pipeline."""

import asyncio
import re
from typing import AsyncGenerator, AsyncIterator, Dict, Any, List, Optional, Protocol, Iterable
from datetime import datetime
import structlog
from ruamel.yaml import YAML

from schemas.models import (
    SpeakerTurnModel, IntentModel, WorldContextModel,
    ProposalModel, ConcessionModel, CounterOfferModel,
    UltimatumModel, SmallTalkModel
)
from schemas.validators import validator
from .base import Provider, ProviderEvent, NewIntent, LiveSubtitle, Analysis, Safety
from .types import VideoSourceConfig
from ._safety import screen_intent
from ._scoring import score_intent
from ._backpressure import BoundedAIO
from stt.base import STTProvider
from tts.base import TTSProvider
from .video_sources import create_video_source


class VideoSource(Protocol):
    """Protocol for video sources."""
    async def start(self) -> None: ...
    async def stop(self) -> None: ...


class Veo3Provider(Provider):
    """Provider using Google's Gemini Veo3 for video+dialogue negotiation pipeline.

    This provider integrates video avatar generation, speech-to-text, text-to-speech,
    and intent detection in a unified pipeline with backpressure control.
    """

    def __init__(
        self,
        *,
        avatar_style: str = "colonial_diplomat",
        voice_id: str = "en_male_01",
        latency_target_ms: int = 800,
        use_veo3: bool = False,
        video_source: Optional[VideoSource] = None,
        stt_provider: Optional[STTProvider] = None,
        tts_provider: Optional[TTSProvider] = None,
    ):
        """Initialize the Veo3 provider.

        Args:
            avatar_style: Style for the avatar (e.g., "colonial_diplomat")
            voice_id: Voice identifier for TTS
            latency_target_ms: Target latency in milliseconds
            use_veo3: Whether to use actual Veo3 API or placeholder
            video_source: Video source for avatar generation
            stt_provider: Speech-to-text provider
            tts_provider: Text-to-speech provider
        """
        self.logger = structlog.get_logger(__name__)

        # Configuration
        self.avatar_style = avatar_style
        self.voice_id = voice_id
        self.latency_target_ms = latency_target_ms
        self.use_veo3 = use_veo3

        # Dependency injection
        self.video_source = video_source
        self.stt_provider = stt_provider
        self.tts_provider = tts_provider

        # Create video source if not provided
        if self.video_source is None:
            video_config = VideoSourceConfig(
                source_type="veo3" if use_veo3 else "placeholder",
                avatar_style=avatar_style,
                resolution=(640, 480),
                framerate=30,
                quality="medium"
            )
            self.video_source = create_video_source(video_config)

        # YAML utilities
        self.yaml = YAML(typ='safe')

        # Function calling schema for intent detection
        self._function_schema = {
            "name": "detect_diplomatic_intent",
            "description": "Detect and classify diplomatic intents from conversation text",
            "parameters": {
                "type": "object",
                "properties": {
                    "intent_type": {
                        "type": "string",
                        "enum": ["PROPOSAL", "CONCESSION", "COUNTER_OFFER", "ULTIMATUM", "SMALL_TALK"],
                        "description": "Type of diplomatic intent"
                    },
                    "content": {
                        "type": "string",
                        "description": "The intent content"
                    },
                    "confidence": {
                        "type": "number",
                        "minimum": 0.0,
                        "maximum": 1.0,
                        "description": "Confidence score for the intent detection"
                    },
                    "terms": {
                        "type": "object",
                        "description": "Additional terms or conditions"
                    }
                },
                "required": ["intent_type", "content", "confidence"]
            }
        }

    async def stream_dialogue(
        self,
        turns: Iterable[Dict[str, Any]],
        world_context: Dict[str, Any],
        system_guidelines: str,
    ) -> AsyncIterator[ProviderEvent]:
        """Stream dialogue processing with video, subtitles, and intent detection.

        Args:
            turns: Iterable of speaker turn YAML objects
            world_context: World context YAML object
            system_guidelines: Safety/tone/system text

        Yields:
            ProviderEvent: Events including subtitles, intents, analysis, safety
        """
        subs_q: Optional[BoundedAIO] = None
        intent_q: Optional[BoundedAIO] = None

        try:
            # Convert inputs to models for internal processing
            turn_models = [SpeakerTurnModel(**turn) if isinstance(turn, dict) else turn for turn in turns]
            world_model = WorldContextModel(**world_context) if isinstance(world_context, dict) else world_context
            
            # Build system prompt
            system_prompt = self._build_system_prompt(world_model, system_guidelines)

            # Start video source
            try:
                await self.video_source.start()
                self.logger.info("Video source started successfully")
            except Exception as e:
                self.logger.error("Failed to start video source", error=str(e))
                # Continue without video - audio/text processing can still work
                
            # Create backpressured queues
            subs_q = BoundedAIO(maxsize=50)
            intent_q = BoundedAIO(maxsize=20)

            # Start subtitle streaming task
            subtitle_task = asyncio.create_task(
                self._stream_subtitles(subs_q, turn_models, system_prompt)
            )

            # Start intent detection task
            intent_task = asyncio.create_task(
                self._detect_intents(intent_q, turn_models, world_model, system_prompt)
            )

            # Yield events with backpressure control
            async for event in self._yield_events(subs_q, intent_q):
                yield event

            # Wait for tasks to complete
            await asyncio.gather(subtitle_task, intent_task, return_exceptions=True)

        except Exception as e:
            self.logger.error("Error in stream_dialogue", error=str(e))
            yield ProviderEvent(
                type="safety",
                payload={
                    "flag": "error",
                    "detail": f"Stream processing error: {str(e)}",
                    "is_safe": False,
                    "severity": "error",
                    "reason": str(e)
                }
            )
        finally:
            # Cleanup resources in reverse order
            cleanup_tasks = []
            
            if subs_q:
                try:
                    await subs_q.close()
                except Exception as e:
                    self.logger.warning("Error closing subtitle queue", error=str(e))
                    
            if intent_q:
                try:
                    await intent_q.close()
                except Exception as e:
                    self.logger.warning("Error closing intent queue", error=str(e))
                    
            if self.video_source:
                try:
                    await self.video_source.stop()
                    self.logger.info("Video source stopped successfully")
                except Exception as e:
                    self.logger.warning("Error stopping video source", error=str(e))

    def _build_system_prompt(self, world_context: WorldContextModel, system_guidelines: Optional[str] = None) -> str:
        """Build YAML system prompt from world context and guidelines.

        Args:
            world_context: World context information
            system_guidelines: System guidelines

        Returns:
            YAML string representing the system prompt
        """
        prompt_data = {
            "system": {
                "role": "AI Diplomatic Envoy",
                "style": "Formal, period-appropriate (1607–1799), concise",
                "output_format": "YAML intents conforming to protocol v1"
            },
            "world": {
                "counterpart_faction_id": world_context.counterpart_faction.get("id", "unknown"),
                "player_faction_id": world_context.initiator_faction.get("id", "unknown"),
                "war_score": world_context.current_state.get("war_score", 0) if world_context.current_state else 0,
                "borders": world_context.current_state.get("borders", []) if world_context.current_state else []
            },
            "rules": {
                "allowed_kinds": ["PROPOSAL", "CONCESSION", "COUNTER_OFFER", "ULTIMATUM", "SMALL_TALK"],
                "constraints": [
                    "Cannot cede land you do not own or occupy.",
                    "Ultimatums require leverage or superior war score."
                ]
            }
        }

        if system_guidelines:
            prompt_data["system"]["guidelines"] = system_guidelines

        from io import StringIO
        stream = StringIO()
        self.yaml.dump(prompt_data, stream)
        return stream.getvalue()

    async def _stream_subtitles(
        self,
        queue: BoundedAIO,
        turns: List[SpeakerTurnModel],
        system_prompt: str
    ) -> None:
        """Stream subtitle events from player turns.

        # TODO: Wire Gemini Veo3 SDK streaming here for real-time subtitle generation
        # This mock implementation simulates progressive subtitle delivery

        Args:
            queue: Backpressured queue for subtitles
            turns: List of speaker turns
            system_prompt: System prompt for context
        """
        try:
            # Find the last PLAYER turn
            player_turns = [turn for turn in turns if turn.speaker_id.startswith("player")]
            if not player_turns:
                return

            last_turn = player_turns[-1]
            text = last_turn.text

            # Split text into clauses for progressive subtitle streaming
            clauses = self._split_into_clauses(text)

            # Stream interim subtitles
            for i, clause in enumerate(clauses[:-1]):
                subtitle = ProviderEvent(
                    type="subtitle",
                    payload={
                        "text": clause,
                        "start_time": i * 2.0,  # Mock timing
                        "end_time": (i + 1) * 2.0,
                        "speaker_id": last_turn.speaker_id,
                        "is_final": False
                    }
                )
                await queue.put(subtitle)

                # Simulate processing delay
                await asyncio.sleep(0.5)

            # Final subtitle
            if clauses:
                final_subtitle = ProviderEvent(
                    type="subtitle",
                    payload={
                        "text": text,
                        "start_time": 0.0,
                        "end_time": len(clauses) * 2.0,
                        "speaker_id": last_turn.speaker_id,
                        "is_final": True
                    }
                )
                await queue.put(final_subtitle)

        except Exception as e:
            self.logger.error("Error streaming subtitles", error=str(e))

    def _split_into_clauses(self, text: str) -> List[str]:
        """Split text into clauses for progressive subtitle streaming.

        Args:
            text: Text to split

        Returns:
            List of text clauses
        """
        # Simple clause splitting on common delimiters
        clauses = []
        current_clause = ""

        for char in text:
            current_clause += char
            if char in '.!?' and len(current_clause.strip()) > 10:
                clauses.append(current_clause.strip())
                current_clause = ""

        if current_clause.strip():
            clauses.append(current_clause.strip())

        return clauses or [text]  # Fallback to original text

    async def _detect_intents(
        self,
        queue: BoundedAIO,
        turns: List[SpeakerTurnModel],
        world_context: WorldContextModel,
        system_prompt: str
    ) -> None:
        """Detect intents from conversation turns.

        # TODO: Wire Gemini Veo3 SDK streaming here
        # This is currently a mock implementation for testing and development.
        # Replace with actual Gemini API calls when Veo3 SDK is available.

        Args:
            queue: Backpressured queue for intents
            turns: List of speaker turns
            world_context: World context
            system_prompt: System prompt
        """
        try:
            # Get the last turn (typically the most recent)
            if not turns:
                return

            last_turn = turns[-1]

            # Use mock function calling to detect intent
            intent_data = await self._mock_function_call(last_turn.text, system_prompt)

            if intent_data:
                # Parse YAML response
                try:
                    intent_dict = self.yaml.load(intent_data)
                except Exception as e:
                    self.logger.error("Failed to parse YAML intent", error=str(e))
                    return

                # Validate intent based on type
                try:
                    intent_type = intent_dict.get('type', '').lower()
                    schema_name = {
                        'proposal': 'proposal',
                        'concession': 'concession', 
                        'counter_offer': 'counter_offer',
                        'ultimatum': 'ultimatum',
                        'small_talk': 'small_talk'
                    }.get(intent_type, 'small_talk')
                    
                    validated_intent = validator.validate_or_raise(intent_dict, schema_name)
                except Exception as e:
                    self.logger.error("Intent validation failed", error=str(e), intent=intent_dict)
                    return

                # Safety screening
                is_safe, reason = screen_intent(validated_intent)
                if not is_safe:
                    safety_event = ProviderEvent(
                        type="safety",
                        payload={
                            "flag": "content_violation",
                            "detail": reason,
                            "is_safe": False,
                            "severity": "warning"
                        }
                    )
                    await queue.put(safety_event)
                    return

                # Score intent
                scores = score_intent(validated_intent, world_context.model_dump())
                overall_score = sum(scores.values()) / len(scores)

                # Create intent event
                intent_event = ProviderEvent(
                    type="intent",
                    payload={
                        "intent": validated_intent,
                        "confidence": overall_score,
                        "justification": f"Intent detected with scores: {scores}"
                    }
                )
                await queue.put(intent_event)

        except Exception as e:
            self.logger.error("Error detecting intents", error=str(e))

    async def _mock_function_call(self, text: str, system_prompt: str) -> Optional[str]:
        """Mock function calling that returns YAML intent data.

        Args:
            text: Input text to analyze
            system_prompt: System prompt for context

        Returns:
            YAML string representing the detected intent
        """
        # Simulate API delay
        await asyncio.sleep(0.2)

        # Simple pattern matching for demo purposes
        text_lower = text.lower()

        if any(word in text_lower for word in ["trade", "deal", "exchange", "offer"]):
            return f"""
type: proposal
speaker_id: "{last_turn.speaker_id if 'last_turn' in locals() else 'ai_diplomat'}"
content: "I propose a trade agreement based on current diplomatic relations."
intent_type: trade
confidence: 0.85
timestamp: "{datetime.now().isoformat()}"
terms:
  duration: "5 years"
  value: 1000
"""
        elif any(word in text_lower for word in ["concede", "yield", "agree", "accept"]):
            return f"""
type: concession
speaker_id: "ai_diplomat"
content: "I am willing to make concessions in the interest of peace."
concession_type: territorial
value: 25.0
timestamp: "{datetime.now().isoformat()}"
"""
        elif "counter" in text_lower:
            return f"""
type: counter_offer
speaker_id: "ai_diplomat"
content: "I counter with a modified proposal that addresses your concerns."
original_proposal_id: "proposal_123"
confidence: 0.75
timestamp: "{datetime.now().isoformat()}"
counter_terms:
  duration: "3 years"
  value: 800
"""
        elif any(word in text_lower for word in ["or else", "deadline", "final", "ultimatum"]):
            future_time = datetime.now().replace(hour=datetime.now().hour + 1)
            return f"""
type: ultimatum
speaker_id: "ai_diplomat"
content: "This is our final offer - accept it or face the consequences."
deadline: "{future_time.isoformat()}"
timestamp: "{datetime.now().isoformat()}"
consequences:
  - "Trade sanctions"
  - "Military action"
"""
        else:
            return f"""
type: small_talk
speaker_id: "ai_diplomat"
content: "I acknowledge your statement and understand your position."
topic: "general"
timestamp: "{datetime.now().isoformat()}"
"""

    async def _yield_events(
        self,
        subs_q: BoundedAIO,
        intent_q: BoundedAIO
    ) -> AsyncGenerator[ProviderEvent, None]:
        """Yield events from queues with backpressure control.

        Args:
            subs_q: Subtitle queue
            intent_q: Intent queue

        Yields:
            ProviderEvent: Events in correct order
        """
        # First yield all subtitle events
        try:
            async for subtitle_event in subs_q:
                yield subtitle_event
        except StopAsyncIteration:
            pass

        # Then yield intent events
        try:
            async for intent_event in intent_q:
                yield intent_event
        except StopAsyncIteration:
            pass

        # Finally yield analysis event
        yield ProviderEvent(
            type="analysis",
            payload={
                "tag": "conversation_summary",
                "result": {
                    "processing_time_ms": self.latency_target_ms,
                    "confidence": 0.85
                }
            }
        )

    async def validate_and_score_intent(
        self,
        intent: Any,
        world_context: Dict[str, Any]
    ) -> tuple[Any, float, str]:
        """Validate and score intent using schema validation and context-aware scoring.

        Args:
            intent: Intent to validate and score
            world_context: World context for scoring

        Returns:
            Tuple of (validated_intent, confidence_score, justification)
        """
        # First validate using schema validator
        from schemas.validators import validator

        try:
            # Convert datetime to ISO string for schema validation if needed
            if hasattr(intent, 'timestamp') and hasattr(intent.timestamp, 'isoformat'):
                intent_dict = intent.model_dump()
                intent_dict['timestamp'] = intent.timestamp.isoformat()
                
                # Validate against schema using the dict with string timestamp
                validated_dict = validator.validate_intent(intent_dict)
            else:
                # Validate against schema
                validated_dict = validator.validate_intent(intent)

            # Calculate context-aware confidence score
            confidence = self._calculate_confidence_score(intent, world_context)

            # Generate justification based on validation and context
            justification = self._generate_validation_justification(intent, world_context, confidence)

            return intent, confidence, justification

        except Exception as e:
            self.logger.warning(
                "Intent validation failed",
                intent_type=intent.type if hasattr(intent, 'type') else 'unknown',
                error=str(e)
            )
            # Return original intent with low confidence
            return intent, 0.1, f"Validation failed: {str(e)}"

    def _calculate_confidence_score(
        self,
        intent: Any,
        world_context: Dict[str, Any]
    ) -> float:
        """Calculate context-aware confidence score for intent."""
        base_score = 0.9  # Start with high confidence for Veo3 provider

        # Reduce confidence based on context mismatch
        if isinstance(world_context, dict) and 'scenario_tags' in world_context:
            scenario_tags = world_context.get('scenario_tags', [])
            if scenario_tags:
                intent_content = intent.content.lower() if hasattr(intent, 'content') else ''
                intent_keywords = set(intent_content.split())

                context_keywords = set()
                for tag in scenario_tags:
                    context_keywords.update(tag.lower().split())

                # Calculate keyword overlap
                if intent_keywords and context_keywords:
                    overlap = len(intent_keywords.intersection(context_keywords))
                    overlap_ratio = overlap / len(intent_keywords)
                    base_score *= (0.7 + 0.3 * overlap_ratio)  # 0.7 to 1.0 multiplier

        # Reduce confidence for very short content
        if hasattr(intent, 'content') and len(intent.content) < 10:
            base_score *= 0.8

        return min(1.0, max(0.1, base_score))

    def _generate_validation_justification(
        self,
        intent: Any,
        world_context: Dict[str, Any],
        confidence: float
    ) -> str:
        """Generate justification for validation and scoring decision."""
        justifications = []

        # Schema validation
        justifications.append("Schema validation passed")

        # Context awareness
        if confidence > 0.8:
            justifications.append("High context relevance")
        elif confidence > 0.6:
            justifications.append("Moderate context relevance")
        else:
            justifications.append("Low context relevance")

        # Provider type
        justifications.append("Validated by Veo3 provider")

        return ". ".join(justifications)
