#!/usr/bin/env bash
# Smoke-test Google Agent Platform (Vertex AI) Gemini TTS with an API key.
# Usage: scripts/check_agent_tts.sh ["text"] [voice] [model] [out.wav]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [[ -f ./.env ]]; then
  set -a; . ./.env; set +a
fi

TEXT="${1:-Say cheerfully: Hello from Espherm!}"
VOICE="${2:-Kore}"
MODEL="${3:-gemini-2.5-flash-tts}"
OUT="${4:-tts_check.wav}"

if [[ -z "${G_API_KEY:-}" ]]; then
  echo "G_API_KEY is not set in .env" >&2
  exit 1
fi
if [[ ! "$VOICE" =~ ^[A-Za-z0-9_-]{1,64}$ ]]; then
  echo "VOICE must contain only letters, digits, underscores, or hyphens" >&2
  exit 1
fi
if [[ ! "$MODEL" =~ ^[A-Za-z0-9._-]{1,100}$ ]]; then
  echo "MODEL must contain only letters, digits, dots, underscores, or hyphens" >&2
  exit 1
fi

TEXT_JSON="$(printf '%s' "$TEXT" | python3 -c 'import json,sys;print(json.dumps(sys.stdin.read()))')"
RAW="$(mktemp)"
trap 'rm -f "$RAW"' EXIT

curl -fsS \
  "https://aiplatform.googleapis.com/v1/publishers/google/models/${MODEL}:generateContent?key=${G_API_KEY}" \
  -H 'Content-Type: application/json' -X POST \
  -d "{
    \"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":${TEXT_JSON}}]}],
    \"generationConfig\":{\"responseModalities\":[\"AUDIO\"],
      \"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"${VOICE}\"}}}}
  }" \
  | jq -er '[.candidates[]?.content.parts[]?.inlineData.data? | select(type == "string" and length > 0)] | first | select(type == "string")' \
  | base64 -d > "$RAW"

# Gemini TTS returns headerless PCM: 24 kHz, mono, s16le. Wrap in a WAV header.
python3 - "$RAW" "$OUT" <<'PY'
import sys, wave
from pathlib import Path
src, dst = Path(sys.argv[1]), sys.argv[2]
pcm = open(src, "rb").read()
if not pcm or len(pcm) % 2:
    raise SystemExit("Gemini TTS returned empty or malformed PCM")
with wave.open(dst, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(24000)
    w.writeframes(pcm)
print(f"wrote {dst}: {len(pcm)} PCM bytes, {len(pcm)/2/24000:.2f}s of speech")
PY
rm -f "$RAW"
