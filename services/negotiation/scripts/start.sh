#!/bin/bash
# 🚀 Turn-Key AI Avatar Negotiation Test Harness Startup Script
# This script starts everything you need for the complete negotiation system

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${PURPLE}================================${NC}"
    echo -e "${PURPLE}$1${NC}"
    echo -e "${PURPLE}================================${NC}"
}

# Check if we're in the right directory
if [[ ! -f "main.py" ]]; then
    print_error "main.py not found. Please run this script from the services/negotiation directory."
    exit 1
fi

print_header "🤖 AI Avatar Negotiation Test Harness"
echo -e "${CYAN}Starting turn-key test environment...${NC}"
echo

# Step 1: Check dependencies
print_status "Checking dependencies..."
if ! command -v uv &> /dev/null; then
    print_error "uv not found. Please install uv first:"
    echo "  curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi
print_success "uv found"

# Step 2: Install dependencies
print_status "Installing dependencies..."
if uv sync --quiet; then
    print_success "Dependencies installed"
else
    print_error "Failed to install dependencies"
    exit 1
fi

# Step 3: Check for optional dependencies
print_status "Checking optional dependencies..."
WHISPER_AVAILABLE=false
if uv run python -c "import faster_whisper; import numpy" 2>/dev/null; then
    WHISPER_AVAILABLE=true
    print_success "faster-whisper and numpy available - real STT enabled"
else
    print_warning "faster-whisper/numpy not available - using mock STT mode"
    echo "  To enable real STT: uv add faster-whisper numpy torch"
fi

# Step 4: Check environment variables
print_status "Checking environment configuration..."
ENV_WARNINGS=()

if [[ -z "${LISTENER_TYPE}" ]]; then
    export LISTENER_TYPE="local_stt"
    print_status "Using default listener: local_stt"
else
    print_status "Using listener: ${LISTENER_TYPE}"
fi

# Check API keys based on listener type
case "${LISTENER_TYPE}" in
    "gemini_realtime")
        if [[ -z "${GEMINI_API_KEY}" ]]; then
            ENV_WARNINGS+=("GEMINI_API_KEY not set - will use mock mode")
        else
            print_success "Gemini API key configured"
        fi
        ;;
    "openai_realtime")
        if [[ -z "${OPENAI_API_KEY}" ]]; then
            ENV_WARNINGS+=("OPENAI_API_KEY not set - will use mock mode")
        else
            print_success "OpenAI API key configured"
        fi
        ;;
    "grok_realtime")
        if [[ -z "${GROK_API_KEY}" ]]; then
            ENV_WARNINGS+=("GROK_API_KEY not set - will use mock mode")
        else
            print_success "Grok API key configured"
        fi
        ;;
    "local_stt")
        if [[ "${WHISPER_AVAILABLE}" == "true" ]]; then
            print_success "Local STT fully configured"
        else
            ENV_WARNINGS+=("faster-whisper not available - will use mock mode")
        fi
        ;;
esac

# Print environment warnings
for warning in "${ENV_WARNINGS[@]}"; do
    print_warning "$warning"
done

# Step 5: Check if port is available
print_status "Checking port 8000..."
if lsof -Pi :8000 -sTCP:LISTEN -t >/dev/null 2>&1; then
    print_warning "Port 8000 is already in use. Attempting to free it..."
    # Try to kill existing uvicorn processes
    pkill -f "uvicorn.*8000" 2>/dev/null || true
    sleep 2
    
    if lsof -Pi :8000 -sTCP:LISTEN -t >/dev/null 2>&1; then
        print_error "Port 8000 is still in use. Please free it manually or use a different port."
        exit 1
    fi
fi
print_success "Port 8000 is available"

# Step 6: Start the server
print_header "🚀 Starting FastAPI Server"
print_status "Server will be available at: http://127.0.0.1:8000"
print_status "Press Ctrl+C to stop the server"
echo

# Create a trap to handle cleanup
cleanup() {
    echo
    print_status "Shutting down server..."
    exit 0
}
trap cleanup SIGINT SIGTERM

# Start the server
print_success "Starting uvicorn server..."
echo -e "${CYAN}================================${NC}"
uv run uvicorn main:app --host 127.0.0.1 --port 8000 --reload
