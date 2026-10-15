import pytest

from app.providers.mock_provider import MockProvider
from app.song_reference.lookup import find_curated_match, lookup_song


@pytest.fixture
def fallback():
    return MockProvider()


def test_exact_title_matches_curated_entry():
    match = find_curated_match("Cliffs of Dover")
    assert match is not None
    reference, score = match
    assert reference.title == "Cliffs of Dover"
    assert reference.artist == "Eric Johnson"
    assert score >= 90.0


def test_fuzzy_title_with_typo_still_matches():
    match = find_curated_match("cliffs of dovr")
    assert match is not None
    assert match[0].title == "Cliffs of Dover"


def test_artist_plus_title_query_matches():
    match = find_curated_match("Dire Straits Sultans of Swing")
    assert match is not None
    assert match[0].title == "Sultans of Swing"


def test_unrelated_query_does_not_match_curated_db():
    match = find_curated_match("some completely unrelated made up song title xyzzy plugh")
    assert match is None


async def test_lookup_song_curated_hit_returns_high_confidence_and_disclaimer(fallback):
    result = await lookup_song("Cliffs of Dover", provider=fallback)
    assert result.match_type == "song_curated"
    assert result.confidence > 0.7
    assert result.artist == "Eric Johnson"
    assert "approximation" in result.disclaimer.lower() or "inspired" in result.disclaimer.lower()


async def test_lookup_song_miss_falls_back_to_inferred(fallback):
    result = await lookup_song("a song that definitely is not in the curated database", provider=fallback)
    assert result.match_type == "song_inferred"
    assert result.confidence < 0.7
    assert result.disclaimer


def test_mac_demarco_curated_entry_is_chorus_forward_and_clean():
    # Added after a real Ollama-inferred guess for this song came back
    # noticeably off (no modulation at all, generic mid-gain chain) despite
    # chorus being THE defining element of DeMarco's tone - see
    # docs/ROADMAP.md's M6 log. Sourced from real gear-rundown coverage, not
    # guessed - regression test for that sourcing actually landing on the
    # right descriptor shape: low gain (clean amps), modulation_amount well
    # above every other curated entry's (all 0.0), and modulation_type
    # explicitly Chorus (0), not left at some other default.
    match = find_curated_match("Freaking Out the Neighborhood by Mac DeMarco")
    assert match is not None
    reference, score = match
    assert reference.title == "Freaking Out the Neighborhood"
    assert score >= 90.0
    assert reference.descriptor.gain_amount < 0.15
    assert reference.descriptor.modulation_amount > 0.5
    assert reference.descriptor.modulation_type == 0
