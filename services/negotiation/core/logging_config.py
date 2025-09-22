"""Logging configuration with structlog."""

import logging
import sys
from typing import Any, Dict

import structlog


def setup_logging() -> None:
    """Configure structured logging with JSON output."""
    # Configure standard library logging
    logging.basicConfig(
        format="%(message)s",
        stream=sys.stdout,
        level=logging.INFO,
    )

    # Configure structlog
    structlog.configure(
        processors=[
            structlog.stdlib.add_log_level,
            structlog.stdlib.add_logger_name,
            structlog.processors.TimeStamper(fmt="iso"),
            structlog.processors.StackInfoRenderer(),
            structlog.processors.format_exc_info,
            structlog.processors.JSONRenderer()
        ],
        context_class=dict,
        logger_factory=structlog.stdlib.LoggerFactory(),
        wrapper_class=structlog.stdlib.BoundLogger,
        cache_logger_on_first_use=True,
    )


class CorrelationIdFilter(logging.Filter):
    """Filter to add correlation IDs to log records."""

    def filter(self, record: logging.LogRecord) -> bool:
        # Add correlation ID if not present
        if not hasattr(record, 'correlation_id'):
            record.correlation_id = getattr(self, 'correlation_id', 'unknown')
        return True


def get_logger_with_correlation(correlation_id: str) -> Any:
    """Get a logger with a correlation ID bound to it."""
    return structlog.get_logger().bind(correlation_id=correlation_id)


def bind_correlation_id(correlation_id: str) -> None:
    """Bind a correlation ID to the current context."""
    structlog.contextvars.bind_contextvars(correlation_id=correlation_id)
