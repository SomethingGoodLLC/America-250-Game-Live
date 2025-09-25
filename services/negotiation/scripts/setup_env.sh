#!/bin/bash
# 🔧 Environment Setup Script for AI Avatar Negotiation System
# This script helps you configure environment variables for different listeners

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${PURPLE}================================${NC}"
    echo -e "${PURPLE}$1${NC}"
    echo -e "${PURPLE}================================${NC}"
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[TIP]${NC} $1"
}

print_header "🔧 Environment Setup for AI Avatar System"
echo

# Create .env file if it doesn't exist
if [[ ! -f ".env" ]]; then
    print_info "Creating .env file..."
    touch .env
fi

echo "Choose your listener configuration:"
echo
echo "1) Local STT (faster-whisper) - No API keys needed"
echo "2) Gemini Realtime - Requires Gemini API key"
echo "3) OpenAI Realtime - Requires OpenAI API key"  
echo "4) Grok Realtime - Requires Grok API key"
echo "5) Custom configuration"
echo

read -p "Enter your choice (1-5): " choice

case $choice in
    1)
        echo "LISTENER_TYPE=local_stt" > .env
        print_success "Configured for Local STT"
        print_warning "Install dependencies: uv add faster-whisper numpy torch"
        ;;
    2)
        echo "LISTENER_TYPE=gemini_realtime" > .env
        read -p "Enter your Gemini API key: " api_key
        echo "GEMINI_API_KEY=$api_key" >> .env
        print_success "Configured for Gemini Realtime"
        ;;
    3)
        echo "LISTENER_TYPE=openai_realtime" > .env
        read -p "Enter your OpenAI API key: " api_key
        echo "OPENAI_API_KEY=$api_key" >> .env
        print_success "Configured for OpenAI Realtime"
        ;;
    4)
        echo "LISTENER_TYPE=grok_realtime" > .env
        read -p "Enter your Grok API key: " api_key
        echo "GROK_API_KEY=$api_key" >> .env
        print_success "Configured for Grok Realtime"
        ;;
    5)
        print_info "Manual configuration mode"
        echo "# AI Avatar Negotiation System Configuration" > .env
        echo "# Listener Types: local_stt, gemini_realtime, openai_realtime, grok_realtime" >> .env
        echo "LISTENER_TYPE=local_stt" >> .env
        echo "" >> .env
        echo "# API Keys (uncomment and set as needed)" >> .env
        echo "# GEMINI_API_KEY=your_gemini_api_key_here" >> .env
        echo "# OPENAI_API_KEY=your_openai_api_key_here" >> .env
        echo "# GROK_API_KEY=your_grok_api_key_here" >> .env
        echo "" >> .env
        echo "# Veo3 Configuration" >> .env
        echo "# USE_VEO3=1" >> .env
        print_success "Created template .env file - please edit it manually"
        ;;
    *)
        print_warning "Invalid choice. Creating default configuration..."
        echo "LISTENER_TYPE=local_stt" > .env
        ;;
esac

echo
print_header "📋 Configuration Summary"
echo "Current .env file contents:"
echo -e "${CYAN}$(cat .env)${NC}"
echo

print_header "🚀 Next Steps"
echo "1. Run: ./start.sh"
echo "2. Open: http://localhost:8000"
echo "3. Test your AI avatar negotiation system!"
echo

print_warning "To modify configuration later, edit the .env file or run this script again"
