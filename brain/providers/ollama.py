"""A local model through Ollama (https://ollama.com) on this computer: nothing leaves the house."""
import json
import urllib.error
import urllib.request

from . import ProviderError

DEFAULT_MODEL = "qwen3.6:35b-a3b"
DEFAULT_HOST = "http://127.0.0.1:11434"


def build_request(model: str, system: str, messages: list[dict], max_tokens: int) -> dict:
    return {
        "model": model,
        "stream": False,
        "think": False,       # a spoken reply has no time for a thinking pass
        "keep_alive": "30m",  # stay loaded: the first reply after loading takes about 13 s
        "options": {"num_predict": max_tokens, "temperature": 0.8},
        "messages": [{"role": "system", "content": system}] + list(messages),
    }


def parse_response(payload) -> str:
    if not isinstance(payload, dict):
        raise ProviderError("Ollama sent something that isn't an answer")
    if payload.get("error"):
        raise ProviderError(f"Ollama: {payload['error']}")
    message = payload.get("message")
    content = message.get("content") if isinstance(message, dict) else None
    if not isinstance(content, str):
        raise ProviderError("Ollama's answer has no text")
    return content.strip()


class OllamaProvider:
    name = "ollama"

    def __init__(self, model: str = DEFAULT_MODEL, host: str = DEFAULT_HOST, timeout_s: float = 25):
        self.model, self._host, self._timeout = model, host.rstrip("/"), timeout_s

    def chat(self, system: str, messages: list[dict], max_tokens: int = 80) -> str:
        body = json.dumps(build_request(self.model, system, messages, max_tokens)).encode()
        request = urllib.request.Request(self._host + "/api/chat", body, {"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as reply:
                payload = json.load(reply)
        except urllib.error.HTTPError as e:  # Ollama explains itself in the body (e.g. the model isn't pulled)
            try:
                payload = json.load(e)
            except ValueError:
                raise ProviderError(f"Ollama answered {e.code}") from None
        except (OSError, ValueError) as e:  # not running, timed out, or not JSON
            raise ProviderError(f"Ollama isn't answering: {e}") from None
        return parse_response(payload)

    def warm(self) -> None:
        """Load the model so the first real question isn't the slow one."""
        try:
            self.chat("Reply with one word.", [{"role": "user", "content": "Ready?"}], max_tokens=1)
        except ProviderError:
            pass
