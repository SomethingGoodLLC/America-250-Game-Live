#!/usr/bin/env bash
# Side-by-side proof: the same ~10 s envoy line on the painting, old 16-shape flipbook vs new warp.
#   scripts/lipsync_compare.sh            -> docs/preview/lipsync_compare.mp4 (left: flipbook, right: warp)
# 1. ElevenLabs voices the line (with character timings) and the service code turns them into viseme curves.
# 2. L_DiplomacyRoom runs WITH sound and -PA250LipCompare=<wav>: plays the line twice (flipbook, then warp) and
#    saves frames at ~15 fps with their playback time.
# 3. ffmpeg crops the painting from each frame, times the frames to the audio and stacks the two passes.
# Editor closed; no other Unreal game running. (*.mp4 is git-ignored at the repo root.)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
GAME="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
DIR="$ROOT/unreal/PlayAmerica250/Saved/PA250/lipcompare"
LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/lipcompare_game.log"
OUT="$ROOT/docs/preview/lipsync_compare.mp4"
LINE="${LINE:-You ask me to open the gates of Boston, sir, for a purse and a pretty title. I have served His Majesty for thirty years, and I will not be bought so cheaply. Make me a real offer, or let us speak of prisoners.}"
rm -rf "$DIR"; mkdir -p "$DIR" "$(dirname "$OUT")"

echo "== voicing the line"
(cd "$ROOT/services/negotiation" && LINE="$LINE" DIR="$DIR" .venv/bin/python - <<'PY'
import asyncio, json, os, shutil, sys
sys.path.insert(0, ".")
from dotenv import load_dotenv; load_dotenv(".env")
os.environ["PA250_TTS"] = "elevenlabs"
from diplomacy import avatar
from diplomacy.visemes import curves_from_alignment
out = asyncio.run(avatar.synthesize_line_audio(os.environ["LINE"], "lipcompare", envoy_id="british"))
if not out or not out[2]:
    sys.exit("ElevenLabs gave no audio/timings")
wav = os.path.join(os.environ["DIR"], "line.wav")
shutil.move(out[0], wav)
secs = avatar.wav_seconds(wav)
open(wav + ".visemes.json", "w").write(json.dumps(curves_from_alignment(out[2], secs)))
print(f"line.wav {secs:.2f}s with viseme curves")
PY
) || exit 1

echo "== running the room (flipbook pass, then warp pass)"
rm -f "$ROOT/unreal/PlayAmerica250/Saved/PA250/lipcompare_result.json"
"$GAME" "$PROJECT" /Game/PA250/Maps/L_DiplomacyRoom -game -windowed -ResX=1280 -ResY=720 -nosplash \
  "-PA250LipCompare=$DIR/line.wav" "-PA250Service=127.0.0.1:1" "-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0" \
  -log "-abslog=$LOG" >/dev/null 2>&1 &
for _ in $(seq 1 240); do [ -f "$ROOT/unreal/PlayAmerica250/Saved/PA250/lipcompare_result.json" ] && break; sleep 1; done
sleep 2; pkill -f "PA250LipCompare" 2>/dev/null
grep -E "EnvoyFace|LipCompare|EnvoyVoice (started|finished|stalled)" "$LOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | cut -c1-200
[ -f "$DIR/flipbook_times.txt" ] && [ -f "$DIR/warp_times.txt" ] || { echo "capture failed"; exit 1; }

echo "== assembling $OUT"
for pass in flipbook warp; do
  awk -v d="$DIR" 'NR>1{printf "file %c%s/%s%c\nduration %.4f\n", 39, d, prev, 39, $2-pt} {prev=$1; pt=$2} END{printf "file %c%s/%s%c\n", 39, d, prev, 39}' \
    "$DIR/${pass}_times.txt" > "$DIR/${pass}_concat.txt"
  first=$(head -1 "$DIR/${pass}_times.txt" | awk '{print $2}')
  ffmpeg -v error -y -f concat -safe 0 -i "$DIR/${pass}_concat.txt" -vf "crop=300:380:490:95,scale=480:-2,fps=30" \
    -c:v libx264 -pix_fmt yuv420p "$DIR/${pass}.mp4"
  echo "$pass: $(wc -l < "$DIR/${pass}_times.txt") frames, first at ${first}s"
done
ffmpeg -v error -y -i "$DIR/flipbook.mp4" -i "$DIR/warp.mp4" -i "$DIR/line.wav" \
  -filter_complex "[0:v]drawtext=text='old: 16-shape flipbook':x=10:y=10:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.5[a];[1:v]drawtext=text='new: warp + visemes':x=10:y=10:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.5[b];[a][b]hstack=inputs=2[v]" \
  -map "[v]" -map 2:a -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$OUT" && echo "wrote $OUT" || exit 1
