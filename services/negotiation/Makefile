# Makefile for Negotiation Service Test Harness

.PHONY: help install dev run clean test launch

PYTHON := python3
UV := uv

help:
	@echo "Available commands:"
	@echo "  install    - Install dependencies using uv"
	@echo "  dev        - Install dependencies in development mode using uv"
	@echo "  run        - Run the test harness server"
	@echo "  launch     - Interactive launch script with port management"
	@echo "  clean      - Clean up Python cache files"
	@echo "  test       - Run tests using pytest"

install:
	$(UV) sync
	@echo "Dependencies installed successfully using uv"

dev:
	$(UV) sync --dev
	@echo "Development dependencies installed successfully using uv"

run:
	@echo "Starting Enhanced AI Avatar test harness server..."
	@echo "Open http://localhost:8000 in your browser to test the AI avatar"
	$(PYTHON) enhanced_harness.py

run-simple:
	@echo "Starting Simple AI Avatar test harness server..."
	@echo "Open http://localhost:8000 in your browser to test the AI avatar"
	$(PYTHON) standalone_harness.py

clean:
	find . -type f -name "*.pyc" -delete
	find . -type d -name "__pycache__" -delete
	find . -type d -name "*.egg-info" -exec rm -rf {} +
	@echo "Cache files cleaned"

test:
	$(UV) run pytest tests/ -v

launch:
	@echo "Starting interactive launch script..."
	./launch.sh