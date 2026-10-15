"""OllamaProvider had no dedicated test coverage at all before this pass -
its retry loop, JSON-extraction, descriptor-merge, and (new) modify-patch
logic were only ever exercised implicitly, if at all. None of these tests
touch the network: `_chat` (the one method that actually calls Ollama) is
monkeypatched throughout, and `is_available()`'s test relies on nothing
actually listening on the default port in CI/this sandbox rather than
mocking httpx - a real, if narrow, integration check that the "offline
degrades gracefully" path works.
"""

from __future__ import annotations

import json

import pytest

from app.providers.base import ProviderUnavailableError
from app.providers.ollama_provider import (
    OllamaProvider,
    _SONG_INFERENCE_SYSTEM_PROMPT,
    _SYSTEM_PROMPT,
    _extract_json_object,
    _merge_descriptor,
)
from app.schemas.preset import AiProvenanceDTO, ModuleStateDTO, PresetDTO


@pytest.fixture
def provider():
    return OllamaProvider(model="test-model", max_retries=1, timeout_seconds=1.0)


def test_extract_json_object_handles_prose_and_code_fence_wrapping():
    text = 'Sure, here you go:\n```json\n{"gain_amount": 0.5}\n```\nHope that helps!'
    assert _extract_json_object(text) == {"gain_amount": 0.5}


def test_extract_json_object_raises_when_no_braces_present():
    with pytest.raises(ValueError):
        _extract_json_object("no json here at all")


def test_merge_descriptor_applies_numeric_and_categorical_overrides():
    from app.core.tone_descriptor import ToneDescriptor

    descriptor = ToneDescriptor()
    _merge_descriptor(
        descriptor,
        {
            "gain_amount": 0.9,
            "reverb_character": "Spring",  # case-insensitivity matters - models won't always match casing
            "modulation_type": "phaser",
            "distortion_style": "distortion",
        },
    )
    assert descriptor.gain_amount == 0.9
    assert descriptor.reverb_character == 2
    assert descriptor.modulation_type == 2
    assert descriptor.distortion_style == "distortion"


def test_merge_descriptor_ignores_unrecognized_categorical_values():
    from app.core.tone_descriptor import ToneDescriptor

    descriptor = ToneDescriptor()
    baseline_character = descriptor.reverb_character
    _merge_descriptor(descriptor, {"reverb_character": "underwater cave"})
    # An unrecognized value must not raise and must not corrupt the field -
    # the keyword-derived baseline stands.
    assert descriptor.reverb_character == baseline_character


async def test_generate_merges_model_json_onto_keyword_baseline(provider, monkeypatch):
    async def fake_chat(*, system, user):
        return json.dumps({"reverb_character": "hall", "modulation_type": "flanger", "summary_tags": ["custom"]})

    monkeypatch.setattr(provider, "_chat", fake_chat)
    preset = await provider.generate("some ambient tone")

    reverb = next(m for m in preset.chain if m.module_id == "reverb.v1")
    assert reverb.parameters["character"] == 0.0  # Hall
    assert preset.tags == ["custom"]
    assert preset.ai_provenance.provider == "ollama:test-model"


async def test_generate_raises_provider_unavailable_after_exhausting_retries(provider, monkeypatch):
    async def fake_chat(*, system, user):
        return "not json at all, sorry"

    monkeypatch.setattr(provider, "_chat", fake_chat)
    with pytest.raises(ProviderUnavailableError):
        await provider.generate("anything")


async def test_infer_song_tone_uses_a_different_system_prompt_than_generate(provider, monkeypatch):
    seen_systems = []

    async def fake_chat(*, system, user):
        seen_systems.append(system)
        return json.dumps({"reverb_character": "spring"})

    monkeypatch.setattr(provider, "_chat", fake_chat)
    await provider.generate("a prompt")
    await provider.infer_song_tone("Some Obscure Song")

    assert len(seen_systems) == 2
    generate_system, song_system = seen_systems
    assert generate_system != song_system
    # The whole point of infer_song_tone's dedicated prompt: it explicitly
    # tells the model not to invent confident-sounding specifics it doesn't
    # actually have - generate()'s prompt has no such instruction, since
    # it's answering a direct description, not inferring from possibly-thin
    # knowledge of a specific song.
    assert "do not invent" in song_system.lower()
    assert "do not invent" not in generate_system.lower()


def test_system_prompt_includes_few_shot_examples_derived_from_descriptor_from_text():
    # Regression test for the "fine-tuning shelved -> few-shot prompting
    # instead" pivot (docs/ROADMAP.md M6/M7 log). These example values are
    # not hand-typed - they're descriptor_from_text()'s real output for
    # each example prompt, so if the keyword-mapping logic ever changes,
    # this test will catch the examples going stale/inconsistent with the
    # deterministic baseline they're supposed to illustrate.
    from app.core.tone_descriptor import descriptor_from_text

    clean_jazz = descriptor_from_text("clean jazz tone")
    assert clean_jazz.gain_amount == 0.0
    assert clean_jazz.brightness == 0.3
    assert '"clean jazz tone"' in _SYSTEM_PROMPT
    assert '"gain_amount": 0.0, "brightness": 0.3' in _SYSTEM_PROMPT

    assert "Cliffs of Dover by Eric Johnson" in _SONG_INFERENCE_SYSTEM_PROMPT
    assert "Freaking Out the Neighborhood by Mac DeMarco" in _SONG_INFERENCE_SYSTEM_PROMPT


async def test_infer_song_tone_merges_response_onto_keyword_baseline(provider, monkeypatch):
    async def fake_chat(*, system, user):
        return json.dumps({"modulation_type": "chorus", "modulation_amount": 0.7, "gain_amount": 0.05})

    monkeypatch.setattr(provider, "_chat", fake_chat)
    preset = await provider.infer_song_tone("Freaking Out the Neighborhood by Mac DeMarco")

    modulation = next((m for m in preset.chain if m.module_id == "modulation.v1"), None)
    assert modulation is not None
    assert modulation.parameters["type"] == 0.0  # Chorus


async def test_infer_song_tone_raises_provider_unavailable_after_exhausting_retries(provider, monkeypatch):
    async def fake_chat(*, system, user):
        return "not json at all, sorry"

    monkeypatch.setattr(provider, "_chat", fake_chat)
    with pytest.raises(ProviderUnavailableError):
        await provider.infer_song_tone("anything")


def _sample_preset() -> PresetDTO:
    return PresetDTO(
        id="p1",
        name="Test Preset",
        chain=[
            ModuleStateDTO(moduleId="drive.v1", bypassed=False, parameters={"gain_db": 10.0, "tone": 0.5, "level_db": -3.0}),
            ModuleStateDTO(moduleId="reverb.v1", bypassed=False, parameters={"character": 0.0, "size": 0.5, "damping": 0.5, "predelay_ms": 10.0, "mix": 0.1}),
        ],
        aiProvenance=AiProvenanceDTO(provider="mock", prompt="x", matchType="generated", confidence=1.0, disclaimer="d"),
    )


async def test_modify_applies_a_well_formed_patch_from_the_model(provider, monkeypatch):
    async def fake_chat(*, system, user):
        return json.dumps({"patch": {"drive.v1": {"gain_db": 30.0}}, "unmet": ""})

    monkeypatch.setattr(provider, "_chat", fake_chat)
    modified = await provider.modify(_sample_preset(), "more gain")

    drive = next(m for m in modified.chain if m.module_id == "drive.v1")
    assert drive.parameters["gain_db"] == 30.0
    assert modified.ai_provenance.provider == "ollama:test-model"
    assert modified.ai_provenance.match_type == "modified"


async def test_modify_patch_cannot_invent_a_new_parameter_name(provider, monkeypatch):
    async def fake_chat(*, system, user):
        # "made_up_param" doesn't exist on drive.v1 - must be silently
        # dropped, not written into the preset, even though "gain_db" (a
        # real one) in the same patch should still apply.
        return json.dumps({"patch": {"drive.v1": {"made_up_param": 99.0, "gain_db": 22.0}}, "unmet": ""})

    monkeypatch.setattr(provider, "_chat", fake_chat)
    modified = await provider.modify(_sample_preset(), "more gain")

    drive = next(m for m in modified.chain if m.module_id == "drive.v1")
    assert drive.parameters["gain_db"] == 22.0
    assert "made_up_param" not in drive.parameters


async def test_modify_falls_back_to_mock_when_patch_is_empty(provider, monkeypatch):
    async def fake_chat(*, system, user):
        return json.dumps({"patch": {}, "unmet": "nothing matched"})

    monkeypatch.setattr(provider, "_chat", fake_chat)
    modified = await provider.modify(_sample_preset(), "do something unrelated")

    # An unusable patch must not be silently accepted as "the instruction
    # was honored" - falls back to the mock heuristic instead.
    assert modified.ai_provenance.provider == "mock"


async def test_modify_falls_back_to_mock_when_ollama_is_unreachable(provider, monkeypatch):
    async def raising_chat(*, system, user):
        raise ProviderUnavailableError("simulated: Ollama offline")

    monkeypatch.setattr(provider, "_chat", raising_chat)
    modified = await provider.modify(_sample_preset(), "more gain")

    assert modified.ai_provenance.provider == "mock"


async def test_is_available_is_false_when_nothing_is_listening_on_the_configured_port():
    # No mocking here on purpose: this sandbox (and CI) genuinely has
    # nothing listening on the default Ollama port, so this exercises the
    # real "offline degrades gracefully instead of raising" path end to end.
    provider = OllamaProvider(base_url="http://127.0.0.1:1")  # port 1: guaranteed nothing is listening
    assert await provider.is_available() is False
