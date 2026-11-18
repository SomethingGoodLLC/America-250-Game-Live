#!/usr/bin/bash
# Regenerate every clip of clip_specs.yaml, one clip per process, in chain order, skipping clips already done.
# Resumable: the animation server restarts without warning. Status in <AnimationPass>/logs/rebuild.status;
# the manifest is copied to tools/anim/manifest.json after every clip so it can be committed.
A="C:/Users/user/Documents/ChatGPT/Play America 250/AnimationPass"
T="$(cd "$(dirname "$0")" && pwd)"
PY="$A/kimodo/.venv/Scripts/python.exe"; export PYTHONIOENCODING=utf-8
cd "$T" || exit 1
DONE="$A/logs/rebuild.done"; touch "$DONE"
ORDER=$("$PY" -c "
import yaml, gen_clips
s = yaml.safe_load(open('clip_specs.yaml', encoding='utf-8'))
print(' '.join(x['name'] for x in gen_clips.order_for_chains(s, [x['name'] for x in s])))" 2>/dev/null)
[ -z "$ORDER" ] && { echo "NO ORDER $(date)" >> "$A/logs/rebuild.status"; exit 1; }
echo "START $(date)" >> "$A/logs/rebuild.status"
for clip in $ORDER; do
  grep -qx "$clip" "$DONE" && continue
  curl -s -o /dev/null -m 3 http://127.0.0.1:9550/ || { echo "ENCODER DOWN at $clip $(date +%H:%M)" >> "$A/logs/rebuild.status"; exit 2; }
  "$PY" gen_clips.py --retries 10 --only "$clip" > "$A/logs/rebuild_$clip.log" 2>&1
  rc=$?
  echo "$clip exit $rc $(date +%H:%M)" >> "$A/logs/rebuild.status"
  [ $rc -eq 0 ] && { echo "$clip" >> "$DONE"; cp "$A/manifest.json" "$T/manifest.json"; }
done
echo "REBUILD DONE $(date)" >> "$A/logs/rebuild.status"
