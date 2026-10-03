import os
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_agent_tts_smoke_script_rejects_failures_and_accepts_audio(tmp_path):
    if shutil.which("jq") is None:
        pytest.skip("jq is required to exercise the smoke script response pipeline")
    script = tmp_path / "scripts" / "check_agent_tts.sh"
    script.parent.mkdir()
    shutil.copyfile(ROOT / "scripts" / "check_agent_tts.sh", script)
    script.chmod(0o755)
    (tmp_path / ".env").write_text("G_API_KEY=offline-test-key\n")

    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    fake_curl = fake_bin / "curl"
    fake_curl.write_text(
        "#!/bin/sh\n"
        "case \"$FAKE_CURL_CASE\" in\n"
        "  http-error) exit 22 ;;\n"
        "  empty) printf '%s' '{\"candidates\":[{\"content\":{\"parts\":[]}}]}' ;;\n"
        "  odd) printf '%s' '{\"candidates\":[{\"content\":{\"parts\":[{\"inlineData\":{\"data\":\"AA==\"}}]}}]}' ;;\n"
        "  success) printf '%s' '{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"metadata\"},{\"inlineData\":{\"data\":\"AAA=\"}}]}}]}' ;;\n"
        "esac\n"
    )
    fake_curl.chmod(0o755)
    env = os.environ.copy()
    env["PATH"] = f"{fake_bin}{os.pathsep}{env['PATH']}"
    output = tmp_path / "speech.wav"

    for case in ("http-error", "empty", "odd"):
        env["FAKE_CURL_CASE"] = case
        result = subprocess.run([str(script), "test", "Kore", "gemini-2.5-flash-tts",
                                 str(output)], cwd=tmp_path, env=env,
                                capture_output=True, text=True)
        assert result.returncode != 0, (case, result.stdout, result.stderr)
        assert not output.exists()

    env["FAKE_CURL_CASE"] = "success"
    result = subprocess.run([str(script), "test", "Kore", "gemini-2.5-flash-tts",
                             str(output)], cwd=tmp_path, env=env,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert output.read_bytes().startswith(b"RIFF")


def test_agent_tts_smoke_script_rejects_invalid_voice_before_request(tmp_path):
    script = tmp_path / "scripts" / "check_agent_tts.sh"
    script.parent.mkdir()
    shutil.copyfile(ROOT / "scripts" / "check_agent_tts.sh", script)
    script.chmod(0o755)
    (tmp_path / ".env").write_text("G_API_KEY=offline-test-key\n")
    result = subprocess.run([str(script), "test", 'Kore\"}', "gemini-2.5-flash-tts"],
                            cwd=tmp_path, capture_output=True, text=True)
    assert result.returncode != 0
    assert "VOICE must contain" in result.stderr
