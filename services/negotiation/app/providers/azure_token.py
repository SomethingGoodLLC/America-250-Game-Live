"""Azure Speech Services token endpoint for browser-side SDK.

This module provides an endpoint to issue short-lived Azure Speech tokens
for browser-side use. The subscription key is kept server-side and never exposed.

Azure Speech SDK in the browser can use these tokens to:
- Synthesize speech with viseme events (for ARKit blendshapes)
- Perform speech recognition
- Access other Azure Cognitive Services

Endpoint: POST /azure/token
Returns: {"token": "...", "region": "..."}

Example Usage (JavaScript):
    const response = await fetch('/azure/token', { method: 'POST' });
    const { token, region } = await response.json();
    
    const speechConfig = SpeechSDK.SpeechConfig.fromAuthorizationToken(token, region);
    speechConfig.requestWordLevelTimestamps();
    
    const synthesizer = new SpeechSDK.SpeechSynthesizer(speechConfig);
"""

import asyncio
import time
from typing import Dict, Optional
from collections import defaultdict

import structlog
from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel

from core.settings import settings

logger = structlog.get_logger(__name__)
router = APIRouter()


class AzureTokenResponse(BaseModel):
    """Response model for Azure token endpoint."""
    token: str
    region: str
    expires_in: int = 600  # Tokens are valid for 10 minutes


class TokenRateLimiter:
    """Rate limiter for token issuance by client IP.
    
    Limits token requests to prevent abuse:
    - Max 10 tokens per IP per minute
    - Max 100 tokens per IP per hour
    """
    
    def __init__(
        self,
        requests_per_minute: int = 10,
        requests_per_hour: int = 100
    ):
        """Initialize rate limiter.
        
        Args:
            requests_per_minute: Max requests per minute per IP
            requests_per_hour: Max requests per hour per IP
        """
        self._requests_per_minute = requests_per_minute
        self._requests_per_hour = requests_per_hour
        
        # Track requests by IP: {ip: [(timestamp, ...]}
        self._minute_requests: Dict[str, list[float]] = defaultdict(list)
        self._hour_requests: Dict[str, list[float]] = defaultdict(list)
        
        self._lock = asyncio.Lock()
    
    async def check_rate_limit(self, client_ip: str) -> tuple[bool, Optional[str]]:
        """Check if request is allowed under rate limits.
        
        Args:
            client_ip: Client IP address
        
        Returns:
            Tuple of (allowed: bool, reason: Optional[str])
            If not allowed, reason contains the error message
        """
        async with self._lock:
            now = time.time()
            
            # Clean up old entries
            minute_ago = now - 60
            hour_ago = now - 3600
            
            # Filter minute requests
            self._minute_requests[client_ip] = [
                t for t in self._minute_requests[client_ip]
                if t > minute_ago
            ]
            
            # Filter hour requests
            self._hour_requests[client_ip] = [
                t for t in self._hour_requests[client_ip]
                if t > hour_ago
            ]
            
            # Check limits
            minute_count = len(self._minute_requests[client_ip])
            hour_count = len(self._hour_requests[client_ip])
            
            if minute_count >= self._requests_per_minute:
                return False, f"Rate limit exceeded: {self._requests_per_minute} requests per minute"
            
            if hour_count >= self._requests_per_hour:
                return False, f"Rate limit exceeded: {self._requests_per_hour} requests per hour"
            
            # Record this request
            self._minute_requests[client_ip].append(now)
            self._hour_requests[client_ip].append(now)
            
            logger.debug(
                "Token rate limit check passed",
                client_ip=client_ip,
                minute_count=minute_count + 1,
                hour_count=hour_count + 1
            )
            
            return True, None


# Global rate limiter instance
token_rate_limiter = TokenRateLimiter(
    requests_per_minute=10,
    requests_per_hour=100
)


@router.post("/azure/token", response_model=AzureTokenResponse)
async def get_azure_speech_token(request: Request):
    """Issue a short-lived Azure Speech token for browser-side SDK usage.
    
    This endpoint allows browser clients to obtain Azure Speech tokens
    without exposing the subscription key. Tokens are valid for 10 minutes.
    
    The token can be used with:
    - Azure Speech SDK for TTS with viseme events (ARKit blendshapes)
    - Azure Speech SDK for STT
    - Other Azure Cognitive Services APIs
    
    Rate Limits:
    - 10 requests per minute per IP
    - 100 requests per hour per IP
    
    Security:
    - Subscription key never leaves the server
    - Tokens expire after 10 minutes
    - Rate limiting prevents abuse
    - Client IP tracking
    
    Returns:
        JSON with token, region, and expiry information
    
    Raises:
        HTTPException: If service not configured or rate limit exceeded
    
    Example Response:
        {
            "token": "eyJ0eXAiOiJKV1QiLCJhbGc...",
            "region": "eastus",
            "expires_in": 600
        }
    """
    client_ip = request.client.host if request.client else "unknown"
    
    logger.info("Azure token request", client_ip=client_ip)
    
    # Check configuration
    if not settings.azure_speech_key:
        logger.error("AZURE_SPEECH_KEY not configured")
        raise HTTPException(
            status_code=503,
            detail="Azure Speech Service not configured"
        )
    
    if not settings.azure_speech_region:
        logger.error("AZURE_SPEECH_REGION not configured")
        raise HTTPException(
            status_code=503,
            detail="Azure Speech Service region not configured"
        )
    
    # Check rate limit
    allowed, reason = await token_rate_limiter.check_rate_limit(client_ip)
    if not allowed:
        logger.warning("Token rate limit exceeded", client_ip=client_ip, reason=reason)
        raise HTTPException(status_code=429, detail=reason)
    
    try:
        # Issue token from Azure
        # Azure token endpoint: https://{region}.api.cognitive.microsoft.com/sts/v1.0/issueToken
        import httpx
        
        token_url = f"https://{settings.azure_speech_region}.api.cognitive.microsoft.com/sts/v1.0/issueToken"
        
        async with httpx.AsyncClient() as client:
            response = await client.post(
                token_url,
                headers={
                    "Ocp-Apim-Subscription-Key": settings.azure_speech_key,
                    "Content-Type": "application/x-www-form-urlencoded",
                },
                timeout=10.0
            )
            
            if response.status_code != 200:
                logger.error(
                    "Failed to issue Azure token",
                    status_code=response.status_code,
                    response=response.text
                )
                raise HTTPException(
                    status_code=500,
                    detail=f"Failed to issue token: {response.status_code}"
                )
            
            # Token is returned as plain text
            token = response.text
            
            logger.info(
                "Azure token issued successfully",
                client_ip=client_ip,
                region=settings.azure_speech_region,
                token_length=len(token)
            )
            
            return AzureTokenResponse(
                token=token,
                region=settings.azure_speech_region,
                expires_in=600  # Azure tokens expire after 10 minutes
            )
    
    except httpx.HTTPError as e:
        logger.error("HTTP error issuing Azure token", error=str(e), client_ip=client_ip)
        raise HTTPException(
            status_code=500,
            detail=f"Failed to communicate with Azure: {str(e)}"
        )
    
    except Exception as e:
        logger.error("Unexpected error issuing Azure token", error=str(e), client_ip=client_ip)
        raise HTTPException(
            status_code=500,
            detail=f"Internal error: {str(e)}"
        )


@router.get("/azure/regions")
async def list_azure_regions():
    """List available Azure Speech Service regions.
    
    Returns a list of Azure regions where Speech Services are available.
    Useful for clients that want to select optimal regions.
    
    Returns:
        JSON list of available regions with metadata
    """
    regions = [
        {"id": "eastus", "name": "East US", "location": "Virginia"},
        {"id": "eastus2", "name": "East US 2", "location": "Virginia"},
        {"id": "westus", "name": "West US", "location": "California"},
        {"id": "westus2", "name": "West US 2", "location": "Washington"},
        {"id": "centralus", "name": "Central US", "location": "Iowa"},
        {"id": "northcentralus", "name": "North Central US", "location": "Illinois"},
        {"id": "southcentralus", "name": "South Central US", "location": "Texas"},
        {"id": "westcentralus", "name": "West Central US", "location": "Wyoming"},
        {"id": "canadacentral", "name": "Canada Central", "location": "Toronto"},
        {"id": "brazilsouth", "name": "Brazil South", "location": "São Paulo"},
        {"id": "northeurope", "name": "North Europe", "location": "Ireland"},
        {"id": "westeurope", "name": "West Europe", "location": "Netherlands"},
        {"id": "uksouth", "name": "UK South", "location": "London"},
        {"id": "francecentral", "name": "France Central", "location": "Paris"},
        {"id": "germanywestcentral", "name": "Germany West Central", "location": "Frankfurt"},
        {"id": "switzerlandnorth", "name": "Switzerland North", "location": "Zurich"},
        {"id": "norwayeast", "name": "Norway East", "location": "Oslo"},
        {"id": "southeastasia", "name": "Southeast Asia", "location": "Singapore"},
        {"id": "eastasia", "name": "East Asia", "location": "Hong Kong"},
        {"id": "australiaeast", "name": "Australia East", "location": "Sydney"},
        {"id": "japaneast", "name": "Japan East", "location": "Tokyo"},
        {"id": "japanwest", "name": "Japan West", "location": "Osaka"},
        {"id": "koreacentral", "name": "Korea Central", "location": "Seoul"},
        {"id": "centralindia", "name": "Central India", "location": "Pune"},
        {"id": "southafricanorth", "name": "South Africa North", "location": "Johannesburg"},
        {"id": "uaenorth", "name": "UAE North", "location": "Dubai"},
    ]
    
    return JSONResponse(content={"regions": regions})


@router.get("/azure/voices")
async def list_azure_voices():
    """List available Azure neural voices.
    
    Fetches the list of available neural voices from Azure Speech Service.
    This requires a valid subscription key.
    
    Returns:
        JSON list of available voices with metadata
    """
    if not settings.azure_speech_key or not settings.azure_speech_region:
        raise HTTPException(
            status_code=503,
            detail="Azure Speech Service not configured"
        )
    
    try:
        import httpx
        
        voices_url = f"https://{settings.azure_speech_region}.tts.speech.microsoft.com/cognitiveservices/voices/list"
        
        async with httpx.AsyncClient() as client:
            response = await client.get(
                voices_url,
                headers={
                    "Ocp-Apim-Subscription-Key": settings.azure_speech_key
                },
                timeout=10.0
            )
            response.raise_for_status()
            
            voices = response.json()
            
            # Filter to neural voices only (best quality)
            neural_voices = [
                v for v in voices
                if "Neural" in v.get("VoiceType", "")
            ]
            
            logger.info("Fetched Azure voices", total=len(voices), neural=len(neural_voices))
            return JSONResponse(content={"voices": neural_voices})
    
    except Exception as e:
        logger.error("Failed to fetch Azure voices", error=str(e))
        raise HTTPException(
            status_code=500,
            detail=f"Failed to fetch voices: {str(e)}"
        )

