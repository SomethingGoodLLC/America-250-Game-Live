"""Mock local provider for offline deterministic behavior."""

import asyncio
import re
from typing import AsyncGenerator, Dict, Any, List, Optional
from datetime import datetime
import structlog

from schemas.models import SpeakerTurnModel, IntentModel, WorldContextModel, ProposalModel, ConcessionModel, CounterOfferModel, UltimatumModel, SmallTalkModel
from .base import Provider, ProviderEvent, NewIntent, LiveSubtitle, Analysis, Safety


class MockLocalProvider(Provider):
    """Mock provider that generates deterministic responses based on key phrases.

    This provider implements a deterministic state machine that translates
    specific key phrases in the last PLAYER turn into diplomatic intents:

    - "We'll grant trade access if you withdraw troops" → CounterOffer
    - "Ceasefire now or else" → Ultimatum
    - Otherwise: small talk + low-stakes proposal
    """

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.strict = config.get("strict", False)
        self.logger = structlog.get_logger(__name__)

        # Pre-compiled regex patterns for efficient matching
        self._patterns = {
            "counter_offer": re.compile(r"grant.*access.*if.*withdraw.*troops", re.IGNORECASE),
            "ultimatum": re.compile(r"ceasefire.*now.*or else|deadline.*final", re.IGNORECASE),
            "trade": re.compile(r"trade|deal|exchange", re.IGNORECASE),
            "aggressive": re.compile(r"war|attack|threaten|destroy", re.IGNORECASE),
            "cooperative": re.compile(r"peace|alliance|cooperate|help", re.IGNORECASE),
        }

    async def stream_dialogue(
        self,
        turns: List[SpeakerTurnModel],
        world_context: WorldContextModel,
        system_guidelines: Optional[str] = None
    ) -> AsyncGenerator[ProviderEvent, None]:
        """Stream deterministic dialogue processing based on key phrases."""

        # Always emit safety check first
        yield Safety(
            is_safe=True,
            flags=["deterministic_mode"],
            severity="info",
            reason="Operating in deterministic mock mode"
        )

        if not turns:
            # Initial greeting - no player turns yet
            yield NewIntent(
                intent=SmallTalkModel(
                    type="small_talk",
                    speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
                    content="Greetings. I understand we have matters to discuss regarding our diplomatic relations.",
                    topic="diplomatic_relations",
                    timestamp=datetime.now()
                ),
                confidence=1.0,
                justification="Initial greeting in diplomatic negotiations"
            )
            return

        # Analyze the last PLAYER turn (assuming player is the initiator)
        last_turn = turns[-1]
        if last_turn.speaker_id != world_context.initiator_faction.get("id"):
            # This is an AI turn, just acknowledge and continue
            yield Analysis(
                analysis_type="turn_analysis",
                result={
                    "turn_type": "ai_response",
                    "content_length": len(last_turn.text),
                    "sentiment": "neutral"
                },
                confidence=0.8
            )
            return

        # Process player turn for intent detection
        text = last_turn.text

        # Check for strict mode violations
        if self.strict and self._contains_unsafe_content(text):
            yield Safety(
                is_safe=False,
                flags=["unsafe_content"],
                severity="high",
                reason="Strict mode detected potentially unsafe content"
            )
            # Still yield analysis even in strict mode
            yield Analysis(
                analysis_type="strict_mode_violation",
                result={
                    "blocked_content": text[:50] + "..." if len(text) > 50 else text,
                    "violation_type": "unsafe_content",
                    "processing_mode": "strict"
                },
                confidence=0.9
            )
            return

        # Deterministic intent detection based on key phrases
        intent = await self._detect_intent_from_text(text, last_turn, world_context)

        # Generate live subtitles for the player turn
        yield LiveSubtitle(
            text=text[:len(text)//2] + "...",
            start_time=0.0,
            end_time=0.5,
            speaker_id=last_turn.speaker_id,
            is_final=False
        )
        
        # Simulate processing delay
        await asyncio.sleep(0.1)
        
        yield LiveSubtitle(
            text=text,
            start_time=0.0,
            end_time=1.0,
            speaker_id=last_turn.speaker_id,
            is_final=True
        )

        if intent:
            # Validate and score the intent
            validated_intent, confidence, justification = await self.validate_and_score_intent(intent, world_context)
            yield NewIntent(
                intent=validated_intent,
                confidence=confidence,
                justification=justification
            )

        # Always yield analysis
        yield Analysis(
            analysis_type="deterministic_analysis",
            result={
                "matched_patterns": self._get_matched_patterns(text),
                "intent_detected": intent.type if intent else "none",
                "processing_mode": "strict" if self.strict else "permissive"
            },
            confidence=0.85
        )

    async def _detect_intent_from_text(
        self,
        text: str,
        turn: SpeakerTurnModel,
        world_context: WorldContextModel
    ) -> Optional[IntentModel]:
        """Detect intent based on deterministic key phrase matching."""

        # Priority order: counter_offer > ultimatum > other patterns

        if self._patterns["counter_offer"].search(text):
            return CounterOfferModel(
                type="counter_offer",
                speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
                content="We will grant trade access to your merchants if you withdraw your military forces from the disputed territories.",
                original_proposal_id="player_trade_proposal_1",
                counter_terms={
                    "trade_access_granted": True,
                    "withdrawal_required": True,
                    "territories": ["northern_border", "southern_pass"],
                    "duration": "2_years"
                },
                confidence=1.0,
                timestamp=datetime.now()
            )

        if self._patterns["ultimatum"].search(text):
            return UltimatumModel(
                type="ultimatum",
                speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
                content="Cease fire immediately or face severe consequences. This is our final warning.",
                deadline=datetime.now().replace(hour=datetime.now().hour + 1),
                consequences=[
                    "Full military mobilization",
                    "Trade embargo",
                    "Alliance termination"
                ],
                timestamp=datetime.now()
            )

        # Check for other patterns
        if self._patterns["trade"].search(text):
            return ProposalModel(
                type="proposal",
                speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
                content="I propose we establish a basic trade agreement to improve our economic relations.",
                intent_type="trade",
                terms={
                    "trade_volume": 500,
                    "duration": "1_year",
                    "goods": ["grain", "textiles"]
                },
                confidence=0.9,
                timestamp=datetime.now()
            )

        if self._patterns["aggressive"].search(text):
            return UltimatumModel(
                type="ultimatum",
                speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
                content="We cannot tolerate such aggressive rhetoric. Cease immediately or face diplomatic isolation.",
                deadline=datetime.now().replace(hour=datetime.now().hour + 2),
                consequences=["Diplomatic isolation", "Economic sanctions"],
                timestamp=datetime.now()
            )

        if self._patterns["cooperative"].search(text):
            return ConcessionModel(
                type="concession",
                speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
                content="I am willing to consider cooperative measures to resolve our differences.",
                concession_type="diplomatic",
                value=25.0,
                timestamp=datetime.now()
            )

        # Default: small talk with low-stakes proposal
        return SmallTalkModel(
            type="small_talk",
            speaker_id=world_context.counterpart_faction.get("id", "ai_diplomat"),
            content="I understand your position. Perhaps we can discuss this matter further in a more constructive manner.",
            topic="diplomatic_relations",
            timestamp=datetime.now()
        )

    def _contains_unsafe_content(self, text: str) -> bool:
        """Check for unsafe content in strict mode."""
        unsafe_patterns = [
            r"hate|discriminat|racist|sexist",
            r"violent|kill|murder|assassinat|violence",
            r"threat|bomb|weapon|attack|war"
        ]

        return any(re.search(pattern, text, re.IGNORECASE) for pattern in unsafe_patterns)

    def _get_matched_patterns(self, text: str) -> List[str]:
        """Get list of matched pattern names."""
        matched = []
        for name, pattern in self._patterns.items():
            if pattern.search(text):
                matched.append(name)
        return matched

    async def validate_intent(self, intent: IntentModel) -> bool:
        """Validate intent in deterministic mode."""
        try:
            # Basic schema validation
            intent.model_validate(intent.model_dump())

            if self.strict:
                # In strict mode, only allow safe, schema-compliant intents
                return self._is_intent_safe(intent)
            return True
        except Exception:
            return False

    async def validate_and_score_intent(
        self,
        intent: IntentModel,
        world_context: WorldContextModel
    ) -> tuple[IntentModel, float, str]:
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
            # Convert datetime to ISO string for schema validation
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
                intent_type=intent.type,
                error=str(e)
            )
            # Return original intent with low confidence
            return intent, 0.1, f"Validation failed: {str(e)}"

    def _calculate_confidence_score(
        self,
        intent: IntentModel,
        world_context: WorldContextModel
    ) -> float:
        """Calculate context-aware confidence score for intent."""
        base_score = 0.8  # Start with high confidence for mock provider

        # Reduce confidence based on context mismatch
        if world_context.scenario_tags:
            intent_keywords = set(intent.content.lower().split())
            context_keywords = set(" ".join(world_context.scenario_tags).lower().split())

            # Calculate keyword overlap
            if intent_keywords and context_keywords:
                overlap = len(intent_keywords.intersection(context_keywords))
                overlap_ratio = overlap / len(intent_keywords)
                base_score *= (0.5 + 0.5 * overlap_ratio)  # 0.5 to 1.0 multiplier

        # Reduce confidence for very short content
        if len(intent.content) < 10:
            base_score *= 0.7

        return min(1.0, max(0.1, base_score))

    def _generate_validation_justification(
        self,
        intent: IntentModel,
        world_context: WorldContextModel,
        confidence: float
    ) -> str:
        """Generate justification for validation and scoring decision."""
        justifications = []

        # Schema validation
        justifications.append("Schema validation passed")

        # Context awareness
        if confidence > 0.8:
            justifications.append("High context relevance")
        elif confidence > 0.5:
            justifications.append("Moderate context relevance")
        else:
            justifications.append("Low context relevance")

        # Pattern matching
        if hasattr(intent, 'content'):
            matched_patterns = self._get_matched_patterns(intent.content)
            if matched_patterns:
                justifications.append(f"Pattern matches: {', '.join(matched_patterns)}")

        return ". ".join(justifications)

    def _is_intent_safe(self, intent: IntentModel) -> bool:
        """Check if intent is safe for strict mode."""
        try:
            # Basic safety checks
            if hasattr(intent, 'consequences'):
                # Check for extreme consequences
                extreme_consequences = ["destroy", "annihilate", "genocide", "exterminate"]
                consequences_str = str(getattr(intent, 'consequences', [])).lower()
                if any(extreme in consequences_str for extreme in extreme_consequences):
                    return False

            # Check content for safety
            if hasattr(intent, 'content'):
                content = getattr(intent, 'content', '')
                if self._contains_unsafe_content(content):
                    return False

            return True
        except Exception:
            # If we can't validate safety, err on the side of caution
            return False
