"""Song/artist -> preset lookup.

Tries a fuzzy match against the curated database first (fast, no network,
hand-tuned descriptors). On a miss, falls back to asking the configured
AIProvider to infer a plausible descriptor from its general knowledge of the
song/artist. Every result - curated or inferred - is tagged with match_type
and a confidence score so the API/UI can never present this as an exact
recreation. See docs/ARCHITECTURE.md 5.2.
"""

from __future__ import annotations

from dataclasses import dataclass

from rapidfuzz import fuzz

from app.core.mapping import descriptor_from_preset
from app.core.tone_descriptor import ToneDescriptor
from app.providers.base import AIProvider
from app.song_reference.db import CURATED_SONGS, SongReference

# Below this fuzzy-match score (0-100), we don't trust the curated match and
# fall back to the inferred path instead of returning a wrong song's tone.
_MATCH_THRESHOLD = 70.0


@dataclass
class SongLookupResult:
    match_type: str  # "song_curated" | "song_inferred"
    confidence: float
    title: str
    artist: str
    descriptor: ToneDescriptor
    disclaimer: str = "Inspired by - approximation, not an exact recreation."


def _score(query: str, reference: SongReference) -> float:
    combined = f"{reference.title} {reference.artist}"
    return max(
        fuzz.token_set_ratio(query, reference.title),
        fuzz.token_set_ratio(query, combined),
    )


def find_curated_match(query: str) -> tuple[SongReference, float] | None:
    best: tuple[SongReference, float] | None = None
    for reference in CURATED_SONGS:
        score = _score(query, reference)
        if best is None or score > best[1]:
            best = (reference, score)
    if best is not None and best[1] >= _MATCH_THRESHOLD:
        return best
    return None


async def lookup_song(query: str, provider: AIProvider) -> SongLookupResult:
    curated = find_curated_match(query)
    if curated is not None:
        reference, score = curated
        return SongLookupResult(
            match_type="song_curated",
            confidence=round(min(score, 100.0) / 100.0, 2),
            title=reference.title,
            artist=reference.artist,
            descriptor=reference.descriptor,
        )

    # Inferred fallback: ask `provider` to infer a full preset from its
    # general knowledge of the song/artist (via infer_song_tone(), NOT
    # generate() - see AIProvider.infer_song_tone's docstring in
    # app/providers/base.py for why that needs to be a dedicated method
    # with its own prompt, not generate() called with a wrapped prompt),
    # then reconstruct a ToneDescriptor from that ENTIRE response via
    # mapping.descriptor_from_preset() - every quality the provider
    # inferred (gain, brightness, compression, reverb amount+character,
    # modulation amount+type, distortion style), not just gain. This used
    # to discard everything but gain and fall back to a keyword match on
    # the query text for the rest, which usually matched nothing at all
    # for a song/artist string - see descriptor_from_preset()'s docstring
    # in app/core/mapping.py for the full story of that gap.
    #
    # `provider` is named generically (not `fallback_provider`, despite
    # that being this function's original parameter name) because the
    # route previously always passed the *mock* provider here regardless
    # of whether Ollama was configured/available - meaning "ask the
    # provider" was never actually true end to end. See
    # app/api/routes_song_reference.py, which now passes the real primary
    # provider (falling back to mock only if that one is unavailable, same
    # pattern /generate and /modify already use).
    generated_preset = await provider.infer_song_tone(query)
    inferred_descriptor = descriptor_from_preset(generated_preset)

    return SongLookupResult(
        match_type="song_inferred",
        confidence=0.35,
        title=query,
        artist="unknown",
        descriptor=inferred_descriptor,
    )
