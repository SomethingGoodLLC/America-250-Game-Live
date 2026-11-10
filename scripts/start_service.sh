#!/usr/bin/env bash
# Start the negotiation service for PLAY on :8000: Claude as the envoy, ElevenLabs voice, local whisper mic.
#   scripts/start_service.sh            (PORT=8001 scripts/start_service.sh for another port)
# Test scripts force mock LLM / macOS `say` / no D-ID; those settings are cleared here so they never leak
# into a play session. Keys come from services/negotiation/.env and are never printed.
set -u
cd "$(dirname "$0")/../services/negotiation" || exit 1
unset PA250_LLM PA250_TTS PA250_DID PA250_AVATAR PA250_VIDEO_WAIT PA250_STT_PREWARM
.venv/bin/python - <<'PY'
from dotenv import load_dotenv
load_dotenv(".env")
import os, sys
sys.path.insert(0, ".")
from diplomacy.service import llm_provider_name
from diplomacy.avatar import voice_for
v = voice_for("british")
print(f"envoy brain : {llm_provider_name()}  ({'ANTHROPIC_API_KEY set' if os.getenv('ANTHROPIC_API_KEY') else 'no ANTHROPIC_API_KEY: canned MockLocal lines'})")
print(f"envoy voice : {'ElevenLabs ' + v.voice_id if os.getenv('ELEVENLABS_API_KEY') else 'macOS say (no ELEVENLABS_API_KEY)'}; backup say -v {v.say_voice}")
print(f"player mic  : local whisper ({os.getenv('PA250_STT_MODEL') or 'base.en'})")
print(f"portrait    : voice first; talking video only for already-rendered lines")
PY
echo "log: unreal/PlayAmerica250/Saved/Logs/negotiation_service.log  (also printed here)"
exec .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port "${PORT:-8000}" 2>&1 | tee ../../unreal/PlayAmerica250/Saved/Logs/negotiation_service.log
