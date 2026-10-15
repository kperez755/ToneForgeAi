"""Runtime configuration, read from environment variables so the same code
works in tests (mock-only), local dev (Ollama on localhost), and later
deployment configurations, without code changes.
"""

from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    # "mock" or "ollama". Defaults to "ollama" for real usage (falls back to
    # mock automatically per-request if Ollama is unreachable - see
    # app/api/routes_generate.py), but tests explicitly force "mock" so CI
    # never depends on a running Ollama instance.
    primary_provider: str = os.environ.get("TONEFORGE_PRIMARY_PROVIDER", "ollama")
    ollama_base_url: str = os.environ.get("TONEFORGE_OLLAMA_URL", "http://localhost:11434")
    # Default matches the model actually pulled in the dev Ollama container
    # this project has been tested against (gemma3:4b) - override with
    # TONEFORGE_OLLAMA_MODEL if you're running a different one. Ollama
    # resolves an untagged name to its ":latest" tag automatically, but
    # being explicit here avoids surprises if multiple tags of the same
    # model are ever pulled side by side.
    ollama_model: str = os.environ.get("TONEFORGE_OLLAMA_MODEL", "gemma3:4b")


def get_settings() -> Settings:
    return Settings()
