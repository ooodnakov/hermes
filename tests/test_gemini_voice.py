"""Offline tests for Gemini and Yandex SpeechKit TTS providers."""
import base64
import json
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError

from plugin import voice


class FakeResponse:
    def __init__(self, body, status=200, headers=None):
        self.body = body
        self.status = status
        self.headers = headers or {}
        self.read_calls = 0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def getcode(self):
        return self.status

    def read(self, count):
        self.read_calls += 1
        result = self.body[:count]
        self.body = self.body[count:]
        return result


def response_for(pcm, mime="audio/pcm;rate=24000"):
    return json.dumps({"candidates": [{"content": {"parts": [
        {"text": "ignored"},
        {"inlineData": {"mimeType": mime,
                         "data": base64.b64encode(pcm).decode("ascii")}},
    ]}}]}).encode()


class GeminiVoiceTests(unittest.TestCase):
    def setUp(self):
        self.old_provider = voice._tts_provider
        self.old_model = voice._gemini_model
        self.old_voice = voice._gemini_voice
        self.old_yandex_voice = voice._yandex_voice
        self.old_yandex_language = voice._yandex_language
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(voice.configure_provider, self.old_provider,
                        self.old_model, self.old_voice)
        self.addCleanup(setattr, voice, "_yandex_voice", self.old_yandex_voice)
        self.addCleanup(setattr, voice, "_yandex_language", self.old_yandex_language)
        self.audio_dir_patch = patch.object(voice, "audio_dir",
                                            return_value=Path(self.temp.name))
        self.audio_dir_patch.start()
        self.addCleanup(self.audio_dir_patch.stop)
        self.env_patch = patch.dict("os.environ", {"G_API_KEY": "unit-test-key"})
        self.env_patch.start()
        self.addCleanup(self.env_patch.stop)
        voice.configure_provider("vertex-gemini")

    def test_vertex_request_schema_and_pcm_wav_output(self):
        pcm = b"\x00\x00\xff\x7f\x00\x80\x34\xf2"
        fake = FakeResponse(response_for(pcm), headers={"Content-Length": "1024"})
        with patch.object(voice.urllib.request, "urlopen", return_value=fake) as open_url:
            output = voice._gemini_render("Hello, привет.")

        self.assertIsNotNone(output)
        self.assertTrue(output.exists())
        request = open_url.call_args.args[0]
        self.assertEqual(request.get_method(), "POST")
        self.assertIn("aiplatform.googleapis.com/v1/publishers/google/models/"
                      "gemini-2.5-flash-tts:generateContent", request.full_url)
        self.assertIn("key=unit-test-key", request.full_url)
        self.assertEqual(open_url.call_args.kwargs["timeout"],
                         voice._GEMINI_READ_TIMEOUT_SECONDS)
        payload = json.loads(request.data)
        self.assertEqual(payload["contents"][0]["parts"][0]["text"], "Hello, привет.")
        config = payload["generationConfig"]
        self.assertEqual(config["responseModalities"], ["AUDIO"])
        self.assertEqual(config["speechConfig"]["voiceConfig"][
            "prebuiltVoiceConfig"]["voiceName"], "Kore")
        with wave.open(str(output), "rb") as wav:
            self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()),
                             (1, 2, 24000))
            self.assertEqual(wav.readframes(wav.getnframes()), pcm)

    def test_rejects_empty_odd_and_invalid_base64_audio(self):
        for body in (response_for(b""), response_for(b"\x00")):
            with self.subTest(body=body), \
                 patch.object(voice.urllib.request, "urlopen", return_value=FakeResponse(body)):
                self.assertIsNone(voice._gemini_render("test"))
        malformed = json.dumps({"candidates": [{"content": {"parts": [
            {"inlineData": {"mimeType": "audio/pcm", "data": "%%%"}}
        ]}}]}).encode()
        with patch.object(voice.urllib.request, "urlopen", return_value=FakeResponse(malformed)):
            self.assertIsNone(voice._gemini_render("test"))
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])

    def test_rejects_unsupported_format_and_oversized_response_header(self):
        for mime in ("audio/wav", "audio/pcm;rate=16000", "audio/l16;codec=alaw",
                     "audio/l16;channels=2"):
            with self.subTest(mime=mime), \
                 patch.object(voice.urllib.request, "urlopen",
                              return_value=FakeResponse(response_for(b"\0\0", mime))):
                self.assertIsNone(voice._gemini_render("test"))
        response = FakeResponse(b"", headers={
            "Content-Length": str(voice._GEMINI_MAX_RESPONSE_BYTES + 1)})
        with patch.object(voice.urllib.request, "urlopen", return_value=response):
            self.assertIsNone(voice._gemini_render("test"))
        self.assertEqual(response.read_calls, 0)

    def test_reads_audio_from_a_later_response_part(self):
        body = json.dumps({"candidates": [{"content": {"parts": [
            {"text": "response metadata"},
            {"inlineData": {"mimeType": "audio/l16;codec=pcm;rate=24000",
                             "data": base64.b64encode(b"\0\0").decode("ascii")}},
        ]}}]}).encode()
        with patch.object(voice.urllib.request, "urlopen", return_value=FakeResponse(body)):
            output = voice._gemini_render("test")
        self.assertIsNotNone(output)

    def test_requires_key_and_bounds_text_and_response_body(self):
        with patch.dict("os.environ", {}, clear=True), \
             patch.object(voice.urllib.request, "urlopen") as open_url:
            self.assertIsNone(voice._gemini_render("test"))
            open_url.assert_not_called()
        with patch.object(voice.urllib.request, "urlopen") as open_url:
            self.assertIsNone(voice._gemini_render("x" * (voice._GEMINI_MAX_TEXT_BYTES + 1)))
            open_url.assert_not_called()
        huge = FakeResponse(b"x" * (voice._GEMINI_MAX_RESPONSE_BYTES + 1))
        with patch.object(voice.urllib.request, "urlopen", return_value=huge):
            self.assertIsNone(voice._gemini_render("test"))
        self.assertEqual(len(huge.body), 0)

    def test_http_errors_never_log_key_or_provider_body(self):
        secret_body = b"private provider diagnostic" * 10
        error = HTTPError("https://example.invalid/?key=unit-test-key", 403,
                          "forbidden", {}, None)
        error.read = lambda *_: secret_body
        with self.assertLogs(voice.logger, level="WARNING") as captured, \
             patch.object(voice.urllib.request, "urlopen", side_effect=error):
            self.assertIsNone(voice._gemini_render("private prompt"))
        joined = "\n".join(captured.output)
        self.assertIn("403", joined)
        self.assertNotIn("unit-test-key", joined)
        self.assertNotIn("private prompt", joined)
        self.assertNotIn(secret_body.decode(), joined)

    def test_hermes_remains_default_and_vertex_name_is_explicit(self):
        self.assertEqual(self.old_provider, "hermes")
        voice.configure_provider("hermes")
        self.assertEqual(voice._tts_provider, "hermes")
        voice.configure_provider("vertex-gemini")
        self.assertEqual(voice._tts_provider, "vertex-gemini")
        voice.configure_provider("gemini")
        self.assertEqual(voice._tts_provider, "vertex-gemini")
        self.assertFalse(voice.configure_provider("vertex-gemini", model="bad/model"))
        self.assertEqual(voice._tts_provider, "hermes")

    def test_say_url_routes_to_selected_provider(self):
        source = Path(self.temp.name) / "source.wav"
        pcm = Path(self.temp.name) / "voice.pcm"
        for provider, method in (("hermes", "_tts_render"),
                                 ("vertex-gemini", "_gemini_render"),
                                 ("yandex", "_yandex_render")):
            voice.configure_provider(provider)
            with patch.object(voice, "start_server", return_value=True), \
                 patch.object(voice, "lan_ip", return_value="192.0.2.1"), \
                 patch.object(voice, "_prune"), \
                 patch.object(voice, method, return_value=source) as render, \
                 patch.object(voice, "_to_pcm16k", return_value=pcm):
                url = voice.say_url("hello")
            self.assertEqual(url, "http://192.0.2.1:8765/voice.pcm")
            render.assert_called_once_with("hello")

    def test_invalid_yandex_voice_configuration_falls_back_to_hermes(self):
        self.assertTrue(voice.configure_provider("yandex-speechkit"))
        self.assertEqual(voice._tts_provider, "yandex")
        self.assertEqual(voice._yandex_voice, "filipp")
        self.assertEqual(voice._yandex_language, "ru-RU")
        self.assertFalse(voice.configure_provider("yandex", voice="bad voice"))
        self.assertEqual(voice._tts_provider, "hermes")


class YandexVoiceTests(unittest.TestCase):
    def setUp(self):
        self.old_provider = voice._tts_provider
        self.old_voice = voice._yandex_voice
        self.old_language = voice._yandex_language
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(setattr, voice, "_tts_provider", self.old_provider)
        self.addCleanup(setattr, voice, "_yandex_voice", self.old_voice)
        self.addCleanup(setattr, voice, "_yandex_language", self.old_language)
        self.audio_dir_patch = patch.object(voice, "audio_dir",
                                            return_value=Path(self.temp.name))
        self.audio_dir_patch.start()
        self.addCleanup(self.audio_dir_patch.stop)
        self.env_patch = patch.dict("os.environ", {"YANDEX_AI_API_KEY": "yandex-test-key"})
        self.env_patch.start()
        self.addCleanup(self.env_patch.stop)
        voice.configure_provider("yandex")

    def test_request_form_auth_and_lpcm_wav_output(self):
        pcm = b"\x00\x00\xff\x7f\x00\x80\x34\xf2"
        fake = FakeResponse(pcm, headers={"Content-Length": str(len(pcm)),
                                          "Content-Type": "audio/lpcm"})
        with patch.object(voice.urllib.request, "urlopen", return_value=fake) as open_url:
            output = voice._yandex_render("Привет, мир!")
        self.assertIsNotNone(output)
        request = open_url.call_args.args[0]
        self.assertEqual(request.get_method(), "POST")
        self.assertEqual(request.full_url, voice._YANDEX_ENDPOINT)
        self.assertEqual(request.get_header("Authorization"), "Api-Key yandex-test-key")
        self.assertNotIn("folderId", request.data.decode("ascii"))
        fields = dict(item.split("=", 1) for item in request.data.decode("ascii").split("&"))
        from urllib.parse import unquote_plus
        fields = {key: unquote_plus(value) for key, value in fields.items()}
        self.assertEqual(fields, {
            "text": "Привет, мир!", "lang": "ru-RU", "voice": "filipp",
            "format": "lpcm", "sampleRateHertz": "16000",
        })
        with wave.open(str(output), "rb") as wav:
            self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()),
                             (1, 2, 16000))
            self.assertEqual(wav.readframes(wav.getnframes()), pcm)

    def test_rejects_empty_odd_wrong_type_and_oversized_response(self):
        for body, headers in ((b"", {}), (b"\x01", {}),
                              (b"data", {"Content-Type": "application/json"})):
            with self.subTest(headers=headers), \
                 patch.object(voice.urllib.request, "urlopen",
                              return_value=FakeResponse(body, headers=headers)):
                self.assertIsNone(voice._yandex_render("test"))
        response = FakeResponse(b"", headers={
            "Content-Length": str(voice._YANDEX_MAX_RESPONSE_BYTES + 1)})
        with patch.object(voice.urllib.request, "urlopen", return_value=response):
            self.assertIsNone(voice._yandex_render("test"))
        self.assertEqual(response.read_calls, 0)
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])

    def test_rejects_non_lpcm_and_wrong_sample_rate_content_types(self):
        for content_type in ("audio/mpeg", "audio/lpcm;rate=24000",
                             "audio/lpcm;codec=alaw", "audio/lpcm;channels=2"):
            with self.subTest(content_type=content_type), \
                 patch.object(voice.urllib.request, "urlopen",
                              return_value=FakeResponse(
                                  b"\0\0", headers={"Content-Type": content_type})):
                self.assertIsNone(voice._yandex_render("test"))

    def test_requires_key_and_http_error_logs_no_key_or_body(self):
        with patch.dict("os.environ", {}, clear=True), \
             patch.object(voice.urllib.request, "urlopen") as open_url:
            self.assertIsNone(voice._yandex_render("test"))
            open_url.assert_not_called()
        body = b"sensitive provider response"
        error = HTTPError(voice._YANDEX_ENDPOINT, 401, "unauthorized", {}, None)
        error.read = lambda *_: body
        with self.assertLogs(voice.logger, level="WARNING") as captured, \
             patch.object(voice.urllib.request, "urlopen", side_effect=error):
            self.assertIsNone(voice._yandex_render("private prompt"))
        logs = "\n".join(captured.output)
        self.assertIn("401", logs)
        self.assertNotIn("yandex-test-key", logs)
        self.assertNotIn(body.decode(), logs)
        self.assertNotIn("private prompt", logs)


if __name__ == "__main__":
    unittest.main()
