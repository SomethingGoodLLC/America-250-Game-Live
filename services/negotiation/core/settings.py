"""Application settings and configuration."""

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
    turn_servers: str | None = None

    # AI Providers
    # Gemini/Google AI
    gemini_api_key: str | None = None
    gemini_model: str = "gemini-2.5-pro"  # For text/LLM analysis
    gemini_veo_model: str = "gemini-veo3"  # For video generation
    gemini_project_id: str | None = None

    # OpenAI/ChatGPT
    openai_api_key: str | None = None
    # Use stable, widely-available default; override via OPENAI_MODEL
    openai_model: str = "gpt-4o"
    openai_org_id: str | None = None

    # Anthropic/Claude
    anthropic_api_key: str | None = None
    # Use Anthropic's moving alias for the latest Sonnet in 3.5 family
    anthropic_model: str = "claude-3-5-sonnet-latest"
    anthropic_max_tokens: int = 4096

    # xAI/Grok
    grok_api_key: str | None = None
    grok_model: str = "grok-beta"
    grok_base_url: str = "https://api.x.ai/v1"

    # STT/TTS
    whisper_model_size: str = "base"
    whisper_device: str = "cpu"
    xtts_model_path: str = "models/xtts"
    xtts_device: str = "cpu"

    # TTS Streaming Providers
    cartesia_api_key: str | None = None
    cartesia_model: str = "sonic-english"

    azure_speech_key: str | None = None
    azure_speech_region: str | None = None
    azure_speech_endpoint: str | None = None

    elevenlabs_api_key: str | None = None
    elevenlabs_model: str = "eleven_turbo_v2"

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

    # Rate Limiting (for WebSocket proxies and token endpoints)
    rate_limit_ws_per_ip: int = 3  # Max concurrent WebSocket connections per IP
    rate_limit_token_per_minute: int = 10  # Max token requests per minute per IP
    rate_limit_token_per_hour: int = 100  # Max token requests per hour per IP

    # CORS/Origin checking for WebSocket proxies
    allowed_ws_origins: str = "http://localhost:*,http://127.0.0.1:*,https://*.your-domain.com"

    # Pydantic v2 settings
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",  # ignore stale/unknown env vars to avoid hard failures
    )


# Global settings instance
settings = Settings()
