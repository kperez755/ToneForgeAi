from __future__ import annotations

import logging

from fastapi import APIRouter, Depends

from app.core.mapping import descriptor_to_preset
from app.deps import get_fallback_provider, get_primary_provider
from app.providers.base import AIProvider, ProviderUnavailableError
from app.schemas.preset import PresetDTO
from app.schemas.requests import SongReferenceRequest
from app.song_reference.lookup import lookup_song

logger = logging.getLogger(__name__)
router = APIRouter(tags=["song-reference"])


@router.post("/song-reference", response_model=PresetDTO)
async def song_reference(
    request: SongReferenceRequest,
    primary: AIProvider = Depends(get_primary_provider),
    fallback: AIProvider = Depends(get_fallback_provider),
) -> PresetDTO:
    """Identifies (or infers) the tone of a referenced song and returns a
    preset "inspired by" it. Never claims an exact recreation - see
    AiProvenanceDTO.disclaimer, which is required and validated non-empty.

    A curated match (see app/song_reference/db.py) never touches an AI
    provider at all - it's a hand-tuned descriptor, looked up by fuzzy
    string match. Only a miss (an "inferred" match) calls out to a
    provider. This used to unconditionally use the mock provider for that
    call regardless of what TONEFORGE_PRIMARY_PROVIDER was set to - a real
    gap, not a deliberate choice: it meant "infer this song's tone from
    general knowledge" never actually consulted a real model, even with
    Ollama configured and running, silently falling back to a keyword
    match on the query text (which rarely contains useful tone words - a
    song title mostly doesn't). Now mirrors the same primary-with-mock-
    fallback pattern /generate and /modify already use.
    """
    try:
        result = await lookup_song(request.query, provider=primary)
    except ProviderUnavailableError as exc:
        logger.warning("Primary provider %s unavailable for song lookup, falling back to mock: %s", primary.name, exc)
        result = await lookup_song(request.query, provider=fallback)

    return descriptor_to_preset(
        result.descriptor,
        name=f"{result.title} - Inspired Tone",
        prompt=request.query,
        provider_name="song_reference",
        match_type=result.match_type,
        confidence=result.confidence,
        disclaimer=result.disclaimer,
    )
