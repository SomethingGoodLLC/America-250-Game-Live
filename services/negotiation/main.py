# /services/negotiation/app.py
from __future__ import annotations

import asyncio
import json
import logging
import os
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any

import structlog
from aiortc import RTCPeerConnection
from aiortc.contrib.media import MediaBlackhole
from dotenv import load_dotenv
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from ruamel.yaml import YAML
from starlette.websockets import WebSocketState

from core.logging_config import setup_logging
from diplomacy import stt
from diplomacy.avatar import backend_status, render_avatar_line
from diplomacy.models import DiplomacyContext
from diplomacy.service import (
    finalize_proposal,
    llm_provider_name,
    world_context_from_diplomacy,
)
from listeners.base import make_listener_from_env
from providers.claude_provider import ClaudeProvider
from providers.gemini_veo3 import (
    Veo3Provider,  # stub uses placeholder video unless USE_VEO3=1
)
from providers.mock_local import MockLocalProvider
from schemas.models import SpeakerTurnModel, WorldContextModel
from tts.elevenlabs import ElevenLabsProvider

# Load environment variables from .env file
load_dotenv()

# Setup logging
setup_logging()
logging.getLogger("httpx").setLevel(logging.WARNING)  # fal/ElevenLabs request lines drown the service log
logger = structlog.get_logger(__name__)

yaml = YAML()

app = FastAPI(title="Negotiation Service (YAML/WebRTC)")
app.mount("/assets", StaticFiles(directory="assets"), name="assets")

SESSIONS: dict[str, dict[str, Any]] = {}

DEFAULT_WORLD_CONTEXT = {
    "scenario_tags": ["default_scenario"],
    "initiator_faction": {"id": "player::default", "name": "Player Faction"},
    "counterpart_faction": {"id": "ai::default", "name": "AI Faction"},
    "current_state": {},
}


def sanitize_world_context(raw: dict[str, Any] | None, session_id: str) -> dict[str, Any]:
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


def _decode_control(msg: str, fmt: str) -> dict[str, Any]:
    """Control messages are YAML by default; ``?format=json`` switches the socket to JSON."""
    if fmt == "json":
        obj = json.loads(msg)
    else:
        obj = yaml.load(msg)
    return obj if isinstance(obj, dict) else {}


async def generate_game_response(sess: dict[str, Any], user_text: str, send_func: Any) -> None:
    """Unreal-client path: provider -> validated proposal -> envoy speech. WebRTC is not involved.

    With Claude and PA250_STREAM != "0", each sentence of the spoken line goes to TTS as soon as Claude has
    written it, so the envoy starts speaking before the reply is complete. The clauses are still validated
    only once the whole reply is in. No content filter on the envoy's words (user decision 2026-11-09).
    """
    ctx: DiplomacyContext = sess["diplomacy_context"]
    t_recv = time.time()
    reply_id = f"pa250_{sess.get('session_id', 'x')}_{int(t_recv * 1000)}"
    try:
        await send_func({"type": "ai_thinking", "text": "...", "reply_id": reply_id})
        wc_dict = sess["world_context"]
        world_context = WorldContextModel(**wc_dict)
        turns = [t if isinstance(t, SpeakerTurnModel) else SpeakerTurnModel(**t) for t in sess["turns"][-8:]]
        speech = _SpeechStream(reply_id, ctx.faction, send_func, t_recv)

        spoken, raw, provider_name = "", None, "mock"
        streamed = False
        use_claude = llm_provider_name() == "claude"
        if use_claude and os.getenv("PA250_STREAM", "1") != "0":
            provider = ClaudeProvider({})
            info = (world_context.current_state or {}).get("envoy", {})
            try:
                raw = await provider.propose_streaming(
                    turns, ctx, str(info.get("name", "")), str(info.get("title", "")),
                    world_context.initiator_faction.get("id", ""), speech.say)
                provider_name, streamed = "claude", True
            except Exception as e:
                logger.error("Streaming reply failed", error=str(e), sentences_spoken=speech.count)
                if speech.count:  # already speaking: keep what was said, no clauses
                    raw, provider_name, streamed = {"spoken_line": speech.text, "clauses": []}, "claude", True
            finally:
                await provider.close()
        if not streamed:
            order = ["claude", "mock"] if use_claude else ["mock"]
            for name in order:
                provider = ClaudeProvider({}) if name == "claude" else MockLocalProvider(
                    {"strict": False, "compromised": bool(sess.get("compromised"))})
                try:
                    spoken, raw = "", None
                    async for ev in provider.stream_dialogue(turns=turns, world_context=world_context):
                        if ev.type == "subtitle" and ev.is_final:
                            spoken = str(ev.payload.get("text", ""))
                        elif ev.type == "diplomacy_proposal":
                            raw = ev.payload.get("proposal")
                    provider_name = name
                    break
                except Exception as e:
                    logger.error("Diplomacy provider failed; falling back", provider=name, error=str(e))
                finally:
                    if isinstance(provider, ClaudeProvider):
                        await provider.close()  # one client per reply; do not leak its connection pool
            if raw is None:
                raw = {"spoken_line": spoken, "clauses": []}
            spoken = str(raw.get("spoken_line", spoken)) if isinstance(raw, dict) else spoken
            speech.t_llm_done = time.time()
            await speech.say(spoken)  # one chunk
        else:
            spoken = speech.text  # exactly what was spoken
        speech.close()

        if isinstance(raw, dict):
            raw = dict(raw, spoken_line=spoken)
        wire, errors = finalize_proposal(ctx, raw)
        sess["turns"].append({
            "speaker": "AI", "text": spoken, "speaker_id": wc_dict["counterpart_faction"]["id"],
            "timestamp": datetime.utcnow().isoformat(),
        })
        logger.info("Diplomacy reply", provider=provider_name, streamed=streamed, spoken=spoken,
                    clauses=len(wire["clauses"]), rejected=errors,
                    llm_s=round((speech.t_llm_done or time.time()) - t_recv, 2),
                    first_sentence_s=round(speech.t_first - t_recv, 2) if speech.t_first else None)
        await send_func({"type": "ai_response", "text": spoken, "final": True, "reply_id": reply_id})
        await send_func({
            "type": "diplomacy_proposal", "spoken_line": spoken, "clauses": wire["clauses"],
            "rejected": errors, "provider": provider_name, "reply_id": reply_id,
        })
    except Exception as e:
        logger.error("Error generating game response", error=str(e))
        await send_func({"type": "representative_unavailable", "text": "The envoy cannot be reached."})


VIDEO_STOCK_DIR = Path(__file__).resolve().parent / "generated_video" / "stock"

# Strong references keep running speech jobs alive (asyncio holds tasks only weakly).
SPEECH_JOBS: set[asyncio.Task[None]] = set()


class _SpeechStream:
    """One reply's speech: sentences go to TTS as they arrive; a detached sender delivers them in order.

    The sender task lives in SPEECH_JOBS, so neither the next utterance, the reply finishing, nor the client
    closing the socket cancels it. Every chunk ends in a 'Speech chunk sent' log line or a WARNING.
    """

    def __init__(self, reply_id: str, envoy_id: str, send_func: Any, t_recv: float) -> None:
        self.reply_id, self.envoy_id, self.send_func, self.t_recv = reply_id, envoy_id, send_func, t_recv
        self.parts: list[str] = []
        self.queue: asyncio.Queue[tuple[int, str, asyncio.Task[Any], float] | None] = asyncio.Queue()
        self.t_first: float | None = None
        self.t_llm_done: float | None = None
        job = asyncio.create_task(self._sender(), name=reply_id)
        SPEECH_JOBS.add(job)
        job.add_done_callback(SPEECH_JOBS.discard)

    @property
    def count(self) -> int:
        return len(self.parts)

    @property
    def text(self) -> str:
        return " ".join(self.parts)

    async def say(self, sentence: str) -> None:
        sentence = sentence.strip()
        if not sentence:
            return
        now = time.time()
        if self.t_first is None:
            self.t_first = now
        previous = self.text or None
        self.parts.append(sentence)
        idx = len(self.parts) - 1
        task = asyncio.create_task(render_avatar_line(sentence, f"{self.reply_id}_{idx}", envoy_id=self.envoy_id,
                                                      previous_text=previous))
        self.queue.put_nowait((idx, sentence, task, now))

    def close(self) -> None:
        if self.t_llm_done is None:
            self.t_llm_done = time.time()
        self.queue.put_nowait(None)

    async def _sender(self) -> None:
        while True:
            item = await self.queue.get()
            if item is None:
                break
            idx, text, task, t_sentence = item
            try:
                line = await task
            except asyncio.CancelledError:
                logger.warning("Speech chunk cancelled", reply_id=self.reply_id, chunk=idx, reason="service shutting down")
                raise
            except Exception as e:
                logger.warning("Speech chunk failed", reply_id=self.reply_id, chunk=idx, error=f"{type(e).__name__}: {e}")
                continue
            t_sent = time.time()
            timing = {"recv": round(self.t_recv, 3), "sentence_ready_s": round(t_sentence - self.t_recv, 3),
                      "tts_s": round(t_sent - t_sentence, 3), "sent": round(t_sent, 3)}
            if line.audio_file:
                ev: dict[str, Any] = {"type": "sadtalker_video_ready", "audio_file": line.audio_file, "text": text,
                                      "job_id": f"{self.reply_id}_{idx}", "reply_id": self.reply_id, "chunk": idx,
                                      "backend": line.backend, "tts_backend": line.tts_backend,
                                      "audio_seconds": round(line.audio_seconds, 3), "timing": timing}
                if line.video_file:
                    ev["video_file"] = line.video_file
                if line.visemes:
                    ev["visemes"] = line.visemes
            else:
                ev = {"type": "avatar_unavailable", "text": text, "job_id": f"{self.reply_id}_{idx}",
                      "reply_id": self.reply_id, "notes": line.notes}
            try:
                delivered = (await self.send_func(ev)) is not False
            except Exception:
                delivered = False
            log = logger.info if delivered else logger.warning
            log("Speech chunk sent" if delivered else "Speech chunk not delivered", reply_id=self.reply_id, chunk=idx,
                tts_backend=line.tts_backend, audio_seconds=round(line.audio_seconds, 2), audio_file=line.audio_file,
                video_backend=line.backend, sentence_ready_s=timing["sentence_ready_s"], tts_s=timing["tts_s"],
                recv_to_sent_s=round(t_sent - self.t_recv, 3), visemes=bool(line.visemes))
        try:
            await self.send_func({"type": "envoy_speech_end", "reply_id": self.reply_id, "chunks": len(self.parts)})
        except Exception:
            pass
        logger.info("Speech job finished", reply_id=self.reply_id, chunks=len(self.parts),
                    elapsed_s=round(time.time() - self.t_recv, 2))


async def generate_ai_response(sess: dict, user_text: str, send_yaml_func):
    """Generate AI response to user input and trigger avatar generation."""
    if sess.get("model") == "game":
        await generate_game_response(sess, user_text, send_yaml_func)
        return
    try:
        logger.info("Generating AI response", user_text=user_text)
        if sess["model"] == "veo3":
            provider = Veo3Provider(use_veo3=True)
        else:
            # Use MockLocalProvider for both "mock_local" and "sadtalker" models
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
                    elif sess["model"] == "sadtalker":
                        logger.info("Calling generate_sadtalker_avatar for TTS audio")
                        await generate_sadtalker_avatar(ai_text, send_yaml_func, sess.get("session_id"))
                    elif sess["model"] == "gemini_realtime":
                        logger.info("Calling generate_wav2lip_avatar for Gemini Realtime (fast lip-sync)")
                        # Use Wav2Lip for fast lip-sync (~5-10 seconds)
                        await generate_wav2lip_avatar(ai_text, send_yaml_func, sess.get("session_id"))
                    elif sess["model"] == "wav2lip":
                        logger.info("Calling generate_wav2lip_avatar for fast lip-sync")
                        await generate_wav2lip_avatar(ai_text, send_yaml_func, sess.get("session_id"))
                    elif sess["model"] == "did":
                        logger.info("Calling generate_wav2lip_avatar for D-ID cloud lip-sync")
                        await generate_wav2lip_avatar(ai_text, send_yaml_func, sess.get("session_id"))

            elif ev.type == "intent":
                logger.info("Sending AI intent", intent_type=ev.payload.get("intent", {}).get("type", "unknown"))
                await send_yaml_func({"type": "ai_intent", "payload": ev.payload})
    except Exception as e:
        logger.error("Error generating AI response", error=str(e))
        await send_yaml_func({"type": "ai_response", "text": "I apologize, I'm having trouble processing that. Could you rephrase?", "final": True})

async def generate_veo3_avatar(ai_text: str, send_yaml_func):
    try:
        logger.info("Generating Veo3 avatar", text=ai_text)
        Veo3Provider(use_veo3=True)
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
            import io
            import wave

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


async def generate_sadtalker_video_from_audio(
    audio_file_path: str,
    text: str,
    job_id: str,
    send_yaml_func
) -> str | None:
    """Generate SadTalker video from audio file.

    Args:
        audio_file_path: Path to the TTS audio file
        text: The text being spoken
        job_id: Unique job identifier
        send_yaml_func: Function to send status updates

    Returns:
        Path to generated video file, or None if generation failed
    """
    try:
        logger.info("Starting SadTalker video generation", audio_file=audio_file_path)

        # Send generation status
        await send_yaml_func({
            "type": "sadtalker_generating",
            "text": "🎨 Generating animated portrait..."
        })

        # Get configuration from environment
        sadtalker_dir = Path(os.getenv("SADTALKER_DIR", "~/Projects/SadTalker")).expanduser()
        portrait_path = Path(os.getenv("PORTRAIT_PATH", "assets/avatars/british_general_frame.jpg"))
        device = os.getenv("SADTALKER_DEVICE", "mps")
        enhance = os.getenv("SADTALKER_ENHANCE", "true").lower() == "true"
        still_mode = os.getenv("SADTALKER_STILL_MODE", "false").lower() == "true"

        # Verify SadTalker is installed
        inference_script = sadtalker_dir / "inference.py"
        if not inference_script.exists():
            logger.error("SadTalker not found", path=str(inference_script))
            return None

        # Create output directory
        video_dir = Path("diplomatic_videos")
        video_dir.mkdir(exist_ok=True)
        result_dir = video_dir / f"result_{job_id}"
        result_dir.mkdir(exist_ok=True)

        # Build SadTalker command
        venv_python = sadtalker_dir / ".venv" / "bin" / "python"
        python_exe = str(venv_python) if venv_python.exists() else sys.executable

        cmd = [
            python_exe,
            str(inference_script),
            "--driven_audio", str(Path(audio_file_path).absolute()),
            "--source_image", str(portrait_path.absolute()),
            "--result_dir", str(result_dir.absolute()),
            "--batch_size", "1",
        ]

        # SadTalker uses --cpu flag for CPU, or no flag for GPU/MPS
        if device == "cpu":
            cmd.append("--cpu")
        # For MPS or GPU, don't add device flag - it auto-detects

        if still_mode:
            cmd.append("--still")

        if enhance:
            cmd.extend(["--enhancer", "gfpgan"])

        logger.info("Running SadTalker", command=" ".join(cmd))

        # Run SadTalker
        process = await asyncio.create_subprocess_exec(
            *cmd,
            cwd=str(sadtalker_dir),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            env={**os.environ, "PYTORCH_ENABLE_MPS_FALLBACK": "1"}
        )

        stdout, stderr = await process.communicate()

        if process.returncode != 0:
            logger.error("SadTalker failed",
                        returncode=process.returncode,
                        stderr=stderr.decode()[:500])
            return None

        logger.info("SadTalker completed", stdout=stdout.decode()[:200])

        # Find the generated video
        generated_videos = list(result_dir.glob("*.mp4"))
        if not generated_videos:
            logger.error("No video generated by SadTalker", result_dir=str(result_dir))
            return None

        video_file = generated_videos[0]
        logger.info("SadTalker video generated successfully", video_path=str(video_file))

        # Return relative path for web serving
        return str(video_file.relative_to(Path.cwd()))

    except Exception as e:
        logger.error("Error generating SadTalker video", error=str(e), exc_info=True)
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


async def generate_wav2lip_avatar(ai_text: str, send_yaml_func, session_id: str = None):
    """Generate Wav2Lip avatar with fast lip-sync (~5-10 seconds)."""
    try:
        logger.info("Generating Wav2Lip avatar", text=ai_text)

        # Send avatar generation status
        await send_yaml_func({
            "type": "avatar_generating",
            "text": "🎬 Wav2Lip generating lip-sync video...",
            "final": False
        })

        # Generate TTS audio and create video
        if session_id and session_id in SESSIONS:
            sess = SESSIONS[session_id]
            tts_provider = sess.get("tts_provider")
            pc = sess.get("pc")

            logger.info("TTS setup check", has_tts_provider=tts_provider is not None, has_peer_connection=pc is not None)

            if tts_provider and pc:
                try:
                    logger.info("Generating TTS audio for AI response", text=ai_text[:50])

                    # Update the audio track with new TTS data using job system
                    if "audio_track" in sess and "tts_provider" in sess:
                        logger.info("Starting TTS job for audio generation")
                        sess["audio_track"]
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
                            sess["audio_track"] = new_audio_track
                            logger.info("Successfully updated audio track with AI response from file")

                            # Send job completion status
                            await send_yaml_func({
                                "type": "tts_job_completed",
                                "job_id": job_id,
                                "audio_file": audio_file_path,
                                "text": ai_text
                            })

                            # Generate talking avatar video with D-ID
                            did_enabled = os.getenv("DID_ENABLED", "true").lower() == "true"
                            video_path = None

                            if did_enabled and os.getenv("DID_API_KEY"):
                                try:
                                    from pathlib import Path

                                    from video_providers.did_video import (
                                        DIDVideoProvider,
                                    )

                                    # Use local portrait path - D-ID will upload it
                                    portrait_path = os.getenv("PORTRAIT_PATH", "assets/avatars/british_general_frame.jpg")

                                    logger.info("Generating D-ID video with text", text=ai_text[:50], portrait=portrait_path)

                                    # Send progress update to client
                                    await send_yaml_func({
                                        "type": "avatar_status",
                                        "status": "generating",
                                        "message": "⏳ Generating photorealistic lip-sync video (20-40s)...",
                                        "generator": "did"
                                    })

                                    # Generate video with D-ID's built-in TTS
                                    provider = DIDVideoProvider()
                                    output_path = Path(f"diplomatic_videos/did/did_{job_id}.mp4")

                                    # Use text directly - D-ID handles TTS internally
                                    video_path = await provider.generate_video(
                                        text=ai_text,
                                        image_url=portrait_path,
                                        output_path=output_path,
                                        voice_id="en-US-GuyNeural"  # Male voice for British general
                                    )

                                    logger.info("✅ D-ID video generated successfully!", path=video_path)

                                except Exception as did_error:
                                    logger.error("D-ID generation failed, using audio only", error=str(did_error))
                                    import traceback
                                    logger.error("D-ID error details", traceback=traceback.format_exc())
                                    video_path = None
                            else:
                                if not os.getenv("DID_API_KEY"):
                                    logger.info("D-ID disabled: No API key set. Get one at https://studio.d-id.com/account-settings")

                            # Send ready event
                            await send_yaml_func({
                                "type": "sadtalker_video_ready",  # Reuse same event type
                                "audio_file": audio_file_path,
                                "video_file": video_path,
                                "text": ai_text,
                                "job_id": job_id,
                                "generator": "did" if video_path else "audio_only"
                            })

                            if video_path:
                                logger.info("D-ID avatar ready for playback", video_file=video_path)
                            else:
                                logger.info("Audio-only avatar ready (D-ID disabled or failed)")
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

    except Exception as e:
        logger.error("Error generating Wav2Lip avatar", error=str(e))


async def generate_sadtalker_avatar(ai_text: str, send_yaml_func, session_id: str = None):
    """Generate SadTalker Avatar animation for AI speech with audio."""
    try:
        logger.info("Generating SadTalker avatar", text=ai_text)

        # Send avatar generation status
        await send_yaml_func({
            "type": "avatar_generating",
            "text": "🎭 SadTalker Avatar generating portrait...",
            "final": False
        })

        # Generate TTS audio and create video
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

                            # Check if SadTalker is enabled (disabled by default for speed)
                            sadtalker_enabled = os.getenv("SADTALKER_ENABLED", "false").lower() == "true"
                            video_path = None

                            if sadtalker_enabled:
                                # Generate SadTalker video from the audio (SLOW: ~20min for 10sec speech)
                                video_path = await generate_sadtalker_video_from_audio(
                                    audio_file_path, ai_text, job_id, send_yaml_func
                                )

                            if video_path and sadtalker_enabled:
                                # Trigger SadTalker video playback
                                await send_yaml_func({
                                    "type": "sadtalker_video_ready",
                                    "audio_file": audio_file_path,
                                    "video_file": video_path,
                                    "text": ai_text,
                                    "job_id": job_id
                                })
                                logger.info("SadTalker video ready for playback", video_file=video_path)
                            else:
                                # Fallback to audio-only if video generation fails
                                await send_yaml_func({
                                    "type": "sadtalker_video_ready",
                                    "audio_file": audio_file_path,
                                    "text": ai_text,
                                    "job_id": job_id
                                })
                                logger.warning("SadTalker video generation failed, using audio only")
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

    except Exception as e:
        logger.error("Error generating SadTalker avatar", error=str(e))

@app.get("/", response_class=HTMLResponse)
async def root():
    # Serve the enhanced test page
    with open("web/enhanced_test_client.html", encoding="utf-8") as f:
        return HTMLResponse(f.read())

@app.get("/sadtalker-avatar-component.html", response_class=HTMLResponse)
async def sadtalker_avatar_component():
    # Serve the SadTalker Avatar component
    with open("web/sadtalker-avatar-component.html", encoding="utf-8") as f:
        return HTMLResponse(f.read())

@app.get("/gemini-realtime-component.html", response_class=HTMLResponse)
async def gemini_avatar_component():
    # Serve the Gemini Realtime Avatar component
    with open("web/gemini-realtime-component.html", encoding="utf-8") as f:
        return HTMLResponse(f.read())

# Create generated audio directory if it doesn't exist
os.makedirs("generated_audio", exist_ok=True)

# Mount the diplomatic videos directory to serve generated videos
if os.path.exists("diplomatic_videos"):
    app.mount("/diplomatic_videos", StaticFiles(directory="diplomatic_videos"), name="diplomatic-videos")
# Mount the generated audio directory
app.mount("/generated_audio", StaticFiles(directory="generated_audio"), name="generated-audio")

@app.post("/v1/session")
async def create_session(request: Request):
    try:
        body = await request.json()
    except Exception:
        body = {}
    session_id = str(uuid.uuid4())[:8]
    model = body.get("model", "gemini_realtime")  # Default to Gemini Realtime for real-time performance
    logger.info("Session creation request", session_id=session_id, requested_model=model, body_keys=list(body.keys()) if body else [])

    raw_dctx = body.get("diplomacy_context")
    if isinstance(raw_dctx, dict):
        # Unreal-client session: no WebRTC; DiplomacyContext from the sim is the only world input.
        try:
            dctx = DiplomacyContext(**raw_dctx)
        except Exception as e:
            return JSONResponse({"error": f"invalid diplomacy_context: {e}"}, status_code=422)
        env = body.get("envoy") if isinstance(body.get("envoy"), dict) else {}
        world_context = world_context_from_diplomacy(raw_dctx, str(env.get("name", "")), str(env.get("title", "")))
        SESSIONS[session_id] = {
            "pc": None, "model": "game", "ws_clients": set(), "turns": [], "world_context": world_context,
            "initiator_id": world_context["initiator_faction"]["id"],
            "counterpart_id": world_context["counterpart_faction"]["id"],
            "provider_task": None, "provider_tasks": [], "listener": None, "session_id": session_id,
            "tts_provider": None, "diplomacy_context": dctx,
            "compromised": bool(body.get("compromised_mock", False)),
        }
        return {"session_id": session_id, "provider": llm_provider_name()}

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
        raise HTTPException(status_code=400, detail=f"Failed to set remote description: {str(e)}") from e

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
                raise HTTPException(
                    status_code=500, detail=f"WebRTC connection failed: {str(retry_error)}"
                ) from retry_error
        else:
            logger.error("Failed to create/set local description", error=str(e))
            from fastapi import HTTPException
            raise HTTPException(
                status_code=400, detail=f"ValueError in local description: {str(e)}"
            ) from e
    except Exception as e:
        logger.error("Failed to create/set local description", error=str(e))
        from fastapi import HTTPException
        raise HTTPException(status_code=500, detail=f"Failed to create local description: {str(e)}") from e

@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "ok", "service": "negotiation", "version": "1.0.0"}


@app.on_event("startup")
async def _prewarm_stt() -> None:
    # Load the speech model in the background so the first mic press is not a ~1 min download/load.
    if os.getenv("PA250_STT_PREWARM", "1") != "0":
        asyncio.create_task(stt.prewarm())


@app.post("/v1/stt")
async def speech_to_text(request: Request) -> JSONResponse:
    """Player mic clip (16-bit PCM WAV body) -> text. The client sends the text as a player_utterance."""
    body = await request.body()
    if not body.startswith(b"RIFF") or len(body) > 12_000_000:
        return JSONResponse({"error": "send a 16-bit PCM WAV body (max 12 MB)"}, status_code=422)
    try:
        return JSONResponse(await stt.transcribe_wav(body))
    except ValueError as e:
        return JSONResponse({"error": str(e)}, status_code=422)
    except Exception as e:
        logger.warning("STT failed", error=f"{type(e).__name__}: {e}"[:400])
        return JSONResponse({"error": "speech recognition unavailable"}, status_code=503)


@app.get("/v1/avatar/health")
async def avatar_health() -> dict[str, Any]:
    """Each video backend in chain order: available + reason (configuration only, never key values)."""
    return {
        "mode": os.getenv("PA250_AVATAR", "auto"),
        "timeout_s": float(os.getenv("PA250_AVATAR_TIMEOUT", "90")),
        "tts": os.getenv("PA250_TTS", "auto"),
        "backends": backend_status(),
    }

@app.post("/test-session-sadtalker")
async def test_session_sadtalker():
    """Test endpoint to create a session with sadtalker model directly."""
    session_id = str(uuid.uuid4())[:8]
    model = "sadtalker"
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

    return {"session_id": session_id, "model": model, "message": "SadTalker session created for testing"}

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
    # Default stays YAML. ``?format=json`` is for clients (Unreal) without a YAML parser.
    fmt = ws.query_params.get("format", "yaml").lower()
    async def send_yaml(ev: dict) -> bool:
        """Returns False when the socket is already closed (event dropped)."""
        if ws.client_state != WebSocketState.CONNECTED:
            return False
        await ws.send_text(json.dumps(ev) if fmt == "json" else _dump_yaml(ev))
        return True
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

    if sess.get("model") == "game":
        # Pre-rendered lip-synced lines (scripts/render_stock_lines.py) the room plays as video.
        faction = sess["diplomacy_context"].faction
        stock = VIDEO_STOCK_DIR
        clips = {p.stem.split("_", 1)[1]: str(p) for p in sorted(stock.glob(f"{faction}_*.mp4"))} if stock.is_dir() else {}
        texts = {name: (stock / f"{faction}_{name}.txt").read_text().strip() for name in clips
                 if (stock / f"{faction}_{name}.txt").exists()}
        await send_yaml({"type": "envoy_stock", "clips": clips, "texts": texts})
        logger.info("Envoy stock clips offered", session_id=sid, clips=sorted(clips))

    # Remove old demo provider loop; real-time responses now handled via listener trigger

    try:
        while True:
            msg = await ws.receive_text()
            try:
                obj = _decode_control(msg, fmt)
            except Exception:
                await send_yaml({"type": "error", "message": "malformed control message"})
                continue
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                logger.info("Player utterance", text=text, session_id=sid)
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
