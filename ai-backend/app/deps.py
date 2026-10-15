"""FastAPI dependency wiring: constructs providers from Settings.

Kept separate from main.py so route modules can `from app.deps import
get_primary_provider` without importing the FastAPI app object itself
(avoids import cycles and keeps route modules unit-testable in isolation).
"""

from __future__ import annotations

from functools import lru_cache

from app.config import get_settings
from app.providers.base import AIProvider
from app.providers.mock_provider import MockProvider
from app.providers.ollama_provider import OllamaProvider


@lru_cache
def get_mock_provider() -> MockProvider:
    return MockProvider()


@lru_cache
def get_ollama_provider() -> OllamaProvider:
    settings = get_settings()
    return OllamaProvider(base_url=settings.ollama_base_url, model=settings.ollama_model)


def get_primary_provider() -> AIProvider:
    settings = get_settings()
    if settings.primary_provider == "mock":
        return get_mock_provider()
    return get_ollama_provider()


def get_fallback_provider() -> AIProvider:
    # The fallback is always the mock provider: it is the one provider that
    # is guaranteed to never fail or require a network round trip.
    return get_mock_provider()
