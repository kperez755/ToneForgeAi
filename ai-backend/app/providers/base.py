"""Provider abstraction.

Every backend that can turn language into a preset - the always-available
MockProvider, the local-first OllamaProvider, and future cloud providers -
implements this Protocol. Call sites (the FastAPI routes) depend only on
`AIProvider`, never on a concrete provider class, which is what lets
`docs/ROADMAP.md` M6 (Ollama quality pass) and future cloud-provider work
happen without touching route code.
"""

from __future__ import annotations

from typing import Protocol, runtime_checkable

from app.schemas.preset import PresetDTO


class ProviderUnavailableError(RuntimeError):
    """Raised when a provider cannot currently serve requests (e.g. Ollama
    is not running, or the configured model is not pulled). Callers - see
    app/api/routes_generate.py - are expected to catch this and fall back to
    MockProvider rather than surface a raw 500 to the plugin.
    """


@runtime_checkable
class AIProvider(Protocol):
    name: str

    async def generate(self, prompt: str) -> PresetDTO:
        """Create a brand-new preset from a natural-language description."""
        ...

    async def infer_song_tone(self, query: str) -> PresetDTO:
        """Infer a preset for a song/artist NOT in the curated database (see
        app/song_reference/lookup.py) from the provider's own knowledge.

        Deliberately a separate method from generate(), not just generate()
        called with a wrapped prompt (which is what this used to be): a
        model asked "what's the tone of song X" tends to produce something
        plausible-sounding with a straight face even with zero real
        knowledge of X, defaulting to a generic template rather than
        reasoning about what it's actually uncertain about. This method's
        prompt (see OllamaProvider's _SONG_INFERENCE_SYSTEM_PROMPT) asks
        the model to explicitly reason from genre/era/production
        conventions when it doesn't know the specific song, instead of
        inventing false-confident specifics. This does not make an
        obscure song's result *accurate* - there is no retrieval/grounding
        here, it's still working purely from the model's training-time
        knowledge - it just makes the failure mode "plausible genre fit"
        instead of "confidently arbitrary." A real fix for truly obscure
        songs would need to actually search for and retrieve real
        information at request time, which this project does not do yet.
        """
        ...

    async def modify(self, preset: PresetDTO, instruction: str) -> PresetDTO:
        """Return a modified copy of `preset` per a natural-language instruction."""
        ...

    async def explain(self, preset: PresetDTO) -> str:
        """Return a short, human-readable explanation of what a preset does
        and why (used by the 'Explain Tone' AI panel action)."""
        ...

    async def is_available(self) -> bool:
        """Cheap liveness check, used by /health. Must not raise."""
        ...
