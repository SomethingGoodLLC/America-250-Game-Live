.PHONY: help dev test run fmt type check clean docker-build docker-run

# Default target
help:
	@echo "Available commands:"
	@echo "  dev      - Run development server"
	@echo "  test     - Run tests"
	@echo "  run      - Run production server"
	@echo "  fmt      - Format code with black and ruff"
	@echo "  type     - Type check with mypy"
	@echo "  check    - Run all checks (fmt, type, test)"
	@echo "  clean    - Clean up build artifacts"
	@echo "  docker-build - Build Docker image"
	@echo "  docker-run   - Run with Docker Compose"

# Development
dev:
	uv run uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload

# Testing
test:
	uv run pytest tests/ -v --cov=.

test-watch:
	uv run pytest-watch tests/ -v

# Production
run:
	uv run uvicorn app.main:app --host 0.0.0.0 --port 8000 --workers 1

# Code formatting
fmt:
	uv run black .
	uv run ruff check --fix .

# Type checking
type:
	uv run mypy .

# All checks
check: fmt type test

# Clean up
clean:
	rm -rf .coverage
	rm -rf htmlcov
	rm -rf .pytest_cache
	rm -rf __pycache__
	rm -rf .mypy_cache
	find . -type d -name __pycache__ -exec rm -rf {} +

# Docker
docker-build:
	docker build -t samson-negotiation-service .

docker-run:
	docker-compose up --build

# Install dependencies
install:
	uv sync

install-dev:
	uv sync --dev
	uv run pre-commit install

# Pre-commit hooks
pre-commit:
	uv run pre-commit run --all-files

pre-commit-update:
	uv run pre-commit autoupdate
