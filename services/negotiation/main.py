# /services/negotiation/app.py
from __future__ import annotations
import asyncio
import os
import uuid
import time
from datetime import datetime
from typing import Dict, Any
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import HTMLResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketState
from ruamel.yaml import YAML
from pydantic import BaseModel
from aiortc import RTCPeerConnection
from aiortc.contrib.media import MediaBlackhole
from webrtc.publish import attach_avatar_track_simple as attach_avatar_track
from providers.mock_local import MockLocalProvider
from providers.gemini_veo3 import Veo3Provider  # stub uses placeholder video unless USE_VEO3=1
from schemas.validators import validator
from schemas.models import SpeakerTurnModel, WorldContextModel
from listeners.base import make_listener_from_env
from tts.elevenlabs import ElevenLabsProvider
import structlog
from core.logging_config import setup_logging
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# Setup logging
setup_logging()
logger = structlog.get_logger(__name__)

yaml = YAML()

app = FastAPI(title="Negotiation Service (YAML/WebRTC)")
app.mount("/assets", StaticFiles(directory="assets"), name="assets")

SESSIONS: Dict[str, Dict[str, Any]] = {}

DEFAULT_WORLD_CONTEXT = {
    "scenario_tags": ["default_scenario"],
    "initiator_faction": {"id": "player::default", "name": "Player Faction"},
    "counterpart_faction": {"id": "ai::default", "name": "AI Faction"},
    "current_state": {},
}


def sanitize_world_context(raw: Dict[str, Any] | None, session_id: str) -> Dict[str, Any]:
    ctx = raw.copy() if isinstance(raw, dict) else {}
    scenario_tags = ctx.get("scenario_tags")
    if not isinstance(scenario_tags, list) or not scenario_tags:
        scenario_tags = DEFAULT_WORLD_CONTEXT["scenario_tags"].copy()
    ctx["scenario_tags"] = scenario_tags

    initiator = ctx.get("initiator_faction") if isinstance(ctx.get("initiator_faction"), dict) else {}
    initiator.setdefault("name", DEFAULT_WORLD_CONTEXT["initiator_faction"]["name"])
    initiator_id = initiator.get("id") or f"player::{session_id}"
    initiator["id"] = initiator_id
    ctx["initiator_faction"] = initiator

    counterpart = ctx.get("counterpart_faction") if isinstance(ctx.get("counterpart_faction"), dict) else {}
    counterpart.setdefault("name", DEFAULT_WORLD_CONTEXT["counterpart_faction"]["name"])
    counterpart_id = counterpart.get("id") or f"ai::{session_id}"
    counterpart["id"] = counterpart_id
    ctx["counterpart_faction"] = counterpart

    current_state = ctx.get("current_state") if isinstance(ctx.get("current_state"), dict) else {}
    ctx["current_state"] = current_state

    return ctx

class SDPIn(BaseModel):
    sdp: str
    type: str = "offer"

def _dump_yaml(obj: Any) -> str:
    from io import StringIO
    buf = StringIO()
    yaml.dump(obj, buf)
    return buf.getvalue()

async def generate_ai_response(sess: dict, user_text: str, send_yaml_func):
    """Generate AI response to user input and trigger avatar generation."""
    try:
        logger.info("Generating AI response", user_text=user_text)
        if sess["model"] == "veo3":
            provider = Veo3Provider(use_veo3=True)
        else:
            # Use MockLocalProvider for both "mock_local" and "teller" models
            # The difference is in avatar generation, not AI logic
            provider = MockLocalProvider({"strict": True})
        turns = [turn if isinstance(turn, SpeakerTurnModel) else SpeakerTurnModel(**turn) for turn in sess["turns"][-5:]]
        world_context_dict = sanitize_world_context(sess.get("world_context"), sess.get("session_id", "default"))
        world_context = WorldContextModel(**world_context_dict)
        await send_yaml_func({"type": "ai_thinking", "text": "🤔 AI is thinking..."})
        async for ev in provider.stream_dialogue(
            turns=turns,
            world_context=world_context,
            system_guidelines="You are a diplomatic AI negotiator. Respond conversationally to the human's statement. Be engaging and strategic."
        ):
            logger.info("Provider event received", event_type=ev.type, payload_keys=list(ev.payload.keys()) if ev.payload else [])
            
            if ev.type == "subtitle":
                logger.info("Sending AI response", text=ev.payload.get("text", ""), final=ev.is_final)
                await send_yaml_func({"type": "ai_response", "text": ev.payload.get("text", ""), "final": ev.is_final})
                if ev.is_final and ev.payload.get("text", "").strip():
                    ai_text = ev.payload.get("text", "").strip()
                    ai_turn = {
                        "speaker": "AI",
                        "text": ai_text,
                        "speaker_id": world_context_dict["counterpart_faction"]["id"],
                        "timestamp": datetime.utcnow().isoformat(),
                    }
                    sess["turns"].append(ai_turn)
                    logger.info("Added AI turn to session", ai_text=ai_text)
                    
                    # Generate avatar based on model type
                    logger.info("Generating avatar for model type", model_type=sess["model"])
                    if sess["model"] == "veo3":
                        await generate_veo3_avatar(ai_text, send_yaml_func)
                    elif sess["model"] == "teller":
                        logger.info("Calling generate_teller_avatar for TTS audio")
                        await generate_teller_avatar(ai_text, send_yaml_func, sess.get("session_id"))
                        
            elif ev.type == "intent":
                logger.info("Sending AI intent", intent_type=ev.payload.get("intent", {}).get("type", "unknown"))
                await send_yaml_func({"type": "ai_intent", "payload": ev.payload})
    except Exception as e:
        logger.error("Error generating AI response", error=str(e))
        await send_yaml_func({"type": "ai_response", "text": "I apologize, I'm having trouble processing that. Could you rephrase?", "final": True})

async def generate_veo3_avatar(ai_text: str, send_yaml_func):
    try:
        logger.info("Generating Veo3 avatar", text=ai_text)
        veo3 = Veo3Provider(use_veo3=True)
        await send_yaml_func({"type": "avatar_generating", "text": "🎬 Generating avatar response..."})
        await asyncio.sleep(2)
        await send_yaml_func({"type": "avatar_ready", "text": f"🎭 Avatar ready: {ai_text}", "video_url": "/api/avatar/latest.mp4", "final": True})
    except Exception as e:
        logger.error("Error generating Veo3 avatar", error=str(e))

async def generate_and_save_tts_audio(tts_provider, text: str, job_id: str, send_yaml_func):
    """Generate TTS audio and save to file for verification."""
    try:
        logger.info("Generating and saving TTS audio", job_id=job_id, text_length=len(text))

        # Send job started status
        await send_yaml_func({
            "type": "tts_job_started",
            "job_id": job_id,
            "text": text
        })

        # Generate audio data
        audio_data = None
        async for chunk in tts_provider.synthesize_speech(text):
            audio_data = chunk
            break

        if audio_data:
            # Save audio to file
            audio_dir = "generated_audio"
            os.makedirs(audio_dir, exist_ok=True)
            audio_file_path = f"{audio_dir}/{job_id}.wav"

            # Convert to WAV format and save
            import wave
            import io

            # Create WAV file in memory
            with io.BytesIO() as wav_buffer:
                with wave.open(wav_buffer, 'wb') as wav_file:
                    wav_file.setnchannels(1)  # Mono
                    wav_file.setsampwidth(2)  # 16-bit
                    # Use the TTS provider's actual sample rate instead of hardcoded 16kHz
                    sample_rate = getattr(tts_provider, 'sample_rate', 16000)
                    wav_file.setframerate(sample_rate)  # Match TTS provider sample rate
                    wav_file.writeframes(audio_data)

                # Save to file
                with open(audio_file_path, 'wb') as f:
                    f.write(wav_buffer.getvalue())

            logger.info("✅ TTS AUDIO FILE GENERATED", file_path=audio_file_path, size=len(audio_data), job_id=job_id)
            print(f"\n🎵 TTS AUDIO GENERATED: {audio_file_path}")
            print(f"📁 File size: {len(audio_data)} bytes")
            print(f"🔗 You can play this file at: http://localhost:8000/{audio_file_path}\n")
            return audio_file_path
        else:
            logger.error("No audio data generated from TTS provider")
            return None

    except Exception as e:
        logger.error("Failed to generate and save TTS audio", error=str(e))
        return None


async def create_audio_track_from_file(file_path: str, tts_provider):
    """Create an audio track from a saved audio file."""
    try:
        logger.info("Creating audio track from file", file_path=file_path)

        # Read the WAV file and extract PCM data properly
        import wave
        with wave.open(file_path, 'rb') as wav_file:
            # Verify the file format matches expectations
            channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
            frame_rate = wav_file.getframerate()
            n_frames = wav_file.getnframes()
            
            logger.info("WAV file info", 
                       channels=channels, 
                       sample_width=sample_width, 
                       frame_rate=frame_rate, 
                       n_frames=n_frames)
            
            # Read the raw PCM data (without WAV header)
            pcm_data = wav_file.readframes(n_frames)

        # Create audio track from the TTS provider
        audio_track = await tts_provider.get_audio_track("loaded_from_file", "default")

        # Replace the audio data in the track with the extracted PCM data
        if hasattr(audio_track, 'audio_data'):
            audio_track.audio_data = pcm_data
            audio_track.position = 0  # Reset position
            logger.info("Updated audio track with file data", data_size=len(pcm_data))

        logger.info("Created audio track from saved file", track_type=type(audio_track).__name__)
        return audio_track

    except Exception as e:
        logger.error("Failed to create audio track from file", error=str(e))
        # Return a basic track as fallback
        return await tts_provider.get_audio_track("Error loading audio file", "default")


async def generate_teller_avatar(ai_text: str, send_yaml_func, session_id: str = None):
    """Generate Teller Avatar animation for AI speech with audio."""
    try:
        logger.info("Generating Teller avatar", text=ai_text)

        # Send avatar generation status
        await send_yaml_func({
            "type": "avatar_generating",
            "text": "🎭 Teller Avatar animating...",
            "final": False
        })

        # Replace placeholder audio track with actual TTS audio
        if session_id and session_id in SESSIONS:
            sess = SESSIONS[session_id]
            tts_provider = sess.get("tts_provider")
            pc = sess.get("pc")

            logger.info("TTS setup check", has_tts_provider=tts_provider is not None, has_peer_connection=pc is not None)

            if tts_provider and pc:
                try:
                    logger.info("Generating TTS audio for AI response", text=ai_text[:50])
                    # Get audio track from TTS provider
                    audio_track = await tts_provider.get_audio_track(ai_text, "default")
                    logger.info("Got audio track from TTS provider", track_type=type(audio_track))

                    # Update the audio track with new TTS data using job system
                    if "audio_track" in sess and "tts_provider" in sess:
                        logger.info("Starting TTS job for audio generation")
                        audio_track = sess["audio_track"]
                        tts_provider = sess["tts_provider"]

                        # Create a job ID for this TTS request
                        job_id = f"tts_{session_id}_{int(time.time())}"
                        sess["current_tts_job"] = job_id

                        # Generate audio asynchronously and save to file
                        audio_file_path = await generate_and_save_tts_audio(
                            tts_provider, ai_text, job_id, send_yaml_func
                        )

                        if audio_file_path:
                            # Load the saved audio file and create audio track
                            new_audio_track = await create_audio_track_from_file(
                                audio_file_path, tts_provider
                            )
                            logger.info("Created audio track from saved file")

                            # Replace the track in the sender
                            audio_sender = sess["audio_sender"]
                            logger.info("Replacing track in sender", old_track_id=id(audio_sender.track), new_track_id=id(new_audio_track))
                            audio_sender.replaceTrack(new_audio_track)
                            sess["audio_track"] = new_audio_track  # Update stored reference
                            logger.info("Successfully updated audio track with AI response from file")

                            # Send job completion status
                            await send_yaml_func({
                                "type": "tts_job_completed",
                                "job_id": job_id,
                                "audio_file": audio_file_path,
                                "text": ai_text
                            })
                        else:
                            logger.error("Failed to generate and save TTS audio")
                            await send_yaml_func({
                                "type": "tts_job_failed",
                                "job_id": job_id,
                                "error": "Audio generation failed"
                            })
                    else:
                        logger.error("No stored audio track or TTS provider found for update")

                except Exception as tts_error:
                    logger.error("Failed to generate TTS audio", error=str(tts_error), error_type=type(tts_error))
                    import traceback
                    logger.error("TTS error traceback", traceback=traceback.format_exc())

        # Simulate processing time (in real implementation, this would trigger TTS + animation)
        await asyncio.sleep(0.5)

        # Send avatar ready status
        await send_yaml_func({
            "type": "avatar_ready",
            "text": f"🎬 Teller Avatar speaking: {ai_text}",
            "animation_data": {
                "text": ai_text,
                "duration": len(ai_text) * 100,  # Estimate speaking duration
                "emotion": "diplomatic"
            },
            "final": True
        })

    except Exception as e:
        logger.error("Error generating Teller avatar", error=str(e))

@app.get("/", response_class=HTMLResponse)
async def root():
    # Serve the enhanced test page
    with open("web/enhanced_test_client.html", "r", encoding="utf-8") as f:
        return HTMLResponse(f.read())

@app.get("/teller-avatar-component.html", response_class=HTMLResponse)
async def teller_avatar_component():
    # Serve the Teller Avatar component
    with open("web/teller-avatar-component.html", "r", encoding="utf-8") as f:
        return HTMLResponse(f.read())

from fastapi.staticfiles import StaticFiles

# Create generated audio directory if it doesn't exist
os.makedirs("generated_audio", exist_ok=True)

# Mount the teller-avatar directory to serve videos
app.mount("/teller-avatar", StaticFiles(directory="teller-avatar"), name="teller-avatar")
# Mount the generated audio directory
app.mount("/generated_audio", StaticFiles(directory="generated_audio"), name="generated-audio")

@app.post("/v1/session")
async def create_session(request: Request):
    try:
        body = await request.json()
    except:
        body = {}
    session_id = str(uuid.uuid4())[:8]
    model = body.get("model", "teller")  # Default to teller for TTS functionality
    logger.info("Session creation request", session_id=session_id, requested_model=model, body_keys=list(body.keys()) if body else [])
    pc = RTCPeerConnection()
    world_context = sanitize_world_context(body.get("world_context"), session_id)
    initiator_id = world_context["initiator_faction"]["id"]
    counterpart_id = world_context["counterpart_faction"]["id"]
    # NOTE: we attach avatar track after SDP answer (in /offer)
    # TTS provider will be created during WebRTC setup
    SESSIONS[session_id] = {
        "pc": pc,
        "model": model,
        "ws_clients": set(),
        "turns": [],
        "world_context": world_context,
        "initiator_id": initiator_id,
        "counterpart_id": counterpart_id,
        "provider_task": None,
        "provider_tasks": [],
        "blackhole": MediaBlackhole(),
        "listener": None,
        "session_id": session_id,
        "tts_provider": None,  # Will be set during WebRTC setup
    }
    return {"session_id": session_id}

@app.post("/v1/session/{sid}/webrtc/offer")
async def sdp_offer(sid: str, sdp_in: SDPIn):
    sess = SESSIONS[sid]
    pc: RTCPeerConnection = sess["pc"]

    # Create listener adapter based on environment
    listener = make_listener_from_env()
    sess["listener"] = listener
    await listener.start()

    # Remote audio → listener adapter for real-time processing
    @pc.on("track")
    async def on_track(track):
        if track.kind == "audio":
            try:
                while True:
                    try:
                        frame = await track.recv()
                        # Convert frame to PCM16 bytes
                        if hasattr(frame, 'data'):
                            # Process frame data
                            audio_data = frame.data
                            # Convert to 16-bit PCM if needed
                            if hasattr(audio_data, 'tobytes'):
                                pcm_bytes = audio_data.tobytes()
                            else:
                                # Assume it's already bytes
                                pcm_bytes = bytes(audio_data)

                            # Feed to listener
                            await listener.feed_pcm(pcm_bytes, int(time.time() * 1000))
                    except Exception as e:
                        logger.debug("Audio frame processing error", error=str(e))
                        break
            except Exception as e:
                logger.error("Audio track error", error=str(e))

    # Validate and set remote description
    from aiortc import RTCSessionDescription
    from fastapi import HTTPException
    
    # Basic SDP validation
    if not sdp_in.sdp or not sdp_in.sdp.strip():
        logger.error("Empty SDP offer received")
        raise HTTPException(status_code=400, detail="SDP offer cannot be empty")
        
    if not sdp_in.sdp.startswith("v="):
        logger.error("Invalid SDP format - missing version line")
        raise HTTPException(status_code=400, detail="Invalid SDP format")
    
    # Required SDP lines for a basic offer
    required_lines = ["o=", "s=", "m="]
    for req_line in required_lines:
        if req_line not in sdp_in.sdp:
            logger.error(f"Missing required SDP line: {req_line}")
            raise HTTPException(status_code=400, detail=f"Missing required SDP line: {req_line}")
    
    try:
        # Use string type directly (aiortc accepts strings)
        remote_desc = RTCSessionDescription(sdp=sdp_in.sdp, type=sdp_in.type)
        await pc.setRemoteDescription(remote_desc)
        logger.info("Remote description set successfully")
    except Exception as e:
        logger.error("Failed to set remote description", error=str(e))
        raise HTTPException(status_code=400, detail=f"Failed to set remote description: {str(e)}")

    # Add audio track during initial setup for TTS functionality
    # Video track will be added after connection to avoid aiortc direction bug
    logger.info("Adding audio track for TTS functionality during WebRTC setup")

    # Create TTS provider and audio track for this session
    # Try to get 11Labs API key from environment
    elevenlabs_api_key = os.getenv("ELEVENLABS_API_KEY", "")
    if elevenlabs_api_key:
        tts_provider = ElevenLabsProvider({
            "api_key": elevenlabs_api_key,
            "voice_id": "JBFqnCBsd6RMkjVDRZzb",  # George voice (from your example)
            "model": "eleven_multilingual_v2"
        })
        logger.info("Using ElevenLabs TTS provider")
    else:
        # Fallback to simple TTS if no API key
        from tts.xtts import XTTSProvider
        tts_provider = XTTSProvider({"device": "cpu", "model_path": "models/xtts"})
        logger.info("Using fallback XTTS provider - no ElevenLabs API key found")

    sess["tts_provider"] = tts_provider

    # Create the actual TTS audio track for the AI avatar
    # We'll use a short placeholder text and replace it when we have the actual AI response
    initial_text = "Initializing avatar audio system..."
    tts_audio_track = await tts_provider.get_audio_track(initial_text, "default")
    logger.info("Created TTS audio track", track_kind=tts_audio_track.kind, track_id=id(tts_audio_track))

    # Add the track to the peer connection
    sender = pc.addTrack(tts_audio_track)
    logger.info("Added TTS audio track to peer connection", track_kind=tts_audio_track.kind, sender_id=id(sender))

    # Verify the track was added correctly
    audio_senders = [s for s in pc.getSenders() if s.track and s.track.kind == "audio"]
    logger.info("Audio senders after adding track", sender_count=len(audio_senders), track_ids=[id(s.track) for s in audio_senders])

    # Force the track to start generating frames by calling recv() once
    try:
        logger.info("Testing audio track frame generation")
        test_frame = await tts_audio_track.recv()
        if test_frame:
            logger.info("Audio track test frame generated successfully", frame_type=type(test_frame).__name__)
        else:
            logger.warning("Audio track test frame returned None")
    except Exception as test_error:
        logger.warning("Audio track test frame generation failed", error=str(test_error))

    # Store references for later updates
    sess["audio_track"] = tts_audio_track
    sess["audio_sender"] = sender
    sess["tts_provider"] = tts_provider

    # Create and set local description with enhanced error handling
    try:
        answer = await pc.createAnswer()
        await pc.setLocalDescription(answer)
        logger.info("Local description set successfully")
        # Return the answer SDP as JSON (browser expects JSON)
        return {"type": answer.type, "sdp": answer.sdp}
        
    except ValueError as e:
        if "None is not in list" in str(e):
            logger.error("aiortc direction bug encountered - attempting workaround", error=str(e))
            # Try to create a new RTCPeerConnection and retry
            try:
                # Create fresh peer connection
                new_pc = RTCPeerConnection()
                
                # Re-attach the same event handlers
                @new_pc.on("track")
                async def on_track_retry(track):
                    if track.kind == "audio":
                        try:
                            while True:
                                try:
                                    frame = await track.recv()
                                    if hasattr(frame, 'data'):
                                        audio_data = frame.data
                                        if hasattr(audio_data, 'tobytes'):
                                            pcm_bytes = audio_data.tobytes()
                                        else:
                                            pcm_bytes = bytes(audio_data)
                                        await listener.feed_pcm(pcm_bytes, int(time.time() * 1000))
                                except Exception as e:
                                    logger.debug("Audio frame processing error in retry", error=str(e))
                                    break
                        except Exception as e:
                            logger.error("Audio track error in retry", error=str(e))
                
                # Set remote description on new connection
                from aiortc import RTCSessionDescription
                remote_desc = RTCSessionDescription(sdp=sdp_in.sdp, type=sdp_in.type)
                await new_pc.setRemoteDescription(remote_desc)
                
                # Skip video track on retry as well
                logger.info("Skipping video track on retry to avoid direction bug")
                
                # Create answer with new connection
                answer = await new_pc.createAnswer()
                await new_pc.setLocalDescription(answer)
                
                # Replace the old peer connection
                sess["pc"] = new_pc
                
                logger.info("Successfully created answer with fresh RTCPeerConnection")
                return {"type": answer.type, "sdp": answer.sdp}
                
            except Exception as retry_error:
                logger.error("Retry with fresh RTCPeerConnection failed", error=str(retry_error))
                from fastapi import HTTPException
                raise HTTPException(status_code=500, detail=f"WebRTC connection failed: {str(retry_error)}")
        else:
            logger.error("Failed to create/set local description", error=str(e))
            from fastapi import HTTPException
            raise HTTPException(status_code=400, detail=f"ValueError in local description: {str(e)}")
    except Exception as e:
        logger.error("Failed to create/set local description", error=str(e))
        from fastapi import HTTPException
        raise HTTPException(status_code=500, detail=f"Failed to create local description: {str(e)}")

@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "ok", "service": "negotiation", "version": "1.0.0"}

@app.post("/test-session-teller")
async def test_session_teller():
    """Test endpoint to create a session with teller model directly."""
    session_id = str(uuid.uuid4())[:8]
    model = "teller"
    pc = RTCPeerConnection()

    # Create TTS provider
    elevenlabs_api_key = os.getenv("ELEVENLABS_API_KEY", "")
    if elevenlabs_api_key:
        tts_provider = ElevenLabsProvider({
            "api_key": elevenlabs_api_key,
            "voice_id": "21m00Tcm4TlvDq8ikWAM",
            "model": "eleven_monolingual_v1"
        })
    else:
        from tts.xtts import XTTSProvider
        tts_provider = XTTSProvider({"device": "cpu", "model_path": "models/xtts"})

    SESSIONS[session_id] = {
        "pc": pc,
        "model": model,
        "ws_clients": set(),
        "turns": [],
        "world_context": {"scenario_tags": ["test"], "initiator_faction": {"id": "test", "name": "Test"}, "counterpart_faction": {"id": "ai", "name": "AI"}},
        "initiator_id": "test",
        "counterpart_id": "ai",
        "provider_task": None,
        "provider_tasks": [],
        "blackhole": MediaBlackhole(),
        "listener": None,
        "session_id": session_id,
        "tts_provider": tts_provider,
    }

    return {"session_id": session_id, "model": model, "message": "Teller session created for testing"}

@app.post("/test-tts")
async def test_tts():
    """Test endpoint to verify TTS functionality."""
    try:
        # Try ElevenLabs first
        elevenlabs_api_key = os.getenv("ELEVENLABS_API_KEY", "")
        if elevenlabs_api_key:
            tts_provider = ElevenLabsProvider({
                "api_key": elevenlabs_api_key,
                "voice_id": "21m00Tcm4TlvDq8ikWAM",
                "model": "eleven_monolingual_v1"
            })
        else:
            from tts.xtts import XTTSProvider
            tts_provider = XTTSProvider({"device": "cpu", "model_path": "models/xtts"})

        # Test text synthesis
        test_text = "Hello, this is a test of the TTS system."
        audio_data = None
        async for chunk in tts_provider.synthesize_speech(test_text):
            audio_data = chunk
            break

        if audio_data:
            return {
                "status": "success",
                "text": test_text,
                "audio_size": len(audio_data),
                "provider": "ElevenLabs" if elevenlabs_api_key else "XTTS",
                "message": "TTS audio generation working"
            }
        else:
            return {
                "status": "error",
                "message": "No audio data generated"
            }
    except Exception as e:
        return {
            "status": "error",
            "message": str(e)
        }

@app.post("/test-audio-track")
async def test_audio_track():
    """Test endpoint to verify audio track creation and frame generation."""
    try:
        # Try ElevenLabs first
        elevenlabs_api_key = os.getenv("ELEVENLABS_API_KEY", "")
        if elevenlabs_api_key:
            tts_provider = ElevenLabsProvider({
                "api_key": elevenlabs_api_key,
                "voice_id": "21m00Tcm4TlvDq8ikWAM",
                "model": "eleven_monolingual_v1"
            })
        else:
            from tts.xtts import XTTSProvider
            tts_provider = XTTSProvider({"device": "cpu", "model_path": "models/xtts"})

        test_text = "This is a test audio track for debugging."

        # Test audio track creation
        audio_track = await tts_provider.get_audio_track(test_text, "default")

        if audio_track:
            # Test frame generation with detailed logging
            frames = []
            for i in range(5):  # Generate more frames for better testing
                try:
                    logger.info(f"Requesting frame {i + 1} from audio track")
                    frame = await audio_track.recv()
                    if frame:
                        frames.append({
                            "frame_number": i + 1,
                            "frame_type": type(frame).__name__,
                            "sample_rate": getattr(frame, 'sample_rate', 'unknown'),
                            "pts": getattr(frame, 'pts', 'unknown'),
                            "time_base": str(getattr(frame, 'time_base', 'unknown'))
                        })
                        logger.info(f"Generated frame {i + 1}", frame_type=type(frame).__name__)
                    else:
                        logger.info(f"Frame {i + 1} returned None (end of stream)")
                        break
                except Exception as frame_error:
                    logger.error(f"Frame generation failed at frame {i + 1}", error=str(frame_error))
                    return {
                        "status": "error",
                        "message": f"Frame generation failed at frame {i + 1}",
                        "error": str(frame_error)
                    }

            return {
                "status": "success",
                "text": test_text,
                "track_type": type(audio_track).__name__,
                "frames_generated": len(frames),
                "frames": frames,
                "provider": "ElevenLabs" if elevenlabs_api_key else "XTTS",
                "message": f"Audio track generated {len(frames)} frames successfully"
            }
        else:
            return {
                "status": "error",
                "message": "No audio track created"
            }
    except Exception as e:
        import traceback
        logger.error("Audio track test failed", error=str(e))
        return {
            "status": "error",
            "message": str(e),
            "traceback": traceback.format_exc()
        }

@app.websocket("/v1/session/{sid}/control")
async def ws_control(ws: WebSocket, sid: str):
    await ws.accept()
    sess = SESSIONS[sid]
    sess["ws_clients"].add(ws)
    async def send_yaml(ev: dict):
        if ws.client_state == WebSocketState.CONNECTED:
            await ws.send_text(_dump_yaml(ev))
    async def pump_subtitles():
        if sess["listener"]:
            async for ev in sess["listener"].stream_events():
                if ev.get("type") == "subtitle":
                    await send_yaml({"type": "subtitle", "text": ev.get("text", ""), "final": ev.get("final", False), "confidence": ev.get("confidence", 0.8)})
                    if ev.get("final", False) and ev.get("text", "").strip():
                        user_text = ev.get("text", "").strip()
                        logger.info("User said (final)", text=user_text)
                        turn_entry = {
                            "speaker": "PLAYER",
                            "speaker_id": sess.get("initiator_id", f"player::{sid}"),
                            "text": user_text,
                            "timestamp": datetime.utcnow().isoformat(),
                        }
                        sess["turns"].append(turn_entry)
                        task = asyncio.create_task(generate_ai_response(sess, user_text, send_yaml))
                        sess["provider_tasks"].append(task)
    subtitle_task = asyncio.create_task(pump_subtitles())
    sess["provider_tasks"].append(subtitle_task)

    # Remove old demo provider loop; real-time responses now handled via listener trigger

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                turn_entry = {
                    "speaker": "PLAYER",
                    "speaker_id": sess.get("initiator_id", f"player::{sid}"),
                    "text": text,
                    "timestamp": datetime.utcnow().isoformat(),
                }
                sess["turns"].append(turn_entry)
                task = asyncio.create_task(generate_ai_response(sess, text, send_yaml))
                sess["provider_tasks"].append(task)
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not subtitle_task.done():
            subtitle_task.cancel()
        if sess["listener"]:
            await sess["listener"].stop()
        sess["ws_clients"].discard(ws)
        # Cancel any outstanding AI tasks
        for t in sess.get("provider_tasks", []):
            if not t.done():
                t.cancel()
