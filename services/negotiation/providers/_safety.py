"""Minimal safety stub - foundation models handle safety internally."""

from typing import Dict, Any, Tuple


def screen_text(text: str) -> Tuple[bool, str]:
    """No-op text screening - foundation models handle safety.
    
    Args:
        text: Text to screen
        
    Returns:
        Always (True, "passed") since foundation models handle safety
    """
    return True, "Foundation model handles safety"


def screen_intent(intent: Dict[str, Any]) -> Tuple[bool, str]:
    """No-op intent screening - foundation models handle safety.
    
    Args:
        intent: Intent dictionary to screen
        
    Returns:
        Always (True, "passed") since foundation models handle safety
    """
    return True, "Foundation model handles safety"


def create_safety_event(flag: str, detail: str) -> Dict[str, Any]:
    """Create a safety event (minimal stub).
    
    Args:
        flag: Safety flag type
        detail: Violation detail
        
    Returns:
        ProviderEvent dictionary
    """
    return {
        "type": "safety",
        "payload": {
            "flag": flag,
            "detail": detail
        }
    }
