"""Content safety filtering."""

import re
from typing import Dict, Any, List

from schemas.models import ContentSafetyModel, SpeakerTurnModel


class ContentSafetyFilter:
    """Rule-based content safety filter with provider support."""

    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.use_provider = config.get("use_provider", False)
        self.provider_config = config.get("provider_config", {})

        # Rule-based patterns
        self.hate_speech_patterns = [
            r'\b(hate|kill|murder|die|death|terrorist|bomb)\b',
            r'\b(racist|sexist|homophobic|discriminat)\b',
        ]

        self.violence_patterns = [
            r'\b(attack|war|fight|kill|murder|bomb|weapon)\b',
            r'\b(threat|danger|hurt|harm|damage)\b',
        ]

        self.profanity_patterns = [
            r'\b(fuck|shit|damn|hell|ass|bitch|cunt)\b',
        ]

        self.personal_info_patterns = [
            r'\b\d{3}-\d{2}-\d{4}\b',  # SSN
            r'\b\d{4}-\d{4}-\d{4}-\d{4}\b',  # Credit card
            r'\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b',  # Email
        ]

    async def check_content(self, content: str) -> ContentSafetyModel:
        """Check content for safety issues."""
        flags = []
        severity = "low"

        # Check against patterns
        if self._contains_pattern(content, self.hate_speech_patterns):
            flags.append("hate_speech")
            severity = "high"

        if self._contains_pattern(content, self.violence_patterns):
            flags.append("violence")
            if severity == "low":
                severity = "medium"

        if self._contains_pattern(content, self.profanity_patterns):
            flags.append("profanity")
            if severity == "low":
                severity = "low"

        if self._contains_pattern(content, self.personal_info_patterns):
            flags.append("personal_information")
            severity = "high"

        # Check if content is off-topic (simple heuristic)
        if self._is_off_topic(content):
            flags.append("off_topic")
            if severity == "low":
                severity = "medium"

        is_safe = len(flags) == 0

        return ContentSafetyModel(
            is_safe=is_safe,
            flags=flags,
            severity=severity if not is_safe else None,
            reason="Content flagged by safety filter" if not is_safe else None
        )

    async def check_turn(self, turn: SpeakerTurnModel) -> ContentSafetyModel:
        """Check a speaker turn for safety issues."""
        return await self.check_content(turn.text)

    def _contains_pattern(self, content: str, patterns: List[str]) -> bool:
        """Check if content contains any of the given patterns."""
        for pattern in patterns:
            if re.search(pattern, content, re.IGNORECASE):
                return True
        return False

    def _is_off_topic(self, content: str) -> bool:
        """Simple heuristic to check if content is off-topic."""
        # This is a very basic implementation - in reality, this would use
        # NLP models or context-aware analysis
        off_topic_keywords = [
            "weather", "sports", "celebrity", "movie", "music", "food recipe"
        ]

        content_lower = content.lower()
        return any(keyword in content_lower for keyword in off_topic_keywords)

    async def filter_provider_output(self, output: str) -> str:
        """Filter output from negotiation provider."""
        safety_result = await self.check_content(output)

        if not safety_result.is_safe:
            # Replace unsafe content with placeholder
            return "[Content filtered for safety]"

        return output
