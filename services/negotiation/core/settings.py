"""Application settings and configuration."""

from typing import List, Optional
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Application settings loaded from environment variables."""

    # Application
    debug: bool = False
    log_level: str = "INFO"
    
    # Server
    host: str = "0.0.0.0"
    port: int = 8000
    workers: int = 1
    
    # WebRTC
    stun_servers: str = "stun:stun.l.google.com:19302"
    turn_servers: Optional[str] = None
    
    # AI Providers
    # Gemini/Google AI
    gemini_api_key: Optional[str] = None
    gemini_model: str = "gemini-2.5-pro"  # For text/LLM analysis
    gemini_veo_model: str = "gemini-veo3"  # For video generation
    gemini_project_id: Optional[str] = None
    
    # OpenAI/ChatGPT
    openai_api_key: Optional[str] = None
    # Use stable, widely-available default; override via OPENAI_MODEL
    openai_model: str = "gpt-4o"
    openai_org_id: Optional[str] = None
    
    # Anthropic/Claude
    anthropic_api_key: Optional[str] = None
    # Use Anthropic's moving alias for the latest Sonnet in 3.5 family
    anthropic_model: str = "claude-3-5-sonnet-latest"
    anthropic_max_tokens: int = 4096
    
    # xAI/Grok
    grok_api_key: Optional[str] = None
    grok_model: str = "grok-beta"
    grok_base_url: str = "https://api.x.ai/v1"
    
    # STT/TTS
    whisper_model_size: str = "base"
    whisper_device: str = "cpu"
    xtts_model_path: str = "models/xtts"
    xtts_device: str = "cpu"
    
    # Content Safety
    content_safety_enabled: bool = True
    content_safety_provider: str = "rule_based"
    
    # Session Management
    session_timeout_minutes: int = 60
    max_concurrent_sessions: int = 100
    
    # Avatar/Video
    avatar_style: str = "diplomatic"
    voice_id: str = "default"
    latency_target_ms: int = 500
    
    # Pydantic v2 settings
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",  # ignore stale/unknown env vars to avoid hard failures
    )


# Global settings instance
settings = Settings()
