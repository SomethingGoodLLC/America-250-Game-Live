@echo off
REM 🚀 Turn-Key AI Avatar Negotiation Test Harness Startup Script (Windows)
REM This script starts everything you need for the complete negotiation system

setlocal enabledelayedexpansion

echo ================================
echo 🤖 AI Avatar Negotiation Test Harness
echo ================================
echo Starting turn-key test environment...
echo.

REM Check if we're in the right directory
if not exist "main.py" (
    echo [ERROR] main.py not found. Please run this script from the services/negotiation directory.
    pause
    exit /b 1
)

REM Step 1: Check dependencies
echo [INFO] Checking dependencies...
where uv >nul 2>&1
if errorlevel 1 (
    echo [ERROR] uv not found. Please install uv first:
    echo   Visit: https://docs.astral.sh/uv/getting-started/installation/
    pause
    exit /b 1
)
echo [SUCCESS] uv found

REM Step 2: Install dependencies
echo [INFO] Installing dependencies...
uv sync --quiet
if errorlevel 1 (
    echo [ERROR] Failed to install dependencies
    pause
    exit /b 1
)
echo [SUCCESS] Dependencies installed

REM Step 3: Check for optional dependencies
echo [INFO] Checking optional dependencies...
uv run python -c "import faster_whisper; import numpy" >nul 2>&1
if errorlevel 1 (
    echo [WARNING] faster-whisper/numpy not available - using mock STT mode
    echo   To enable real STT: uv add faster-whisper numpy torch
) else (
    echo [SUCCESS] faster-whisper and numpy available - real STT enabled
)

REM Step 4: Check environment variables
echo [INFO] Checking environment configuration...
if "%LISTENER_TYPE%"=="" (
    set LISTENER_TYPE=local_stt
    echo [INFO] Using default listener: local_stt
) else (
    echo [INFO] Using listener: %LISTENER_TYPE%
)

REM Check API keys based on listener type
if "%LISTENER_TYPE%"=="gemini_realtime" (
    if "%GEMINI_API_KEY%"=="" (
        echo [WARNING] GEMINI_API_KEY not set - will use mock mode
    ) else (
        echo [SUCCESS] Gemini API key configured
    )
)

if "%LISTENER_TYPE%"=="openai_realtime" (
    if "%OPENAI_API_KEY%"=="" (
        echo [WARNING] OPENAI_API_KEY not set - will use mock mode
    ) else (
        echo [SUCCESS] OpenAI API key configured
    )
)

if "%LISTENER_TYPE%"=="grok_realtime" (
    if "%GROK_API_KEY%"=="" (
        echo [WARNING] GROK_API_KEY not set - will use mock mode
    ) else (
        echo [SUCCESS] Grok API key configured
    )
)

REM Step 5: Check if port is available
echo [INFO] Checking port 8000...
netstat -an | find "127.0.0.1:8000" | find "LISTENING" >nul 2>&1
if not errorlevel 1 (
    echo [WARNING] Port 8000 is already in use. Please free it manually or use a different port.
    echo You can kill existing processes using Task Manager or:
    echo   netstat -ano ^| findstr :8000
    echo   taskkill /PID ^<PID^> /F
    pause
    exit /b 1
)
echo [SUCCESS] Port 8000 is available

REM Step 6: Start the server
echo.
echo ================================
echo 🚀 Starting FastAPI Server
echo ================================
echo Server will be available at: http://127.0.0.1:8000
echo Press Ctrl+C to stop the server
echo.
echo [SUCCESS] Starting uvicorn server...
echo ================================

uv run uvicorn main:app --host 127.0.0.1 --port 8000 --reload

pause
