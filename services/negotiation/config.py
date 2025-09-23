"""Configuration management for negotiation service providers."""

import os
from typing import Optional, Dict, Any, List
from pathlib import Path
from dataclasses import dataclass
import structlog


@dataclass
class ProviderConfig:
    """Configuration for negotiation providers."""
    # API Keys (loaded from environment)
    gemini_api_key: Optional[str] = None
    openai_api_key: Optional[str] = None
    anthropic_api_key: Optional[str] = None
    grok_api_key: Optional[str] = None

    # Model configurations
    gemini_model: str = "gemini-2.5-pro"
    gemini_veo_model: str = "gemini-veo3"
    openai_model: str = "gpt-4o"
    anthropic_model: str = "claude-3-5-sonnet-latest"
    grok_model: str = "grok-beta"

    # Provider-specific settings
    provider_timeout: int = 30
    max_retries: int = 3
    backpressure_enabled: bool = True

    # Video source settings
    default_video_source: str = "placeholder"
    avatar_style: str = "diplomatic"
    video_resolution: tuple = (640, 480)  # Use tuple instead of tuple[int, int] for compatibility
    video_framerate: int = 30
    video_quality: str = "medium"

    # Audio settings
    default_voice_id: str = "diplomat_en_us"
    audio_sample_rate: int = 16000
    tts_provider: str = "xtts"

    # Content safety settings
    content_safety_enabled: bool = True
    content_safety_provider: str = "rule_based"
    safety_strict_mode: bool = False

    # Session management
    session_timeout_minutes: int = 60
    max_concurrent_sessions: int = 100

    # Resource limits
    memory_limit_mb: int = 500
    cpu_limit_percent: int = 80

    # Feature toggles
    enable_video: bool = True
    enable_audio: bool = True
    enable_realtime_subtitles: bool = True
    enable_intent_detection: bool = True

    # Video+dialogue provider settings
    use_veo3: int = 0
    veo3_api_key: Optional[str] = None
    avatar_style: str = "colonial_diplomat"
    voice_id: str = "en_male_01"
    latency_target_ms: int = 800


@dataclass
class WebRTCConfig:
    """Configuration for WebRTC connections."""
    stun_servers: List[str] = None
    turn_servers: List[str] = None
    enable_turn: bool = False
    turn_username: Optional[str] = None
    turn_password: Optional[str] = None

    # Connection settings
    connection_timeout: int = 30
    max_connections: int = 100

    def __post_init__(self):
        if self.stun_servers is None:
            self.stun_servers = ["stun:stun.l.google.com:19302"]


@dataclass
class LoggingConfig:
    """Configuration for logging."""
    level: str = "INFO"
    format: str = "json"
    enable_structured_logging: bool = True
    log_to_file: bool = False
    log_file_path: Optional[str] = None


class ConfigManager:
    """Manages configuration for the negotiation service.

    This class loads configuration from environment variables and
    provides typed configuration objects for different components.
    """

    def __init__(self):
        self.logger = structlog.get_logger(__name__)

        # Load environment variables
        self._load_from_environment()

        # Create configuration objects
        self.provider_config = self._create_provider_config()
        self.webrtc_config = self._create_webrtc_config()
        self.logging_config = self._create_logging_config()

    def _load_from_environment(self):
        """Load configuration values from environment variables."""
        # API Keys
        self.gemini_api_key = os.getenv("GEMINI_API_KEY")
        self.openai_api_key = os.getenv("OPENAI_API_KEY")
        self.anthropic_api_key = os.getenv("ANTHROPIC_API_KEY")
        self.grok_api_key = os.getenv("GROK_API_KEY")

        # Model configurations
        self.gemini_model = os.getenv("GEMINI_MODEL", "gemini-2.5-pro")
        self.gemini_veo_model = os.getenv("GEMINI_VEO_MODEL", "gemini-veo3")
        self.openai_model = os.getenv("OPENAI_MODEL", "gpt-4o")
        self.anthropic_model = os.getenv("ANTHROPIC_MODEL", "claude-3-5-sonnet-latest")
        self.grok_model = os.getenv("GROK_MODEL", "grok-beta")

        # Provider settings
        self.provider_timeout = int(os.getenv("PROVIDER_TIMEOUT", "30"))
        self.max_retries = int(os.getenv("MAX_RETRIES", "3"))
        self.backpressure_enabled = os.getenv("BACKPRESSURE_ENABLED", "true").lower() == "true"

        # Video source settings
        self.default_video_source = os.getenv("DEFAULT_VIDEO_SOURCE", "placeholder")
        self.avatar_style = os.getenv("AVATAR_STYLE", "diplomatic")
        self.video_resolution = self._parse_resolution(os.getenv("VIDEO_RESOLUTION", "640x480"))
        self.video_framerate = int(os.getenv("VIDEO_FRAMERATE", "30"))
        self.video_quality = os.getenv("VIDEO_QUALITY", "medium")

        # Audio settings
        self.default_voice_id = os.getenv("DEFAULT_VOICE_ID", "diplomat_en_us")
        self.audio_sample_rate = int(os.getenv("AUDIO_SAMPLE_RATE", "16000"))
        self.tts_provider = os.getenv("TTS_PROVIDER", "xtts")

        # Content safety settings
        self.content_safety_enabled = os.getenv("CONTENT_SAFETY_ENABLED", "true").lower() == "true"
        self.content_safety_provider = os.getenv("CONTENT_SAFETY_PROVIDER", "rule_based")
        self.safety_strict_mode = os.getenv("SAFETY_STRICT_MODE", "false").lower() == "true"

        # Session management
        self.session_timeout_minutes = int(os.getenv("SESSION_TIMEOUT_MINUTES", "60"))
        self.max_concurrent_sessions = int(os.getenv("MAX_CONCURRENT_SESSIONS", "100"))

        # Resource limits
        self.memory_limit_mb = int(os.getenv("MEMORY_LIMIT_MB", "500"))
        self.cpu_limit_percent = int(os.getenv("CPU_LIMIT_PERCENT", "80"))

        # Feature toggles
        self.enable_video = os.getenv("ENABLE_VIDEO", "true").lower() == "true"
        self.enable_audio = os.getenv("ENABLE_AUDIO", "true").lower() == "true"
        self.enable_realtime_subtitles = os.getenv("ENABLE_REALTIME_SUBTITLES", "true").lower() == "true"
        self.enable_intent_detection = os.getenv("ENABLE_INTENT_DETECTION", "true").lower() == "true"

        # Video+dialogue provider settings
        self.use_veo3 = int(os.getenv("USE_VEO3", "0"))
        self.veo3_api_key = os.getenv("VEO3_API_KEY")
        self.avatar_style = os.getenv("AVATAR_STYLE", "colonial_diplomat")
        self.voice_id = os.getenv("VOICE_ID", "en_male_01")
        self.latency_target_ms = int(os.getenv("LATENCY_TARGET_MS", "800"))

        # WebRTC settings
        self.stun_servers = os.getenv("STUN_SERVERS", "stun:stun.l.google.com:19302").split(",")
        self.turn_servers = os.getenv("TURN_SERVERS", "").split(",") if os.getenv("TURN_SERVERS") else []
        self.enable_turn = os.getenv("ENABLE_TURN", "false").lower() == "true"
        self.turn_username = os.getenv("TURN_USERNAME")
        self.turn_password = os.getenv("TURN_PASSWORD")

        # Connection settings
        self.connection_timeout = int(os.getenv("CONNECTION_TIMEOUT", "30"))
        self.max_connections = int(os.getenv("MAX_CONNECTIONS", "100"))

        # Logging settings
        self.log_level = os.getenv("LOG_LEVEL", "INFO")
        self.log_format = os.getenv("LOG_FORMAT", "json")
        self.enable_structured_logging = os.getenv("ENABLE_STRUCTURED_LOGGING", "true").lower() == "true"
        self.log_to_file = os.getenv("LOG_TO_FILE", "false").lower() == "true"
        self.log_file_path = os.getenv("LOG_FILE_PATH")

    def _parse_resolution(self, resolution_str: str) -> tuple:
        """Parse resolution string like '640x480' into tuple."""
        try:
            width, height = map(int, resolution_str.split("x"))
            return (width, height)
        except (ValueError, AttributeError):
            self.logger.warning("Invalid resolution format, using default", resolution=resolution_str)
            return (640, 480)

    def _create_provider_config(self) -> ProviderConfig:
        """Create provider configuration object."""
        return ProviderConfig(
            gemini_api_key=self.gemini_api_key,
            openai_api_key=self.openai_api_key,
            anthropic_api_key=self.anthropic_api_key,
            grok_api_key=self.grok_api_key,
            gemini_model=self.gemini_model,
            gemini_veo_model=self.gemini_veo_model,
            openai_model=self.openai_model,
            anthropic_model=self.anthropic_model,
            grok_model=self.grok_model,
            provider_timeout=self.provider_timeout,
            max_retries=self.max_retries,
            backpressure_enabled=self.backpressure_enabled,
            default_video_source=self.default_video_source,
            avatar_style=self.avatar_style,
            video_resolution=self.video_resolution,
            video_framerate=self.video_framerate,
            video_quality=self.video_quality,
            default_voice_id=self.default_voice_id,
            audio_sample_rate=self.audio_sample_rate,
            tts_provider=self.tts_provider,
            content_safety_enabled=self.content_safety_enabled,
            content_safety_provider=self.content_safety_provider,
            safety_strict_mode=self.safety_strict_mode,
            session_timeout_minutes=self.session_timeout_minutes,
            max_concurrent_sessions=self.max_concurrent_sessions,
            memory_limit_mb=self.memory_limit_mb,
            cpu_limit_percent=self.cpu_limit_percent,
            enable_video=self.enable_video,
            enable_audio=self.enable_audio,
            enable_realtime_subtitles=self.enable_realtime_subtitles,
            enable_intent_detection=self.enable_intent_detection,
            use_veo3=self.use_veo3,
            veo3_api_key=self.veo3_api_key,
            avatar_style=self.avatar_style,
            voice_id=self.voice_id,
            latency_target_ms=self.latency_target_ms
        )

    def _create_webrtc_config(self) -> WebRTCConfig:
        """Create WebRTC configuration object."""
        return WebRTCConfig(
            stun_servers=self.stun_servers,
            turn_servers=self.turn_servers,
            enable_turn=self.enable_turn,
            turn_username=self.turn_username,
            turn_password=self.turn_password,
            connection_timeout=self.connection_timeout,
            max_connections=self.max_connections
        )

    def _create_logging_config(self) -> LoggingConfig:
        """Create logging configuration object."""
        return LoggingConfig(
            level=self.log_level,
            format=self.log_format,
            enable_structured_logging=self.enable_structured_logging,
            log_to_file=self.log_to_file,
            log_file_path=self.log_file_path
        )

    def get_provider_config_for_provider(self, provider_name: str) -> Dict[str, Any]:
        """Get configuration specific to a provider.

        Args:
            provider_name: Name of the provider (e.g., "gemini", "openai")

        Returns:
            Dictionary of provider-specific configuration
        """
        base_config = {
            "timeout": self.provider_timeout,
            "max_retries": self.max_retries,
            "backpressure_enabled": self.backpressure_enabled,
            "enable_video": self.enable_video,
            "enable_audio": self.enable_audio,
            "content_safety_enabled": self.content_safety_enabled,
            "safety_strict_mode": self.safety_strict_mode
        }

        # Provider-specific configurations
        provider_configs = {
            "gemini": {
                "api_key": self.gemini_api_key,
                "model": self.gemini_model,
                "veo_model": self.gemini_veo_model
            },
            "openai": {
                "api_key": self.openai_api_key,
                "model": self.openai_model
            },
            "anthropic": {
                "api_key": self.anthropic_api_key,
                "model": self.anthropic_model
            },
            "grok": {
                "api_key": self.grok_api_key,
                "model": self.grok_model
            }
        }

        if provider_name in provider_configs:
            base_config.update(provider_configs[provider_name])

        return base_config

    def validate_configuration(self) -> List[str]:
        """Validate the configuration and return list of issues.

        Returns:
            List of configuration issues (empty if valid)
        """
        issues = []

        # Check required API keys for enabled providers
        if not self.gemini_api_key and (self.gemini_model or self.gemini_veo_model):
            issues.append("GEMINI_API_KEY required for Gemini providers")

        if not self.openai_api_key and self.openai_model:
            issues.append("OPENAI_API_KEY required for OpenAI provider")

        if not self.anthropic_api_key and self.anthropic_model:
            issues.append("ANTHROPIC_API_KEY required for Anthropic provider")

        if not self.grok_api_key and self.grok_model:
            issues.append("GROK_API_KEY required for Grok provider")

        # Validate video+dialogue settings
        if self.use_veo3 == 1 and not self.veo3_api_key:
            issues.append("VEO3_API_KEY required when USE_VEO3=1")

        if self.latency_target_ms < 100:
            issues.append("LATENCY_TARGET_MS should be at least 100ms")

        # Validate resource limits
        if self.memory_limit_mb < 100:
            issues.append("MEMORY_LIMIT_MB should be at least 100MB")

        if self.cpu_limit_percent < 10 or self.cpu_limit_percent > 100:
            issues.append("CPU_LIMIT_PERCENT should be between 10 and 100")

        # Validate session limits
        if self.session_timeout_minutes < 5:
            issues.append("SESSION_TIMEOUT_MINUTES should be at least 5 minutes")

        if self.max_concurrent_sessions < 1:
            issues.append("MAX_CONCURRENT_SESSIONS should be at least 1")

        return issues


# Global configuration manager instance
config_manager = ConfigManager()
