"""voice — give the familiar a real voice.

Pipeline: text → Hermes' own TTS (tools.tts_tool, provider/voice from the
user's ``tts:`` config) → FFmpeg (Linux) or afconvert (macOS) to 16 kHz mono s16le →
raw PCM file → served over a tiny LAN HTTP server → the device streams it
straight into its I2S speaker (no transcoding on-device; the I2S bus runs at
exactly this format).

USB serial stays the control channel; Wi-Fi only carries audio. Everything
degrades quietly: no TTS provider / no converter / port taken / device not on
Wi-Fi → banners and chirps still work, speech is skipped.
"""
from __future__ import annotations

import array
import base64
import binascii
import functools
import http.server
import json
import logging
import os
import re
import socket
import subprocess
import sys
import threading
import time
import shutil
import urllib.error
import urllib.parse
import urllib.request
import wave
from pathlib import Path

logger = logging.getLogger("familiar.voice")

_PORT = 8765
_MAX_FILES = 40
_volume = 1.0
_tts_provider = "hermes"
_gemini_model = "gemini-2.5-flash-tts"
_gemini_voice = "Kore"
_yandex_voice = "filipp"
_yandex_language = "ru-RU"
_GEMINI_ENDPOINT = "https://aiplatform.googleapis.com/v1/publishers/google/models"
_GEMINI_TIMEOUT_SECONDS = 20
_GEMINI_READ_TIMEOUT_SECONDS = 5
_GEMINI_MAX_TEXT_BYTES = 4096
_GEMINI_MAX_RESPONSE_BYTES = 4 * 1024 * 1024
_GEMINI_MODEL_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
_GEMINI_VOICE_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_YANDEX_VOICE_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_YANDEX_LANG_RE = re.compile(r"^[A-Za-z0-9-]{2,16}$")
_YANDEX_ENDPOINT = "https://tts.api.cloud.yandex.net/speech/v1/tts:synthesize"
_YANDEX_TIMEOUT_SECONDS = 20
_YANDEX_READ_TIMEOUT_SECONDS = 5
_YANDEX_MAX_TEXT_BYTES = 4096
_YANDEX_MAX_RESPONSE_BYTES = 1_920_000


def set_volume(v) -> None:
    """0.0..1.0 host-side gain applied to every rendered clip."""
    global _volume
    try:
        _volume = min(1.0, max(0.0, float(v)))
    except (TypeError, ValueError):
        _volume = 1.0


def configure_provider(provider: str = "hermes", model: str | None = None,
                       voice: str | None = None, language: str | None = None) -> bool:
    """Select the configured renderer; Hermes remains the default provider."""
    global _tts_provider, _gemini_model, _gemini_voice, _yandex_voice, _yandex_language
    selected = str(provider or "hermes").strip().lower()
    if selected == "gemini":
        selected = "vertex-gemini"
    if selected == "yandex-speechkit":
        selected = "yandex"
    if selected not in ("hermes", "vertex-gemini", "yandex"):
        logger.warning("unsupported familiar TTS provider; using Hermes renderer")
        selected = "hermes"
    if selected == "hermes":
        _tts_provider = "hermes"
        return True
    if selected == "yandex":
        next_voice = str(voice or "filipp").strip()
        next_language = str(language or "ru-RU").strip()
        if not _YANDEX_VOICE_RE.fullmatch(next_voice) or not _YANDEX_LANG_RE.fullmatch(next_language):
            logger.warning("invalid Yandex SpeechKit voice/language configuration")
            _tts_provider = "hermes"
            return False
        _yandex_voice = next_voice
        _yandex_language = next_language
        _tts_provider = "yandex"
        return True
    next_model = str(model or "gemini-2.5-flash-tts").strip()
    next_voice = str(voice or "Kore").strip()
    if not _GEMINI_MODEL_RE.fullmatch(next_model) or not _GEMINI_VOICE_RE.fullmatch(next_voice):
        logger.warning("invalid Gemini TTS model/voice configuration")
        _tts_provider = "hermes"
        return False
    _tts_provider = selected
    _gemini_model = next_model
    _gemini_voice = next_voice
    return True


def _pcm_content_type_matches(value: str, media_types: tuple[str, ...],
                              sample_rate: int) -> bool:
    """Check an advertised PCM type against the format we wrap downstream."""
    pieces = [piece.strip() for piece in value.split(";")]
    if pieces[0].lower() not in media_types:
        return False
    params: dict[str, str] = {}
    for piece in pieces[1:]:
        key, separator, item = piece.partition("=")
        if not separator or key.strip().lower() in params:
            return False
        params[key.strip().lower()] = item.strip().strip('"').lower()
    if "rate" in params and params["rate"] != str(sample_rate):
        return False
    if "channels" in params and params["channels"] != "1":
        return False
    if "codec" in params and params["codec"] != "pcm":
        return False
    if "bit" in params and params["bit"] != "16":
        return False
    return True


_serve_dir: Path | None = None
_server_ok = False


def audio_dir() -> Path:
    home = Path(os.environ.get("HERMES_HOME", Path.home() / ".hermes"))
    d = home / "familiar_audio"
    d.mkdir(parents=True, exist_ok=True)
    return d


def lan_ip() -> str | None:
    """Return an explicitly advertised host, or detect the default LAN address.

    Set HERMES_ADVERTISED_HOST when the host is behind WSL/NAT and the address
    selected by the UDP route probe is not reachable from the ESP32.
    """
    configured = os.environ.get("HERMES_ADVERTISED_HOST", "").strip()
    if configured:
        return configured
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return None


def start_server(port: int = _PORT) -> bool:
    """Serve the audio dir on the LAN. Idempotent; False if the port is taken."""
    global _serve_dir, _server_ok
    if _server_ok:
        return True
    _serve_dir = audio_dir()
    class _Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt, *args):  # noqa: N802
            logger.info("audio %s %s", self.client_address[0], fmt % args)

    handler = functools.partial(_Handler, directory=str(_serve_dir))
    try:
        srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), handler)
    except OSError as e:
        logger.warning("familiar audio server port %d unavailable (%s) — voice off", port, e)
        return False
    threading.Thread(target=srv.serve_forever, name="familiar-audio", daemon=True).start()
    _server_ok = True
    logger.info("familiar audio server on 0.0.0.0:%d serving %s", port, _serve_dir)
    return True


def _prune() -> None:
    files = sorted(audio_dir().glob("*.pcm"), key=lambda p: p.stat().st_mtime)
    for p in files[:-_MAX_FILES]:
        try:
            p.unlink()
        except OSError:
            pass


def _tts_render(text: str) -> Path | None:
    """Hermes' own TTS -> audio file (mp3/ogg/wav per provider config)."""
    try:
        from tools.tts_tool import text_to_speech_tool
    except ImportError:
        logger.warning("hermes tts_tool unavailable — voice off")
        return None
    out = audio_dir() / f"tts_{int(time.time() * 1000)}.mp3"
    try:
        res = json.loads(text_to_speech_tool(text=text, output_path=str(out)))
    except Exception:
        logger.exception("familiar tts render failed")
        return None
    path = res.get("file_path") if isinstance(res, dict) else None
    if not path or not Path(path).exists():
        logger.warning("familiar tts produced no file: %s", res)
        return None
    return Path(path)


def _gemini_render(text: str) -> Path | None:
    """Vertex Express Mode Gemini TTS -> verified 24 kHz mono PCM WAV.

    The API key is read only from ``G_API_KEY``. Neither request URLs, response
    bodies, prompts, nor exception messages are written to logs.
    """
    api_key = os.environ.get("G_API_KEY", "").strip()
    if not api_key:
        logger.warning("Gemini TTS selected but G_API_KEY is not configured")
        return None
    if len(api_key) > 512:
        logger.warning("G_API_KEY exceeds the supported configuration limit")
        return None
    try:
        text_bytes = text.encode("utf-8")
    except UnicodeError:
        logger.warning("Gemini TTS input is not valid UTF-8")
        return None
    if not text_bytes or len(text_bytes) > _GEMINI_MAX_TEXT_BYTES:
        logger.warning("Gemini TTS input is empty or exceeds the request limit")
        return None
    if not _GEMINI_MODEL_RE.fullmatch(_gemini_model) or not _GEMINI_VOICE_RE.fullmatch(_gemini_voice):
        logger.warning("Gemini TTS model/voice configuration is invalid")
        return None

    endpoint = (f"{_GEMINI_ENDPOINT}/{_gemini_model}:generateContent?" +
                urllib.parse.urlencode({"key": api_key}))
    payload = {
        "contents": [{"role": "user", "parts": [{"text": text}]}],
        "generationConfig": {
            "responseModalities": ["AUDIO"],
            "speechConfig": {
                "voiceConfig": {
                    "prebuiltVoiceConfig": {"voiceName": _gemini_voice}
                }
            },
        },
    }
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(body) > _GEMINI_MAX_TEXT_BYTES + 1024:
        logger.warning("Gemini TTS request exceeds the request limit")
        return None

    request = urllib.request.Request(
        endpoint, data=body,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST")
    started = time.monotonic()
    response_body = bytearray()
    try:
        with urllib.request.urlopen(request, timeout=_GEMINI_READ_TIMEOUT_SECONDS) as response:
            status = response.getcode()
            if status != 200:
                logger.warning("Gemini TTS returned HTTP %s", status)
                return None
            length = response.headers.get("Content-Length")
            if length:
                try:
                    if int(length) > _GEMINI_MAX_RESPONSE_BYTES:
                        logger.warning("Gemini TTS response exceeds the audio limit")
                        return None
                except ValueError:
                    logger.warning("Gemini TTS returned an invalid response length")
                    return None
            while len(response_body) <= _GEMINI_MAX_RESPONSE_BYTES:
                if time.monotonic() - started >= _GEMINI_TIMEOUT_SECONDS:
                    logger.warning("Gemini TTS response timed out")
                    return None
                remaining = _GEMINI_MAX_RESPONSE_BYTES + 1 - len(response_body)
                chunk = response.read(min(65536, remaining))
                if not chunk:
                    break
                response_body.extend(chunk)
    except urllib.error.HTTPError as exc:
        logger.warning("Gemini TTS returned HTTP %s", exc.code)
        return None
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        logger.warning("Gemini TTS request failed (%s)", type(exc).__name__)
        return None
    if not response_body or len(response_body) > _GEMINI_MAX_RESPONSE_BYTES:
        logger.warning("Gemini TTS response is empty or exceeds the audio limit")
        return None
    try:
        result = json.loads(response_body)
        audio_data = None
        mime_type = ""
        for candidate in result.get("candidates", []):
            for part in candidate.get("content", {}).get("parts", []):
                inline = part.get("inlineData") or part.get("inline_data")
                if isinstance(inline, dict) and inline.get("data"):
                    audio_data = inline["data"]
                    mime_type = str(inline.get("mimeType") or inline.get("mime_type") or "")
                    break
            if audio_data:
                break
        if not isinstance(audio_data, str) or not audio_data:
            logger.warning("Gemini TTS response contains no audio data")
            return None
        if mime_type and not _pcm_content_type_matches(
                mime_type, ("audio/pcm", "audio/l16"), 24000):
            logger.warning("Gemini TTS returned an unsupported audio format")
            return None
        pcm = base64.b64decode(audio_data, validate=True)
        if not pcm or len(pcm) % 2 or len(pcm) > _GEMINI_MAX_RESPONSE_BYTES:
            logger.warning("Gemini TTS returned empty or malformed PCM")
            return None
    except (ValueError, TypeError, KeyError, AttributeError, RecursionError,
            json.JSONDecodeError, binascii.Error):
        logger.warning("Gemini TTS response could not be decoded")
        return None

    out = audio_dir() / f"gemini_{time.time_ns()}.wav"
    try:
        with wave.open(str(out), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(24000)
            wav.writeframes(pcm)
        return out
    except (OSError, wave.Error):
        out.unlink(missing_ok=True)
        logger.warning("Gemini TTS audio could not be stored")
        return None


def _yandex_render(text: str) -> Path | None:
    """Yandex SpeechKit API v1 -> 16 kHz mono LPCM WAV.

    SpeechKit returns headerless binary audio. The service-account API key is
    read only from ``YANDEX_AI_API_KEY``; request headers and response content
    are never included in logs.
    """
    api_key = os.environ.get("YANDEX_AI_API_KEY", "").strip()
    if not api_key:
        logger.warning("Yandex SpeechKit selected but YANDEX_AI_API_KEY is not configured")
        return None
    if len(api_key) > 512:
        logger.warning("YANDEX_AI_API_KEY exceeds the supported configuration limit")
        return None
    try:
        text_bytes = text.encode("utf-8")
    except UnicodeError:
        logger.warning("Yandex SpeechKit input is not valid UTF-8")
        return None
    if not text_bytes or len(text_bytes) > _YANDEX_MAX_TEXT_BYTES:
        logger.warning("Yandex SpeechKit input is empty or exceeds the request limit")
        return None
    if not _YANDEX_VOICE_RE.fullmatch(_yandex_voice) or not _YANDEX_LANG_RE.fullmatch(_yandex_language):
        logger.warning("Yandex SpeechKit voice/language configuration is invalid")
        return None

    fields = {
        "text": text,
        "lang": _yandex_language,
        "voice": _yandex_voice,
        "format": "lpcm",
        "sampleRateHertz": "16000",
    }
    body = urllib.parse.urlencode(fields).encode("ascii")
    # SpeechKit v1 caps the encoded request at 15 KiB.
    if len(body) > 15 * 1024:
        logger.warning("Yandex SpeechKit request exceeds the encoded body limit")
        return None
    request = urllib.request.Request(
        _YANDEX_ENDPOINT, data=body,
        headers={
            "Authorization": f"Api-Key {api_key}",
            "Content-Type": "application/x-www-form-urlencoded",
            "Accept": "audio/lpcm, application/octet-stream",
        },
        method="POST")
    started = time.monotonic()
    response_body = bytearray()
    try:
        with urllib.request.urlopen(request, timeout=_YANDEX_READ_TIMEOUT_SECONDS) as response:
            status = response.getcode()
            if status != 200:
                logger.warning("Yandex SpeechKit returned HTTP %s", status)
                return None
            content_type = str(response.headers.get("Content-Type") or "").lower()
            if content_type and not _pcm_content_type_matches(
                    content_type, ("audio/lpcm", "audio/pcm", "audio/l16", "audio/x-pcm",
                                   "application/octet-stream"), 16000):
                logger.warning("Yandex SpeechKit returned an unsupported content type")
                return None
            length = response.headers.get("Content-Length")
            if length:
                try:
                    if int(length) > _YANDEX_MAX_RESPONSE_BYTES:
                        logger.warning("Yandex SpeechKit audio exceeds the response limit")
                        return None
                except ValueError:
                    logger.warning("Yandex SpeechKit returned an invalid response length")
                    return None
            while len(response_body) <= _YANDEX_MAX_RESPONSE_BYTES:
                if time.monotonic() - started >= _YANDEX_TIMEOUT_SECONDS:
                    logger.warning("Yandex SpeechKit response timed out")
                    return None
                remaining = _YANDEX_MAX_RESPONSE_BYTES + 1 - len(response_body)
                chunk = response.read(min(65536, remaining))
                if not chunk:
                    break
                response_body.extend(chunk)
    except urllib.error.HTTPError as exc:
        logger.warning("Yandex SpeechKit returned HTTP %s", exc.code)
        return None
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        logger.warning("Yandex SpeechKit request failed (%s)", type(exc).__name__)
        return None
    if not response_body or len(response_body) > _YANDEX_MAX_RESPONSE_BYTES or len(response_body) % 2:
        logger.warning("Yandex SpeechKit returned empty or malformed LPCM")
        return None

    out = audio_dir() / f"yandex_{time.time_ns()}.wav"
    try:
        with wave.open(str(out), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(16000)
            wav.writeframes(response_body)
        return out
    except (OSError, wave.Error):
        out.unlink(missing_ok=True)
        logger.warning("Yandex SpeechKit audio could not be stored")
        return None


def _to_pcm16k(src: Path) -> Path | None:
    """Convert audio to raw signed 16-bit little-endian mono 16 kHz PCM."""
    converter = "ffmpeg" if sys.platform != "darwin" else "afconvert"
    executable = shutil.which(converter)
    try:
        if not executable:
            logger.warning("%s not found — install %s to enable familiar speech",
                           converter, "FFmpeg" if converter == "ffmpeg" else "macOS audio tools")
            return None
        if converter == "ffmpeg":
            result = subprocess.run(
                [executable, "-v", "error", "-i", str(src), "-f", "s16le",
                 "-acodec", "pcm_s16le", "-ar", "16000", "-ac", "1", "pipe:1"],
                check=True, capture_output=True, timeout=30)
            frames = result.stdout
        else:
            wav = src.with_suffix(".16k.wav")
            subprocess.run(
                [executable, str(src), "-d", "LEI16@16000", "-c", "1",
                 "-f", "WAVE", str(wav)],
                check=True, capture_output=True, timeout=30)
            import wave
            with wave.open(str(wav), "rb") as w:
                if w.getframerate() != 16000 or w.getnchannels() != 1 or w.getsampwidth() != 2:
                    raise ValueError("afconvert output did not match 16 kHz mono s16le")
                frames = w.readframes(w.getnframes())
            wav.unlink(missing_ok=True)
        if not frames or len(frames) % 2:
            raise ValueError("converter returned empty or malformed 16-bit PCM")
        if _volume < 0.999:
            samples = array.array("h")
            samples.frombytes(frames)
            for i in range(len(samples)):
                samples[i] = int(samples[i] * _volume)
            frames = samples.tobytes()
        pcm = src.with_suffix(".pcm")
        pcm.write_bytes(frames)
        return pcm
    except Exception:
        logger.exception("familiar pcm convert failed")
        return None
    finally:
        src.unlink(missing_ok=True)
        if sys.platform == "darwin":
            src.with_suffix(".16k.wav").unlink(missing_ok=True)


def say_url(text: str, port: int = _PORT) -> str | None:
    """Full pipeline: text -> LAN URL of a device-ready PCM clip, or None."""
    text = " ".join(str(text or "").split())
    if not text:
        return None
    if not start_server(port):
        return None
    ip = lan_ip()
    if not ip:
        return None
    clip_text = text[:400]
    if _tts_provider == "vertex-gemini":
        audio = _gemini_render(clip_text)
    elif _tts_provider == "yandex":
        audio = _yandex_render(clip_text)
    else:
        audio = _tts_render(clip_text)
    if audio is None:
        return None
    pcm = _to_pcm16k(audio)
    if pcm is None:
        return None
    _prune()
    return f"http://{ip}:{port}/{pcm.name}"
