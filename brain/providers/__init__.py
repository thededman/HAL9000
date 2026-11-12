"""The models that can write a HAL's lines. A provider has `name`, `model`,
`chat(system, messages, max_tokens=80) -> str` (raises ProviderError) and `warm()` (never raises)."""


class ProviderError(Exception):
    """The model couldn't answer: not running, too slow, or it sent something unusable."""


def make_provider(env: dict):
    """The provider the settings ask for (HAL_BRAIN_PROVIDER, default ollama; HAL_BRAIN_MODEL)."""
    name = (env.get("HAL_BRAIN_PROVIDER") or "ollama").strip().lower()
    model = (env.get("HAL_BRAIN_MODEL") or "").strip()
    if name == "ollama":
        from . import ollama
        return ollama.OllamaProvider(model=model or ollama.DEFAULT_MODEL)
    raise ValueError(f"unknown HAL_BRAIN_PROVIDER: {name!r} (this version knows: ollama)")
