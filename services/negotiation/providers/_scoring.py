"""Intent scoring utilities for negotiation providers."""

from typing import Dict, Any, Optional


def score_intent(intent: Dict[str, Any], world_context: Dict[str, Any]) -> Dict[str, float]:
    """Score an intent based on deterministic heuristics.

    Args:
        intent: Intent dictionary
        world_context: World context dictionary

    Returns:
        Dictionary with scores: {"trust": 0..1, "leverage": 0..1, "face_saving": 0..1, "confidence": 0..1}
    
    Raises:
        TypeError: If inputs are not dictionaries
        ValueError: If intent or world_context are empty
    """
    # Input validation
    if not isinstance(intent, dict):
        raise TypeError("intent must be a dictionary")
    if not isinstance(world_context, dict):
        raise TypeError("world_context must be a dictionary")
    
    if not intent:
        raise ValueError("intent cannot be empty")

    # Base scores
    scores = {
        "trust": 0.5,
        "leverage": 0.5,
        "face_saving": 0.5,
        "confidence": 0.5
    }

    try:
        intent_type = intent.get("type", "")

        # Score based on intent type
        type_scores = {
            "proposal": {"trust": 0.7, "leverage": 0.6, "face_saving": 0.4, "confidence": 0.8},
            "counter_offer": {"trust": 0.8, "leverage": 0.7, "face_saving": 0.5, "confidence": 0.9},
            "ultimatum": {"trust": 0.3, "leverage": 0.9, "face_saving": 0.2, "confidence": 0.7},
            "concession": {"trust": 0.9, "leverage": 0.4, "face_saving": 0.8, "confidence": 0.6},
            "small_talk": {"trust": 0.6, "leverage": 0.3, "face_saving": 0.7, "confidence": 0.9}
        }

        if intent_type in type_scores:
            type_score = type_scores[intent_type]
            scores.update(type_score)

        # Adjust based on content analysis
        content = str(intent).lower()

        # Demands vs offers affect trust
        demands_count = content.count("demand") + content.count("require") + content.count("must")
        offers_count = content.count("offer") + content.count("propose") + content.count("suggest")

        if demands_count > offers_count:
            scores["trust"] *= 0.7  # Reduce trust for demanding intents
            scores["leverage"] = min(1.0, scores["leverage"] + 0.1)  # Increase leverage
        elif offers_count > demands_count:
            scores["trust"] = min(1.0, scores["trust"] + 0.1)  # Increase trust for offering intents
            scores["face_saving"] = min(1.0, scores["face_saving"] + 0.1)  # Increase face saving

        # Face-saving clauses
        face_saving_phrases = [
            "willing to", "open to", "consider", "explore", "discuss"
        ]
        face_saving_bonus = 0.0
        for phrase in face_saving_phrases:
            if phrase in content:
                face_saving_bonus += 0.1
        
        scores["face_saving"] = min(1.0, scores["face_saving"] + face_saving_bonus)

        # Confidence based on content quality
        content_length = len(str(intent))
        if 10 <= content_length <= 200:
            scores["confidence"] = min(1.0, scores["confidence"] + 0.1)
        elif content_length < 10:
            scores["confidence"] = max(0.0, scores["confidence"] - 0.2)
        elif content_length > 500:
            scores["confidence"] = max(0.0, scores["confidence"] - 0.1)

        # Context alignment
        scenario_tags = world_context.get("scenario_tags", [])
        if scenario_tags and isinstance(scenario_tags, list):
            context_relevance = 0
            for tag in scenario_tags:
                if isinstance(tag, str) and tag.lower() in content:
                    context_relevance += 1

            if len(scenario_tags) > 0:
                alignment_score = context_relevance / len(scenario_tags)
                scores["trust"] = min(1.0, scores["trust"] + alignment_score * 0.2)
                scores["confidence"] = min(1.0, scores["confidence"] + alignment_score * 0.1)

    except Exception:
        # On any error, return conservative default scores
        scores = {
            "trust": 0.3,
            "leverage": 0.3,
            "face_saving": 0.3,
            "confidence": 0.3
        }

    # Ensure scores are within bounds (defensive programming)
    for key in scores:
        scores[key] = max(0.0, min(1.0, scores[key]))

    return scores


def calculate_overall_score(scores: Dict[str, float]) -> float:
    """Calculate overall score from individual scores.

    Args:
        scores: Individual scores dictionary

    Returns:
        Overall score between 0.0 and 1.0
    
    Raises:
        TypeError: If scores is not a dictionary
        ValueError: If required score keys are missing
    """
    if not isinstance(scores, dict):
        raise TypeError("scores must be a dictionary")
    
    required_keys = {"trust", "leverage", "face_saving", "confidence"}
    if not required_keys.issubset(scores.keys()):
        missing = required_keys - scores.keys()
        raise ValueError(f"Missing required score keys: {missing}")

    # Weighted average favoring trust and confidence
    weights = {
        "trust": 0.3,
        "leverage": 0.2,
        "face_saving": 0.2,
        "confidence": 0.3
    }

    try:
        overall = sum(
            scores[key] * weights[key] 
            for key in weights.keys()
            if key in scores and isinstance(scores[key], (int, float))
        )
        return max(0.0, min(1.0, overall))
    except (TypeError, ValueError):
        # Return conservative default on calculation error
        return 0.3
