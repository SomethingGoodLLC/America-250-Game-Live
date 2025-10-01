#!/usr/bin/env python3
"""
Test script for Revolutionary War diplomatic avatars
"""

from diplomatic_avatar import DiplomaticAvatar, create_character_avatar

# Test 1: Single character speech
def test_washington_speech():
    print("=" * 60)
    print("TEST 1: George Washington Speech")
    print("=" * 60)
    
    avatar = create_character_avatar("george_washington")
    
    video = avatar.speak(
        text="We shall secure liberty for these colonies, "
             "or we shall perish in the attempt.",
        output_name="washington_liberty_speech",
        emotion="diplomatic",
        still_mode=True,  # Formal speech posture
        enhance_video=True
    )
    
    print(f"\n✅ Washington video: {video}\n")


# Test 2: Negotiation dialogue
def test_diplomatic_negotiation():
    print("=" * 60)
    print("TEST 2: Franklin-Cornwallis Negotiation")
    print("=" * 60)
    
    franklin = create_character_avatar("benjamin_franklin")
    cornwallis = create_character_avatar("lord_cornwallis")
    
    # Franklin's opening
    franklin_video = franklin.speak(
        text="Your Lordship, perhaps we can reach an accord "
             "that benefits both our nations.",
        output_name="franklin_negotiation_01",
        still_mode=False  # Natural conversation movement
    )
    
    # Cornwallis's response
    cornwallis_video = cornwallis.speak(
        text="I am prepared to listen, Doctor Franklin, "
             "though His Majesty's terms are quite clear.",
        output_name="cornwallis_negotiation_01",
        emotion="diplomatic"
    )
    
    print(f"\n✅ Negotiation videos generated:")
    print(f"   Franklin: {franklin_video}")
    print(f"   Cornwallis: {cornwallis_video}\n")


# Test 3: Quick test with single portrait
def quick_test():
    """
    Minimal test - just provide a portrait path
    """
    avatar = DiplomaticAvatar(
        portrait_path="test_portrait.jpg",  # Your test image
        voice_id="YOUR_VOICE_ID"
    )
    
    video = avatar.speak(
        text="This is a test of the diplomatic avatar system.",
        output_name="quick_test"
    )
    
    print(f"Video: {video}")


if __name__ == "__main__":
    # Run tests
    import sys
    
    if len(sys.argv) > 1:
        test_name = sys.argv[1]
        if test_name == "washington":
            test_washington_speech()
        elif test_name == "negotiation":
            test_diplomatic_negotiation()
        elif test_name == "quick":
            quick_test()
    else:
        print("Usage: python test_diplomatic_avatar.py [washington|negotiation|quick]")


