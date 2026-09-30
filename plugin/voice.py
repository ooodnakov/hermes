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
import functools
import http.server
import json
import logging
import os
import socket
import subprocess
import sys
import threading
import time
import shutil
from pathlib import Path

logger = logging.getLogger("familiar.voice")

_PORT = 8765
_MAX_FILES = 40
_volume = 1.0


def set_volume(v) -> None:
    """0.0..1.0 host-side gain applied to every rendered clip."""
    global _volume
    try:
        _volume = min(1.0, max(0.0, float(v)))
    except (TypeError, ValueError):
        _volume = 1.0
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
    audio = _tts_render(text[:400])
    if audio is None:
        return None
    pcm = _to_pcm16k(audio)
    if pcm is None:
        return None
    _prune()
    return f"http://{ip}:{port}/{pcm.name}"
