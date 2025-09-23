#!/bin/bash

# Samson Negotiation Service Launch Script
# This script provides easy ways to start the negotiation service

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
PORT=8000
HOST="127.0.0.1"
SERVICE="simple_test_harness"
RELOAD="--reload"

# Function to print colored output
print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Function to check if port is in use
check_port() {
    if lsof -Pi :$1 -sTCP:LISTEN -t >/dev/null 2>&1; then
        return 0  # Port is in use
    else
        return 1  # Port is free
    fi
}

# Function to kill process on port
kill_port() {
    local port=$1
    print_warning "Port $port is in use. Attempting to free it..."
    
    # Find and kill the process
    local pid=$(lsof -ti:$port)
    if [ ! -z "$pid" ]; then
        kill -9 $pid 2>/dev/null || true
        sleep 2
        if check_port $port; then
            print_error "Failed to free port $port"
            return 1
        else
            print_success "Port $port freed successfully"
            return 0
        fi
    fi
}

# Function to show usage
show_usage() {
    echo "Samson Negotiation Service Launch Script"
    echo ""
    echo "Usage: $0 [OPTIONS] [SERVICE]"
    echo ""
    echo "Services:"
    echo "  simple        Simple test harness (default)"
    echo "  enhanced      Enhanced test harness with full UI"
    echo "  app           Main FastAPI application"
    echo "  standalone    Standalone harness"
    echo ""
    echo "Options:"
    echo "  -p, --port PORT     Port to run on (default: 8000)"
    echo "  -h, --host HOST     Host to bind to (default: 127.0.0.1)"
    echo "  --no-reload         Disable auto-reload"
    echo "  --kill-port         Kill any process using the target port"
    echo "  --help              Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                          # Start simple test harness on port 8000"
    echo "  $0 enhanced                 # Start enhanced test harness"
    echo "  $0 -p 8080 simple          # Start on port 8080"
    echo "  $0 --kill-port enhanced     # Kill port 8000 and start enhanced harness"
    echo ""
}

# Parse command line arguments
KILL_PORT=false
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -h|--host)
            HOST="$2"
            shift 2
            ;;
        --no-reload)
            RELOAD=""
            shift
            ;;
        --kill-port)
            KILL_PORT=true
            shift
            ;;
        --help)
            show_usage
            exit 0
            ;;
        simple|enhanced|app|standalone)
            SERVICE="$1"
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Map service names to modules
case $SERVICE in
    simple)
        MODULE="simple_test_harness:app"
        DESCRIPTION="Simple Test Harness"
        ;;
    enhanced)
        MODULE="enhanced_harness:app"
        DESCRIPTION="Enhanced Test Harness"
        ;;
    app)
        MODULE="app.main:app"
        DESCRIPTION="Main FastAPI Application"
        ;;
    standalone)
        MODULE="standalone_harness:app"
        DESCRIPTION="Standalone Harness"
        ;;
    *)
        print_error "Unknown service: $SERVICE"
        show_usage
        exit 1
        ;;
esac

# Change to the negotiation service directory
cd "$(dirname "$0")"

print_info "Starting Samson Negotiation Service"
print_info "Service: $DESCRIPTION"
print_info "URL: http://$HOST:$PORT"

# Check if port is in use
if check_port $PORT; then
    if [ "$KILL_PORT" = true ]; then
        if ! kill_port $PORT; then
            exit 1
        fi
    else
        print_error "Port $PORT is already in use!"
        print_info "Use --kill-port to automatically free the port, or choose a different port with -p"
        print_info "To see what's using the port: lsof -i :$PORT"
        exit 1
    fi
fi

# Check if uv is available
if ! command -v uv &> /dev/null; then
    print_error "uv is not installed or not in PATH"
    print_info "Install uv: curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi

# Install dependencies if needed
if [ ! -d ".venv" ]; then
    print_info "Virtual environment not found. Installing dependencies..."
    uv sync
    print_success "Dependencies installed"
fi

# Start the service
print_info "Starting $DESCRIPTION on http://$HOST:$PORT"
print_info "Press Ctrl+C to stop the server"
echo ""

# Run the service
exec uv run uvicorn $MODULE --host $HOST --port $PORT $RELOAD
