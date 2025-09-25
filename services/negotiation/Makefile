# Makefile for Negotiation Service Test Harness

.PHONY: help install dev run clean test launch

PYTHON := python3
UV := uv

help:
	@echo "Available commands:"
	@echo "  install    - Install dependencies using uv"
	@echo "  dev        - Install dependencies in development mode using uv"
	@echo "  run        - Run the negotiation service"
	@echo "  video-gen  - Run AI video generator tool"
	@echo "  launch     - Interactive launch script with port management"
	@echo "  setup      - Run environment setup wizard"
	@echo "  clean      - Clean up Python cache files"
	@echo "  test       - Run tests using pytest"

install:
	$(UV) sync
	@echo "Dependencies installed successfully using uv"

dev:
	$(UV) sync --dev
	@echo "Development dependencies installed successfully using uv"

run:
	@echo "Starting negotiation service..."
	@echo "Open http://localhost:8000 in your browser to test"
	$(UV) run uvicorn main:app --host 127.0.0.1 --port 8000 --reload

video-gen:
	@echo "Running AI video generator..."
	$(UV) run python tools/real_video_generator.py

clean:
	find . -type f -name "*.pyc" -delete
	find . -type d -name "__pycache__" -delete
	find . -type d -name "*.egg-info" -exec rm -rf {} +
	@echo "Cache files cleaned"

test:
	$(UV) run pytest tests/ -v

launch:
	@echo "Starting interactive launch script..."
	./scripts/launch.sh

setup:
	@echo "Running environment setup..."
	./scripts/setup_env.sh