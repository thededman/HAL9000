"""Claude, through Anthropic's API: an optional model on the internet in place of the local one.
Every line then sends the HAL's instructions, the facts he knows, the last few lines of the chat
and the question to Anthropic. Recordings never leave this computer. It is turned on in
brain/.env (docs/SETUP.md, step 9) and needs Anthropic's library:
tools/.venv/bin/pip install anthropic"""
import sys

from . import ProviderError

DEFAULT_MODEL = "claude-opus-5-5"
MAX_TOKENS = 4096  # a line is a sentence or two, but the model's thinking counts toward this as well
FALLBACK_BETA = "server-side-fallback-2026-07-01"
# These models' safety filters sometimes decline a harmless question. For them Anthropic is asked
# to let another Claude model answer instead, within the same request.
FALLBACK_MODELS = frozenset({"claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5",
                             "claude-fable-5-2", "claude-fable-5-1"})


def load_sdk():
    """Anthropic's library, which is only needed (and only loaded) when Claude is the chosen model."""
    try:
        import anthropic
    except ImportError:
        raise ValueError("HAL_BRAIN_PROVIDER=claude needs Anthropic's library: "
                         "tools/.venv/bin/pip install anthropic") from None
    return anthropic


def build_request(model: str, system: str, messages: list[dict]) -> dict:
    request = {
        "model": model,
        "max_tokens": MAX_TOKENS,
        "system": system,
        "messages": list(messages),
        "output_config": {"effort": "low"},  # a spoken reply: think briefly, answer soon
    }
    if model in FALLBACK_MODELS:
        request["betas"] = [FALLBACK_BETA]
        request["fallbacks"] = "default"
    return request


def reply_text(reply) -> str:
    """The words of an answer: its text blocks, without the (empty) thinking ones. A question that
    was declined has no words, even if some had been written before it was stopped."""
    if reply.stop_reason == "refusal":
        return ""
    return "".join(block.text for block in reply.content if block.type == "text").strip()


class ClaudeProvider:
    name = "claude"
    hint = "check the internet connection, and ANTHROPIC_API_KEY and HAL_BRAIN_MODEL in brain/.env."

    def __init__(self, api_key: str, model: str = DEFAULT_MODEL, timeout_s: float = 20, sdk=None):
        self.model = model
        self._sdk = sdk or load_sdk()
        # One try: HAL Zero stops waiting after 30 seconds, and a second try would outlast him.
        self._client = self._sdk.Anthropic(api_key=api_key, timeout=timeout_s, max_retries=0)

    def chat(self, system: str, messages: list[dict], max_tokens: int = 80) -> str:
        """`max_tokens` is the local model's measure and isn't used: see MAX_TOKENS."""
        sdk = self._sdk
        request = build_request(self.model, system, messages)
        create = self._client.beta.messages.create if "betas" in request else self._client.messages.create
        try:
            reply = create(**request)
        except sdk.AuthenticationError:
            raise ProviderError("Claude: Anthropic refused the key (ANTHROPIC_API_KEY in brain/.env)") from None
        except sdk.PermissionDeniedError:
            raise ProviderError(f"Claude: that key may not use {self.model}") from None
        except sdk.NotFoundError:
            raise ProviderError(f"Claude: Anthropic has no model called {self.model}") from None
        except sdk.RateLimitError:
            raise ProviderError("Claude answered 429: too many requests just now") from None
        except sdk.APIStatusError as e:
            raise ProviderError(f"Claude answered {e.status_code}: {e.message}"[:200]) from None
        except sdk.APIConnectionError as e:  # no internet, or no answer within the time allowed
            raise ProviderError(f"Claude isn't answering: {type(e).__name__}") from None
        except sdk.AnthropicError as e:
            raise ProviderError(f"Claude: {type(e).__name__}") from None
        if reply.stop_reason == "refusal":  # the log says why HAL had nothing to say
            category = getattr(reply.stop_details, "category", None)
            print(f"Claude declined that question ({str(category)[:40]})", file=sys.stderr, flush=True)
        return reply_text(reply)

    def warm(self) -> bool:
        """There is nothing to load, so this only checks the key and the model's name. No line is
        written, so nothing is paid for. False if Anthropic didn't confirm them."""
        try:
            self._client.models.retrieve(self.model)
            return True
        except Exception:  # whatever it was, checking must not take anything else down
            return False
