#!/usr/bin/env bash
# Install MuseTalk 1.5 for the file-per-line avatar path. Not run automatically.
# Usage: MUSETALK_HOME=~/musetalk scripts/install_musetalk.sh
# Apple Silicon note: upstream targets CUDA; expect CPU/MPS-only inference to be slow. If the
# install or render fails the service degrades to D-ID (if DID_API_KEY is set), then audio-only.
set -euo pipefail
HOME_DIR="${MUSETALK_HOME:-$HOME/musetalk}"
[ -d "$HOME_DIR/.git" ] || git clone https://github.com/TMElyralab/MuseTalk.git "$HOME_DIR"
cd "$HOME_DIR"
python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
pip install --no-cache-dir -U openmim huggingface_hub
# MuseTalk 1.5 weights (unet, whisper, dwpose, face-parse, sd-vae, syncnet)
hf download TMElyralab/MuseTalk --local-dir models --include "musetalkV15/*" "musetalk/*"
hf download stabilityai/sd-vae-ft-mse --local-dir models/sd-vae --include "config.json" "diffusion_pytorch_model.bin"
hf download openai/whisper-tiny --local-dir models/whisper --include "config.json" "pytorch_model.bin" "preprocessor_config.json"
echo "MuseTalk installed at $HOME_DIR. Export MUSETALK_HOME=$HOME_DIR before starting the service."
