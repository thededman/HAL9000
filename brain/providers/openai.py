"""ChatGPT, through OpenAI's API: an optional model on the internet in place of the local one.
Every line then sends the HAL's instructions, the facts he knows, the last few lines of the chat
and the question to OpenAI. Recordings never leave this computer. It is turned on in brain/.env
(docs/SETUP.md, step 9) and needs nothing installed."""
import http.client
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

from . import ProviderError

BASE_URL = "https://api.openai.com/v1"
MAX_TOKENS = 4096  # a line is a sentence or two, but a model's reasoning counts toward this as well


class NoRedirects(urllib.request.HTTPRedirectHandler):
    """The key goes to OpenAI's address and nowhere that address might point."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def build_request(model: str, system: str, messages: list[dict]) -> dict:
    """A Chat Completions request. Only what every model accepts: no temperature, no reasoning setting."""
    return {
        "model": model,
        "max_completion_tokens": MAX_TOKENS,
        "messages": [{"role": "developer", "content": system}] + list(messages),
    }


def parse_response(payload) -> str:
    if not isinstance(payload, dict):
        raise ProviderError("OpenAI sent something that isn't an answer")
    if payload.get("error"):
        error = payload["error"]
        raise ProviderError(f"OpenAI: {error.get('message') if isinstance(error, dict) else error}"[:200])
    choices = payload.get("choices")
    first = choices[0] if isinstance(choices, list) and choices else None
    message = first.get("message") if isinstance(first, dict) else None
    if not isinstance(message, dict):
        raise ProviderError("OpenAI's answer has no message")
    content = message.get("content")
    return content.strip() if isinstance(content, str) else ""  # a refusal has no content: no line to speak


class OpenAIProvider:
    name = "openai"
    hint = "check the internet connection, and OPENAI_API_KEY and HAL_BRAIN_MODEL in brain/.env."

    def __init__(self, api_key: str, model: str, base_url: str = BASE_URL, timeout_s: float = 25, opener=None):
        self.model, self._key, self._base, self._timeout = model, api_key, base_url.rstrip("/"), timeout_s
        self._opener = opener or urllib.request.build_opener(NoRedirects())

    def _call(self, method: str, path: str, body: dict | None = None):
        headers = {"Authorization": f"Bearer {self._key}"}
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self._base + path, data, headers, method=method)
        try:
            with self._opener.open(request, timeout=self._timeout) as reply:
                return json.load(reply)
        except urllib.error.HTTPError as e:
            raise ProviderError(self._explain(e)) from None
        except (OSError, ValueError, http.client.HTTPException) as e:  # no internet, timed out, cut off, not JSON
            raise ProviderError(f"OpenAI isn't answering: {type(e).__name__}: {e}"[:200]) from None

    def _explain(self, e: urllib.error.HTTPError) -> str:
        if e.code == 401:  # OpenAI's own message quotes part of the key, so it isn't passed on
            return "OpenAI refused the key (OPENAI_API_KEY in brain/.env)"
        try:
            message = str(json.load(e)["error"]["message"])
        except (OSError, ValueError, KeyError, TypeError, http.client.HTTPException):
            message = ""
        about = f" for {self.model}" if e.code == 404 else ""
        return f"OpenAI answered {e.code}{about}: {message}".replace(self._key, "***")[:200]

    def chat(self, system: str, messages: list[dict], max_tokens: int = 80) -> str:
        """`max_tokens` is the local model's measure and isn't used: see MAX_TOKENS."""
        payload = self._call("POST", "/chat/completions", build_request(self.model, system, messages))
        text = parse_response(payload)
        if not text and payload["choices"][0]["message"].get("refusal"):  # the log says why HAL had nothing to say
            print("OpenAI declined that question", file=sys.stderr, flush=True)
        return text

    def warm(self) -> bool:
        """There is nothing to load, so this only checks the key and the model's name. No line is
        written, so nothing is paid for. False if OpenAI didn't confirm them."""
        try:
            self._call("GET", "/models/" + urllib.parse.quote(self.model, safe=":"))
            return True
        except Exception:  # whatever it was, checking must not take anything else down
            return False
