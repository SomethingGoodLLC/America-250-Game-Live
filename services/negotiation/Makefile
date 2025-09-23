# Makefile for Negotiation Service Test Harness

.PHONY: help install run clean test

PYTHON := python3
PIP := pip3

help:
	@echo "Available commands:"
	@echo "  install    - Install dependencies"
	@echo "  run        - Run the test harness server"
	@echo "  clean      - Clean up Python cache files"
	@echo "  test       - Run tests"

install:
	$(PIP) install -r requirements-providers.txt
	@echo "Dependencies installed successfully"

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
	pytest tests/ -v