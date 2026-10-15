import pytest

from app.providers.mock_provider import MockProvider


@pytest.fixture
def provider():
    return MockProvider()


async def test_generate_returns_a_valid_preset_with_provenance(provider):
    preset = await provider.generate("Give me a Cliffs of Dover style lead tone.")
    assert preset.name
    assert len(preset.chain) > 0
    assert preset.ai_provenance is not None
    assert preset.ai_provenance.match_type == "generated"
    assert preset.ai_provenance.disclaimer


async def test_generate_is_deterministic_for_the_same_prompt(provider):
    a = await provider.generate("warm clean jazz tone")
    b = await provider.generate("warm clean jazz tone")
    # ids differ (uuid4 per call) but the shaped parameters must match.
    a_params = [m.parameters for m in a.chain]
    b_params = [m.parameters for m in b.chain]
    assert a_params == b_params


async def test_high_gain_keywords_increase_drive_gain_relative_to_clean(provider):
    # Both of these stay on the "overdrive" style (no metal/distortion/heavy
    # keywords), so this is a fair like-for-like drive.v1 gain comparison.
    clean = await provider.generate("clean jazz tone")
    crunchy = await provider.generate("crunchy blues tone")

    clean_drive = next(m for m in clean.chain if m.module_id == "drive.v1")
    crunchy_drive = next(m for m in crunchy.chain if m.module_id == "drive.v1")

    assert crunchy_drive.parameters["gain_db"] > clean_drive.parameters["gain_db"]


async def test_heavy_metal_prompts_select_distortion_not_overdrive(provider):
    # "heavy"/"metal"/"djent"-style prompts get the distortion.v1 module
    # (symmetric, earlier-clipping, cascaded twice - see
    # core/include/tfcore/dsp/modules/Distortion.h) instead of drive.v1's
    # transparent overdrive character - a real DSP difference, not just a
    # bigger gain number on the same clip curve.
    heavy = await provider.generate("heavy distorted metal tone")
    bluesy = await provider.generate("smooth bluesy overdrive")

    assert any(m.module_id == "distortion.v1" for m in heavy.chain)
    assert not any(m.module_id == "drive.v1" for m in heavy.chain)

    assert any(m.module_id == "drive.v1" for m in bluesy.chain)
    assert not any(m.module_id == "distortion.v1" for m in bluesy.chain)


async def test_reverb_character_and_modulation_type_follow_prompt_keywords(provider):
    spring = await provider.generate("surfy spring reverb tone")
    spring_reverb = next(m for m in spring.chain if m.module_id == "reverb.v1")
    assert spring_reverb.parameters["character"] == 2.0  # Spring

    tremolo = await provider.generate("slow pulsing tremolo tone")
    tremolo_mod = next(m for m in tremolo.chain if m.module_id == "modulation.v1")
    assert tremolo_mod.parameters["type"] == 3.0  # Tremolo


async def test_chain_never_exceeds_the_8_slot_pedalboard_budget(provider):
    # Regression test for a real bug: build_default_chain() used to
    # unconditionally emit 10 modules while the pedalboard only has 8 slots
    # (toneforge::kNumSlots), and PluginProcessor::applyPreset() maps
    # chain[i] onto slot i by raw position - so reverb.v1 and limiter.v1
    # (indices 8 and 9) were silently dropped from every single AI-generated
    # preset before ever reaching the plugin. See docs/ARCHITECTURE.md 5.4.
    prompts = [
        "ambient shoegaze wall of sound with chorus delay and reverb",
        "heavy distorted metal tone with a noise gate and tight compression",
        "clean jazz tone",
        "tight funk rhythm",
    ]
    for prompt in prompts:
        preset = await provider.generate(prompt)
        assert len(preset.chain) <= 8, f"{prompt!r} produced {len(preset.chain)} modules"


async def test_reverb_is_not_starved_out_by_a_maximal_effects_prompt(provider):
    # The specific bug this whole redesign targets: a prompt that wants
    # reverb (among other things) must actually get reverb.v1 somewhere in
    # the chain, not have it silently discarded because the chain ran out
    # of room.
    preset = await provider.generate("ambient shoegaze wall of sound with chorus delay and reverb")
    assert any(m.module_id == "reverb.v1" for m in preset.chain)


async def test_ambient_keywords_increase_reverb_and_delay(provider):
    dry = await provider.generate("tight funk rhythm")
    ambient = await provider.generate("ambient shoegaze wall of sound")

    dry_reverb = next(m for m in dry.chain if m.module_id == "reverb.v1")
    ambient_reverb = next(m for m in ambient.chain if m.module_id == "reverb.v1")

    assert ambient_reverb.parameters["mix"] > dry_reverb.parameters["mix"]
    assert ambient_reverb.bypassed is False


async def test_modify_adjusts_existing_preset_without_discarding_other_modules(provider):
    original = await provider.generate("clean jazz tone")
    modified = await provider.modify(original, "make it more distorted and add reverb")

    original_drive = next(m for m in original.chain if m.module_id == "drive.v1")
    modified_drive = next(m for m in modified.chain if m.module_id == "drive.v1")
    assert modified_drive.parameters["gain_db"] >= original_drive.parameters["gain_db"]

    # Module count/order preserved - modify should not rebuild the chain.
    assert [m.module_id for m in modified.chain] == [m.module_id for m in original.chain]
    assert modified.ai_provenance.match_type == "modified"


async def test_explain_mentions_active_modules(provider):
    preset = await provider.generate("bright lead tone")
    explanation = await provider.explain(preset)
    assert isinstance(explanation, str)
    assert len(explanation) > 0


async def test_is_available_always_true(provider):
    assert await provider.is_available() is True


async def test_infer_song_tone_returns_a_valid_preset(provider):
    # MockProvider's infer_song_tone() is just generate() with a wrapped
    # prompt - it has no "knowledge" to reason about either way, so unlike
    # OllamaProvider's version there's no honesty/genre-fallback distinction
    # to test here, just that the method exists and behaves sanely.
    preset = await provider.infer_song_tone("some song title")
    assert preset.name
    assert len(preset.chain) > 0
    assert preset.ai_provenance is not None
