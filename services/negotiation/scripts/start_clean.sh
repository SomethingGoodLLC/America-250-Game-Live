#!/bin/bash

# AI Avatar Negotiation System - Clean Startup Script
# This script ensures a clean startup by handling all common issues

set -e  # Exit on any error

echo "🚀 AI Avatar Negotiation System - Clean Startup"
echo "================================================"

# Change to the correct directory
cd "$(dirname "$0")"
echo "📁 Working directory: $(pwd)"

# Function to check if port is in use
check_port() {
    local port=$1
    if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo "⚠️  Port $port is in use. Stopping existing processes..."
        pkill -f "uvicorn.*:$port" 2>/dev/null || true
        sleep 2
        if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1; then
            echo "❌ Failed to free port $port. Please manually stop the process:"
            lsof -Pi :$port -sTCP:LISTEN
            exit 1
        fi
    fi
}

# Function to clean Python cache
clean_cache() {
    echo "🧹 Cleaning Python cache files..."
    find . -name "*.pyc" -delete 2>/dev/null || true
    find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
    echo "✅ Cache cleaned"
}

# Function to validate Python syntax
validate_syntax() {
    echo "🔍 Validating Python syntax..."
    
    # Check main files
    local files=("main.py" "providers/video_sources/veo3_stream.py")
    
    for file in "${files[@]}"; do
        if [ -f "$file" ]; then
            if ! uv run python -m py_compile "$file" 2>/dev/null; then
                echo "❌ Syntax error in $file"
                uv run python -c "
import ast
try:
    with open('$file', 'r') as f:
        content = f.read()
    ast.parse(content)
    print('✅ $file syntax is valid')
except SyntaxError as e:
    print(f'❌ Syntax error in $file: {e}')
    print(f'Line {e.lineno}: {e.text}')
"
                exit 1
            fi
        fi
    done
    
    echo "✅ All Python files have valid syntax"
}

# Function to test imports
test_imports() {
    echo "📦 Testing module imports..."
    
    if ! uv run python -c "import main; print('✅ Main module imports successfully')" 2>/dev/null; then
        echo "❌ Failed to import main module. Checking dependencies..."
        uv run python -c "
try:
    import main
    print('✅ Main module imports successfully')
except Exception as e:
    print(f'❌ Import error: {e}')
    import traceback
    traceback.print_exc()
"
        exit 1
    fi
    
    echo "✅ All imports successful"
}

# Function to check dependencies
check_dependencies() {
    echo "📋 Checking dependencies..."
    
    # Check if uv is available
    if ! command -v uv &> /dev/null; then
        echo "❌ uv is not installed. Please install uv first."
        exit 1
    fi
    
    # Check if virtual environment exists
    if [ ! -d ".venv" ]; then
        echo "🔧 Creating virtual environment..."
        uv venv
    fi
    
    # Install dependencies
    echo "📦 Installing dependencies..."
    uv sync --quiet
    
    echo "✅ Dependencies ready"
}

# Function to set environment variables
set_environment() {
    echo "🌍 Setting environment variables..."
    
    # Set default values if not already set
    export LISTENER_TYPE="${LISTENER_TYPE:-local_stt}"
    export USE_VEO3="${USE_VEO3:-1}"
    export PYTHONPATH="${PYTHONPATH:-.}"
    
    echo "   LISTENER_TYPE: $LISTENER_TYPE"
    echo "   USE_VEO3: $USE_VEO3"
    echo "✅ Environment configured"
}

# Function to start server
start_server() {
    local port=${1:-8000}
    
    echo "🚀 Starting AI Avatar Negotiation Server on port $port..."
    echo "   Server will be available at: http://127.0.0.1:$port"
    echo "   Press Ctrl+C to stop the server"
    echo ""
    
    # Start the server with proper error handling
    if ! uv run uvicorn main:app --host 127.0.0.1 --port "$port" --reload; then
        echo ""
        echo "❌ Server failed to start. Common issues:"
        echo "   1. Port $port might be in use"
        echo "   2. Dependencies might be missing"
        echo "   3. Configuration might be incorrect"
        echo ""
        echo "🔧 Troubleshooting:"
        echo "   - Check if another server is running: lsof -i :$port"
        echo "   - Verify dependencies: uv run python -c 'import main'"
        echo "   - Check logs above for specific errors"
        exit 1
    fi
}

# Main execution
main() {
    local port=${1:-8000}
    
    # Run all checks and setup
    check_port "$port"
    clean_cache
    check_dependencies
    validate_syntax
    test_imports
    set_environment
    
    echo ""
    echo "🎉 All checks passed! Starting server..."
    echo ""
    
    # Start the server
    start_server "$port"
}

# Handle script arguments
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: $0 [PORT]"
    echo ""
    echo "Starts the AI Avatar Negotiation System with comprehensive checks."
    echo ""
    echo "Arguments:"
    echo "  PORT    Port number to run the server on (default: 8000)"
    echo ""
    echo "Environment Variables:"
    echo "  LISTENER_TYPE    Type of listener (default: local_stt)"
    echo "  USE_VEO3        Enable Veo3 video generation (default: 1)"
    echo ""
    echo "Examples:"
    echo "  $0              # Start on port 8000"
    echo "  $0 8080         # Start on port 8080"
    echo "  LISTENER_TYPE=real_llm $0  # Use real LLM listener"
    exit 0
fi

# Run main function with provided port or default
main "${1:-8000}"
