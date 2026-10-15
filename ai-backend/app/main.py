"""ToneForge AI backend entry point.

Run locally with:
    uvicorn app.main:app --host 127.0.0.1 --port 8765 --reload

The plugin (plugin/Source/ai/AiClient) talks to this service over HTTP on
localhost only - see docs/ARCHITECTURE.md section 4.2/5. This process never
touches real-time audio and is free to allocate, block on IO, and call out
to Ollama or a future cloud provider.

Note: there is no route at "/" - a request there correctly returns FastAPI's
default {"detail": "Not Found"}. Use /health, /generate, /modify, /explain,
/song-reference, or browse /docs for the interactive API explorer.
"""

from __future__ import annotations

from fastapi import FastAPI

from app.api import routes_explain, routes_generate, routes_health, routes_song_reference

app = FastAPI(
    title="ToneForge AI Backend",
    description="Turns natural language, song references, and (later) audio analysis into guitar tone presets.",
    version="0.1.0",
)

app.include_router(routes_generate.router)
app.include_router(routes_explain.router)
app.include_router(routes_song_reference.router)
app.include_router(routes_health.router)
