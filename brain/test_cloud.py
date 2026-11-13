"""The optional cloud models (Claude, ChatGPT), checked without the internet, a key or their libraries."""
import http.server
import json
import sys
import threading
import types
import unittest
import urllib.request

from brain import providers
from brain.providers import claude, openai

KEY = "test-key-not-real-0123456789abcdef"
HISTORY = [{"role": "user", "content": "q1"}, {"role": "assistant", "content": "a1"},
           {"role": "user", "content": "q2"}]


def reply(text="I am fine, Dave.", stop_reason="end_turn", category=None):
    """Shaped like the library's Message: a thinking block (empty, as it arrives), then the text."""
    blocks = [types.SimpleNamespace(type="thinking", thinking="")]
    if text is not None:
        blocks.append(types.SimpleNamespace(type="text", text=text))
    details = types.SimpleNamespace(type="refusal", category=category) if stop_reason == "refusal" else None
    return types.SimpleNamespace(stop_reason=stop_reason, content=blocks, stop_details=details)


class FakeSdk:
    """Shaped like the `anthropic` package: the client and its family of errors."""

    class AnthropicError(Exception):
        pass

    class APIError(AnthropicError):
        def __init__(self, message="it went wrong"):
            super().__init__(message)
            self.message = message

    class APIStatusError(APIError):
        status_code = 500

    class BadRequestError(APIStatusError):
        status_code = 400

    class AuthenticationError(APIStatusError):
        status_code = 401

    class PermissionDeniedError(APIStatusError):
        status_code = 403

    class NotFoundError(APIStatusError):
        status_code = 404

    class RateLimitError(APIStatusError):
        status_code = 429

    class APIConnectionError(APIError):
        pass

    class APITimeoutError(APIConnectionError):
        pass

    class CredentialsError(AnthropicError):
        pass

    @staticmethod
    def Timeout(default, **each):
        return ("timeout", default, each)

    def __init__(self, answer=None):
        self.answer = answer if answer is not None else reply()  # a Message, or an exception to raise
        self.made = []       # how each client was built
        self.plain = []      # calls to client.messages.create
        self.beta = []       # calls to client.beta.messages.create
        self.looked_up = []  # calls to client.models.retrieve

    def Anthropic(self, **kwargs):
        self.made.append(kwargs)

        def create(calls):
            def call(**request):
                calls.append(request)
                if isinstance(self.answer, Exception):
                    raise self.answer
                return self.answer
            return call

        def retrieve(model):
            self.looked_up.append(model)
            if isinstance(self.answer, Exception):
                raise self.answer
            return types.SimpleNamespace(id=model)

        return types.SimpleNamespace(
            messages=types.SimpleNamespace(create=create(self.plain)),
            beta=types.SimpleNamespace(messages=types.SimpleNamespace(create=create(self.beta))),
            models=types.SimpleNamespace(retrieve=retrieve))


class ClaudeTest(unittest.TestCase):
    def test_request(self):
        request = claude.build_request("claude-opus-5-5", "You are HAL.", HISTORY)
        self.assertEqual(request["model"], "claude-opus-5-5")
        self.assertEqual(request["system"], "You are HAL.")
        self.assertEqual(request["messages"], HISTORY)
        self.assertEqual(request["output_config"], {"effort": "low"})  # a spoken line: think briefly
        self.assertGreaterEqual(request["max_tokens"], 2000)  # thinking counts toward it, not just the line
        for gone in ("thinking", "temperature", "top_p", "top_k"):  # this model refuses them
            self.assertNotIn(gone, request)

    def test_a_declined_question_may_be_answered_by_another_claude_model(self):
        for model in ("claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5", "claude-fable-5-2"):
            request = claude.build_request(model, "s", HISTORY)
            self.assertEqual(request["fallbacks"], "default", model)
            self.assertEqual(request["betas"], ["server-side-fallback-2026-07-01"], model)
        for model in ("claude-haiku-5-5", "claude-opus-4-8"):  # no such arrangement for these
            request = claude.build_request(model, "s", HISTORY)
            self.assertNotIn("fallbacks", request, model)
            self.assertNotIn("betas", request, model)

    def test_chat(self):
        sdk = FakeSdk(reply("  I am fine, Dave.\n"))
        provider = claude.ClaudeProvider(KEY, sdk=sdk)
        self.assertEqual((provider.name, provider.model), ("claude", "claude-opus-5-5"))
        self.assertEqual(provider.chat("You are HAL.", HISTORY), "I am fine, Dave.")
        self.assertEqual(sdk.beta, [claude.build_request("claude-opus-5-5", "You are HAL.", HISTORY)])
        self.assertEqual(sdk.plain, [])

    def test_chat_without_the_fallback_arrangement(self):
        sdk = FakeSdk()
        claude.ClaudeProvider(KEY, model="claude-haiku-5-5", sdk=sdk).chat("s", HISTORY)
        self.assertEqual(sdk.plain, [claude.build_request("claude-haiku-5-5", "s", HISTORY)])
        self.assertEqual(sdk.beta, [])

    def test_one_try_within_hal_zeros_patience(self):
        sdk = FakeSdk()
        claude.ClaudeProvider(KEY, sdk=sdk)
        self.assertEqual(sdk.made[0]["max_retries"], 0)
        self.assertEqual(sdk.made[0]["timeout"], ("timeout", 20.0, {"connect": 5.0}))  # 25 s at the very most

    def test_the_key_goes_to_anthropic_whatever_this_computer_is_set_up_for(self):
        # Without an address the library takes ANTHROPIC_BASE_URL from the shell the brain was started
        # in: a gateway set up for some other program would get the key, the questions and the readings.
        sdk = FakeSdk()
        claude.ClaudeProvider(KEY, sdk=sdk)
        self.assertEqual(sdk.made[0]["base_url"], "https://api.anthropic.com")
        self.assertEqual(sdk.made[0]["api_key"], KEY)

    def test_reply_text(self):
        two = reply("One.")
        two.content.append(types.SimpleNamespace(type="text", text=" Two."))
        self.assertEqual(claude.reply_text(two), "One. Two.")
        self.assertEqual(claude.reply_text(reply(None)), "")  # only thinking came back
        self.assertEqual(claude.reply_text(reply("Cut sho", stop_reason="max_tokens")), "Cut sho")

    def test_a_refusal_is_an_empty_reply(self):
        # Declined before any text, or part-way through one: either way there is no line to speak.
        self.assertEqual(claude.reply_text(reply(None, stop_reason="refusal", category="cyber")), "")
        self.assertEqual(claude.reply_text(reply("I can tell you th", stop_reason="refusal")), "")

    def test_errors_are_provider_errors_that_never_show_the_key(self):
        sdk = FakeSdk()
        for error, says in ((sdk.AuthenticationError(f"invalid x-api-key {KEY}"), "brain/.env"),
                            (sdk.PermissionDeniedError(), "claude-opus-5-5"),
                            (sdk.NotFoundError(), "claude-opus-5-5"),
                            (sdk.RateLimitError(), "429"),
                            (sdk.BadRequestError("max_tokens: too small"), "max_tokens: too small"),
                            (sdk.APIStatusError("overloaded"), "500"),
                            (sdk.APITimeoutError(), "APITimeoutError"),
                            (sdk.APIConnectionError(), "APIConnectionError"),
                            (sdk.CredentialsError("no credentials"), "CredentialsError")):
            sdk.answer = error
            with self.assertRaises(providers.ProviderError, msg=repr(error)) as caught:
                claude.ClaudeProvider(KEY, sdk=sdk).chat("s", HISTORY)
            self.assertIn(says, str(caught.exception))
            self.assertNotIn(KEY, str(caught.exception))
            self.assertLessEqual(len(str(caught.exception)), 200)

    def test_warm_checks_the_key_and_the_model_without_asking_anything(self):
        sdk = FakeSdk()
        self.assertIs(claude.ClaudeProvider(KEY, model="claude-sonnet-5-5", sdk=sdk).warm(), True)
        self.assertEqual(sdk.looked_up, ["claude-sonnet-5-5"])
        self.assertEqual(sdk.plain + sdk.beta, [])  # no line was written, so none was paid for
        sdk.answer = sdk.AuthenticationError()
        self.assertIs(claude.ClaudeProvider(KEY, sdk=sdk).warm(), False)
        sdk.answer = RuntimeError("anything")
        self.assertIs(claude.ClaudeProvider(KEY, sdk=sdk).warm(), False)

    def test_missing_library_says_how_to_get_it(self):
        had = sys.modules.get("anthropic", ...)
        sys.modules["anthropic"] = None  # makes `import anthropic` fail, installed or not
        try:
            with self.assertRaises(ValueError) as caught:
                claude.ClaudeProvider(KEY)
            self.assertIn("pip install anthropic", str(caught.exception))
        finally:
            if had is ...:
                del sys.modules["anthropic"]
            else:
                sys.modules["anthropic"] = had


class FakeOpenAI:
    """A stand-in for api.openai.com on this computer: answers from a queue and keeps what it was sent."""

    def __init__(self):
        self.seen = []     # (method, path, headers, body or None)
        self.answers = []  # (status, bytes), oldest first
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def _answer(self):
                length = int(self.headers.get("Content-Length") or 0)
                body = json.loads(self.rfile.read(length)) if length else None
                outer.seen.append((self.command, self.path, dict(self.headers), body))
                status, payload = outer.answers.pop(0)
                self.send_response(status)
                if status in (301, 302, 307):
                    self.send_header("Location", payload.decode())
                    payload = b""
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

            do_GET = do_POST = _answer

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/v1"

    def will_say(self, payload, status=200):
        self.answers.append((status, payload if isinstance(payload, bytes) else json.dumps(payload).encode()))

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def said(content, refusal=None):
    return {"choices": [{"index": 0, "finish_reason": "stop",
                         "message": {"role": "assistant", "content": content, "refusal": refusal}}]}


class OpenAITest(unittest.TestCase):
    def setUp(self):
        self.api = FakeOpenAI()
        self.addCleanup(self.api.close)
        # Straight to the stand-in: a proxy set on this computer must not get in the way of the test.
        direct = urllib.request.build_opener(urllib.request.ProxyHandler({}), openai.NoRedirects())
        self.provider = openai.OpenAIProvider(KEY, "some-model", base_url=self.api.url, timeout_s=3, opener=direct)

    def test_request(self):
        body = openai.build_request("some-model", "You are HAL.", HISTORY)
        self.assertEqual(body["model"], "some-model")
        self.assertEqual(body["messages"][0], {"role": "developer", "content": "You are HAL."})
        self.assertEqual(body["messages"][1:], HISTORY)
        self.assertGreaterEqual(body["max_completion_tokens"], 2000)  # a model's reasoning counts toward it
        for gone in ("max_tokens", "temperature", "reasoning_effort"):  # not every model takes these
            self.assertNotIn(gone, body)

    def test_chat(self):
        self.api.will_say(said(" I am fine, Dave.\n"))
        self.assertEqual((self.provider.name, self.provider.model), ("openai", "some-model"))
        self.assertEqual(self.provider.chat("You are HAL.", HISTORY), "I am fine, Dave.")
        method, path, headers, body = self.api.seen[0]
        self.assertEqual((method, path), ("POST", "/v1/chat/completions"))
        self.assertEqual(headers["Authorization"], f"Bearer {KEY}")
        self.assertEqual(headers["Content-Type"], "application/json")
        self.assertEqual(body, openai.build_request("some-model", "You are HAL.", HISTORY))

    def test_response(self):
        self.assertEqual(openai.parse_response(said(" hi \n")), "hi")
        # A refusal comes with no content: there is no line to speak.
        self.assertEqual(openai.parse_response(said(None, refusal="I can't help with that.")), "")
        for bad in ({}, {"choices": []}, {"choices": [{}]}, {"choices": [{"message": 5}]}, {"choices": "x"},
                    {"error": {"message": "The model does not exist"}}, {"error": "odd"}, [], None):
            with self.assertRaises(providers.ProviderError, msg=repr(bad)):
                openai.parse_response(bad)

    def test_a_refusal_is_an_empty_reply_and_a_line_in_the_log(self):
        import contextlib
        import io
        noted = io.StringIO()
        self.api.will_say(said(None, refusal="I can't help with that."))
        with contextlib.redirect_stderr(noted):
            self.assertEqual(self.provider.chat("s", HISTORY), "")
        self.assertIn("OpenAI declined", noted.getvalue())
        self.assertNotIn("I can't help", noted.getvalue())  # the model's words stay out of the log

    def test_errors_are_provider_errors_that_never_show_the_key(self):
        # OpenAI's own 401 message quotes part of the key back.
        for status, payload, says in (
                (401, {"error": {"message": f"Incorrect API key provided: {KEY[:12]}***"}}, "brain/.env"),
                (404, {"error": {"message": "The model `some-model` does not exist"}}, "some-model"),
                (429, {"error": {"message": "You exceeded your current quota"}}, "quota"),
                (400, {"error": {"message": "Unsupported parameter"}}, "Unsupported parameter"),
                (500, b"<html>bad gateway</html>", "500"),
                (200, b"not json", "OpenAI")):
            self.api.will_say(payload, status)
            with self.assertRaises(providers.ProviderError, msg=str(status)) as caught:
                self.provider.chat("s", HISTORY)
            self.assertIn(says, str(caught.exception))
            self.assertNotIn(KEY[:12], str(caught.exception))
            self.assertLessEqual(len(str(caught.exception)), 200)

    def test_unreachable_is_a_provider_error(self):
        dead = openai.OpenAIProvider(KEY, "m", base_url="http://127.0.0.1:9/v1", timeout_s=2,
                                     opener=urllib.request.build_opener(urllib.request.ProxyHandler({})))
        with self.assertRaises(providers.ProviderError):
            dead.chat("s", HISTORY)
        self.assertIs(dead.warm(), False)

    def test_the_key_is_not_carried_to_wherever_a_redirect_points(self):
        for status in (301, 302, 307):
            self.api.will_say(self.api.url.encode() + b"/elsewhere", status)
            with self.assertRaises(providers.ProviderError, msg=str(status)):
                self.provider.chat("s", HISTORY)
        self.assertEqual([path for _, path, _, _ in self.api.seen], ["/v1/chat/completions"] * 3)

    def test_warm_checks_the_key_and_the_model_without_asking_anything(self):
        self.api.will_say({"id": "some-model", "object": "model"})
        self.assertIs(self.provider.warm(), True)
        method, path, headers, body = self.api.seen[0]
        self.assertEqual((method, path, body), ("GET", "/v1/models/some-model", None))
        self.assertEqual(headers["Authorization"], f"Bearer {KEY}")
        self.api.will_say({"error": {"message": "no such model"}}, 404)
        self.assertIs(self.provider.warm(), False)


class ChoosingTest(unittest.TestCase):
    def setUp(self):
        self.sdk = FakeSdk()
        real = claude.load_sdk
        claude.load_sdk = lambda: self.sdk
        self.addCleanup(setattr, claude, "load_sdk", real)

    def test_claude(self):
        p = providers.make_provider({"HAL_BRAIN_PROVIDER": "claude", "ANTHROPIC_API_KEY": f" {KEY} "})
        self.assertEqual((p.name, p.model), ("claude", "claude-opus-5-5"))
        self.assertEqual(self.sdk.made[0]["api_key"], KEY)
        p = providers.make_provider({"HAL_BRAIN_PROVIDER": "Claude", "HAL_BRAIN_MODEL": "claude-haiku-5-5",
                                     "ANTHROPIC_API_KEY": KEY})
        self.assertEqual(p.model, "claude-haiku-5-5")

    def test_openai(self):
        for name in ("openai", "ChatGPT"):
            p = providers.make_provider({"HAL_BRAIN_PROVIDER": name, "HAL_BRAIN_MODEL": "some-model",
                                         "OPENAI_API_KEY": KEY})
            self.assertEqual((p.name, p.model), ("openai", "some-model"))

    def test_a_cloud_model_needs_its_key_in_the_settings_file(self):
        for env, says in (({"HAL_BRAIN_PROVIDER": "claude"}, "ANTHROPIC_API_KEY"),
                          ({"HAL_BRAIN_PROVIDER": "claude", "ANTHROPIC_API_KEY": "  "}, "ANTHROPIC_API_KEY"),
                          ({"HAL_BRAIN_PROVIDER": "openai", "HAL_BRAIN_MODEL": "m"}, "OPENAI_API_KEY")):
            with self.assertRaises(ValueError, msg=repr(env)) as caught:
                providers.make_provider(env)
            self.assertIn(says, str(caught.exception))
        self.assertEqual(self.sdk.made, [])

    def test_the_local_models_name_is_not_sent_to_a_cloud_model(self):
        # Changing HAL_BRAIN_PROVIDER and forgetting HAL_BRAIN_MODEL is the easy mistake.
        for env in ({"HAL_BRAIN_PROVIDER": "claude", "HAL_BRAIN_MODEL": "qwen3.6:35b-a3b", "ANTHROPIC_API_KEY": KEY},
                    {"HAL_BRAIN_PROVIDER": "openai", "HAL_BRAIN_MODEL": "qwen3.6:35b-a3b", "OPENAI_API_KEY": KEY},
                    {"HAL_BRAIN_PROVIDER": "openai", "HAL_BRAIN_MODEL": "claude-opus-5-5", "OPENAI_API_KEY": KEY},
                    {"HAL_BRAIN_PROVIDER": "openai", "OPENAI_API_KEY": KEY}):  # OpenAI: no model is assumed
            with self.assertRaises(ValueError, msg=repr(env)) as caught:
                providers.make_provider(env)
            self.assertIn("HAL_BRAIN_MODEL", str(caught.exception))
            self.assertNotIn(KEY, str(caught.exception))

    def test_an_unknown_name_lists_the_known_ones(self):
        with self.assertRaises(ValueError) as caught:
            providers.make_provider({"HAL_BRAIN_PROVIDER": "nope"})
        for name in ("ollama", "claude", "openai"):
            self.assertIn(name, str(caught.exception))

    def test_every_model_says_what_to_check_when_it_does_not_answer(self):
        local = providers.make_provider({})
        cloud = providers.make_provider({"HAL_BRAIN_PROVIDER": "claude", "ANTHROPIC_API_KEY": KEY})
        gpt = providers.make_provider({"HAL_BRAIN_PROVIDER": "openai", "HAL_BRAIN_MODEL": "m", "OPENAI_API_KEY": KEY})
        self.assertIn("Ollama", local.hint)
        self.assertIn("ANTHROPIC_API_KEY", cloud.hint)
        self.assertIn("OPENAI_API_KEY", gpt.hint)


if __name__ == "__main__":
    unittest.main()
