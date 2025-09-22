# Git Guidelines for Samson Project

## ✅ Files That SHOULD Be Committed

### Core Project Files
- `pyproject.toml` - Project configuration and dependencies
- `.cursorrules` - Project-specific AI assistant rules
- `README.md` - Project documentation
- `SECURITY.md` - Security guidelines
- `Makefile` - Build and development commands

### Source Code
- `services/negotiation/app/` - FastAPI application code
- `services/negotiation/core/` - Core business logic
- `services/negotiation/providers/` - Negotiation providers
- `services/negotiation/stt/` - Speech-to-text interfaces
- `services/negotiation/tts/` - Text-to-speech interfaces
- `services/negotiation/schemas/` - Pydantic models

### Configuration Templates
- `services/negotiation/env.example` - Environment variable template
- `services/negotiation/docker-compose.yml` - Docker orchestration
- `services/negotiation/Dockerfile` - Container configuration

### Protocol Definitions
- `protocol/schemas/*.yaml` - YAML schema definitions for all data structures

### Tests
- `services/negotiation/tests/` - All test files
- `services/negotiation/conftest.py` - Test configuration

### Documentation
- `services/negotiation/README.md` - Service-specific documentation
- `services/negotiation/IMPROVEMENTS.md` - Improvement documentation

## ❌ Files That Should NOT Be Committed

### Environment & Secrets
- `.env` - Environment variables with secrets
- `.env.local` - Local environment overrides
- `services/negotiation/.env` - Service-specific environment files
- `secrets/` - Directory containing secret files
- `*.key` - Private keys
- `*.pem` - SSL certificates
- `credentials/` - Credential files

### Generated Files
- `__pycache__/` - Python bytecode cache
- `*.pyc` - Compiled Python files
- `.pytest_cache/` - Pytest cache
- `.mypy_cache/` - MyPy type checker cache
- `.coverage` - Coverage reports
- `htmlcov/` - Coverage HTML reports

### IDE & Editor Files
- `.vscode/` - VS Code settings
- `.idea/` - PyCharm/IntelliJ settings
- `*.swp` - Vim swap files
- `.DS_Store` - macOS metadata

### Dependencies & Build Artifacts
- `.venv/` - Virtual environment
- `venv/` - Virtual environment
- `node_modules/` - Node.js dependencies
- `dist/` - Build distributions
- `build/` - Build artifacts

### Runtime & Data Files
- `logs/` - Log files
- `*.log` - Individual log files
- `models/` - AI model files (may contain sensitive data)
- `data/` - Runtime data
- `recordings/` - Audio/video recordings
- `session_data/` - Session persistence files

### Media Files
- `*.mp3` - Audio files
- `*.mp4` - Video files
- `*.wav` - Audio files
- `*.avi` - Video files

## 🔍 Verification Commands

### Check Git Status
```bash
git status --porcelain
```

### See What Would Be Committed
```bash
git add . --dry-run
```

### Check Ignored Files
```bash
git status --ignored
```

### Verify No Secrets in Staged Files
```bash
git diff --cached | grep -i "api_key\|secret\|password\|token"
```

## 🚨 Before Committing Checklist

- [ ] No `.env` files are staged
- [ ] No API keys or secrets in code
- [ ] No large binary files (models, media)
- [ ] No IDE-specific files
- [ ] No generated/cache files
- [ ] All tests pass: `make test`
- [ ] Code is formatted: `make fmt`
- [ ] Type checking passes: `make type`

## 🛠️ Useful Git Commands

### Remove Accidentally Committed Secrets
```bash
# Remove from staging
git reset HEAD .env

# Remove from history (dangerous!)
git filter-branch --force --index-filter 'git rm --cached --ignore-unmatch .env' --prune-empty --tag-name-filter cat -- --all
```

### Check for Secrets in History
```bash
git log --all --full-history -- .env
git log -p --all -S "api_key" --source --all
```

### Clean Up Ignored Files
```bash
git clean -fX  # Remove ignored files
git clean -fx  # Remove all untracked files (be careful!)
```

## 📋 Repository Structure

```
Samson/
├── .gitignore              ✅ Commit
├── .cursorrules           ✅ Commit
├── pyproject.toml         ✅ Commit
├── README.md              ✅ Commit
├── SECURITY.md            ✅ Commit
├── .env                   ❌ Never commit
├── protocol/
│   └── schemas/           ✅ Commit YAML schemas
└── services/
    └── negotiation/
        ├── app/           ✅ Commit source code
        ├── core/          ✅ Commit source code
        ├── providers/     ✅ Commit source code
        ├── tests/         ✅ Commit tests
        ├── .env           ❌ Never commit
        ├── env.example    ✅ Commit template
        └── logs/          ❌ Never commit
```

Remember: **When in doubt, don't commit it!** You can always add files later, but removing secrets from git history is much harder.
