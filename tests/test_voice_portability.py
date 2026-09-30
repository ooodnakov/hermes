import array
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from plugin import voice


class VoicePortabilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "speech.mp3"
        self.source.write_bytes(b"encoded")
        self.old_volume = voice._volume
        self.addCleanup(setattr, voice, "_volume", self.old_volume)
        self.old_platform = voice.sys.platform
        self.addCleanup(setattr, voice.sys, "platform", self.old_platform)

    def test_linux_ffmpeg_emits_device_pcm_and_applies_volume(self):
        voice.sys.platform = "linux"
        voice.set_volume(0.5)
        samples = array.array("h", [1200, -400]).tobytes()
        completed = type("Completed", (), {"stdout": samples})()
        with patch.object(voice.shutil, "which", return_value="/usr/bin/ffmpeg"), \
             patch.object(voice.subprocess, "run", return_value=completed) as run:
            result = voice._to_pcm16k(self.source)
        self.assertEqual(result.read_bytes(), array.array("h", [600, -200]).tobytes())
        self.assertFalse(self.source.exists())
        args = run.call_args.args[0]
        self.assertEqual(args[args.index("-f") + 1], "s16le")
        self.assertEqual(args[args.index("-ar") + 1], "16000")
        self.assertEqual(args[args.index("-ac") + 1], "1")
        self.assertIn("pcm_s16le", args)

    def test_missing_linux_converter_is_graceful_and_cleans_source(self):
        voice.sys.platform = "linux"
        with patch.object(voice.shutil, "which", return_value=None):
            self.assertIsNone(voice._to_pcm16k(self.source))
        self.assertFalse(self.source.exists())

    def test_advertised_host_overrides_wsl_route_detection(self):
        with patch.dict(os.environ, {"HERMES_ADVERTISED_HOST": "192.168.1.20"}), \
             patch.object(voice.socket, "socket", side_effect=AssertionError("should not probe")):
            self.assertEqual(voice.lan_ip(), "192.168.1.20")


if __name__ == "__main__":
    unittest.main()
