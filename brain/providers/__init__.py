"""The models that can write a HAL's lines. A provider has `name`, `model`, `hint` (what to check
when it doesn't answer), `chat(system, messages, max_tokens=80) -> str` (raises ProviderError; an
empty answer means it had nothing to say) and `warm()` (never raises; False if it isn't ready).

ollama runs on this computer and is the default. claude and openai are on the internet: they are
used only when brain/.env names one and holds its key (docs/SETUP.md, step 9)."""


class ProviderError(Exception):
    """The model couldn't answer: not running, unreachable, too slow, or it sent something unusable."""


def _cloud_key(env: dict, setting: str, name: str) -> str:
    key = (env.get(setting) or "").strip()
    if not key:
        raise ValueError(f"HAL_BRAIN_PROVIDER={name} needs {setting} in brain/.env (docs/SETUP.md, step 9)")
    return key


def make_provider(env: dict):
    """The provider the settings ask for (HAL_BRAIN_PROVIDER, default ollama; HAL_BRAIN_MODEL)."""
    name = (env.get("HAL_BRAIN_PROVIDER") or "ollama").strip().lower()
    model = (env.get("HAL_BRAIN_MODEL") or "").strip()
    if name == "ollama":
        from . import ollama
        return ollama.OllamaProvider(model=model or ollama.DEFAULT_MODEL)
    if name == "claude":
        from . import claude
        key = _cloud_key(env, "ANTHROPIC_API_KEY", name)
        if model and not model.startswith("claude-"):
            raise ValueError(f"HAL_BRAIN_MODEL={model} isn't a Claude model: delete that line to use "
                             f"{claude.DEFAULT_MODEL}, or name another Claude model")
        return claude.ClaudeProvider(key, model or claude.DEFAULT_MODEL)
    if name in ("openai", "chatgpt"):
        from . import ollama, openai
        key = _cloud_key(env, "OPENAI_API_KEY", "openai")
        if not model or model == ollama.DEFAULT_MODEL or model.startswith("claude-"):
            raise ValueError("HAL_BRAIN_PROVIDER=openai needs HAL_BRAIN_MODEL set to the name of one of "
                             "OpenAI's models (https://developers.openai.com/api/docs/models)")
        return openai.OpenAIProvider(key, model)
    raise ValueError(f"unknown HAL_BRAIN_PROVIDER: {name!r} (this version knows: ollama, claude, openai)")
