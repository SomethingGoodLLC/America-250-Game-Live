"""YAML middleware for FastAPI to handle application/x-yaml content type."""

from typing import Callable
from fastapi import Request, Response
from fastapi.responses import Response as FastAPIResponse
from starlette.middleware.base import BaseHTTPMiddleware

from .yaml_utils import yaml_helper


class YAMLResponse(FastAPIResponse):
    """Custom response class for YAML content."""
    
    media_type = "application/x-yaml"

    def render(self, content) -> bytes:
        if isinstance(content, dict):
            return yaml_helper.encode(content).encode("utf-8")
        return str(content).encode("utf-8")


class YAMLMiddleware(BaseHTTPMiddleware):
    """Middleware to handle YAML content type in requests and responses."""

    async def dispatch(self, request: Request, call_next: Callable) -> Response:
        # Handle YAML request body
        if request.headers.get("content-type") == "application/x-yaml":
            body = await request.body()
            if body:
                try:
                    yaml_data = yaml_helper.decode(body.decode("utf-8"))
                    # Store parsed YAML data in request state
                    request.state.yaml_data = yaml_data
                except Exception as e:
                    return Response(
                        content=f"Invalid YAML: {str(e)}",
                        status_code=400,
                        media_type="text/plain"
                    )

        response = await call_next(request)

        # Handle YAML response
        if request.headers.get("accept") == "application/x-yaml":
            if hasattr(response, "body") and response.media_type == "application/json":
                try:
                    import json
                    json_data = json.loads(response.body.decode("utf-8"))
                    yaml_content = yaml_helper.encode(json_data)
                    return Response(
                        content=yaml_content,
                        status_code=response.status_code,
                        headers=dict(response.headers),
                        media_type="application/x-yaml"
                    )
                except Exception:
                    pass  # Fall back to original response

        return response
