"""Direct tests for app/core/mapping.py - the single place that knows the
pedalboard's default signal-chain topology and the ToneDescriptor <->
PresetDTO mapping in both directions. Despite being arguably the most
important file in the backend, it previously had no direct test coverage at
all - only indirect coverage via test_mock_provider.py's higher-level
assertions. These tests exercise build_default_chain() and its inverse,
descriptor_from_preset(), directly.
"""

from __future__ import annotations

from app.core.mapping import build_default_chain, descriptor_from_preset
from app.core.tone_descriptor import ToneDescriptor
from app.schemas.preset import ModuleStateDTO, PresetDTO


def _preset_from_descriptor(descriptor: ToneDescriptor) -> PresetDTO:
    return PresetDTO(id="p", name="n", chain=build_default_chain(descriptor))


def test_descriptor_from_preset_recovers_gain_and_brightness_approximately():
    original = ToneDescriptor(gain_amount=0.7, brightness=0.2)
    preset = _preset_from_descriptor(original)

    recovered = descriptor_from_preset(preset)
    assert abs(recovered.gain_amount - original.gain_amount) < 0.02
    assert abs(recovered.brightness - original.brightness) < 0.02


def test_descriptor_from_preset_recovers_distortion_style():
    overdrive = descriptor_from_preset(_preset_from_descriptor(ToneDescriptor(distortion_style="overdrive")))
    assert overdrive.distortion_style == "overdrive"

    distortion = descriptor_from_preset(_preset_from_descriptor(ToneDescriptor(distortion_style="distortion")))
    assert distortion.distortion_style == "distortion"


def test_descriptor_from_preset_recovers_reverb_character_and_amount():
    original = ToneDescriptor(reverb_amount=0.6, reverb_character=2)
    recovered = descriptor_from_preset(_preset_from_descriptor(original))
    assert recovered.reverb_character == 2
    assert abs(recovered.reverb_amount - 0.6) < 0.02


def test_descriptor_from_preset_recovers_modulation_type_and_amount():
    original = ToneDescriptor(modulation_amount=0.8, modulation_type=3)
    recovered = descriptor_from_preset(_preset_from_descriptor(original))
    assert recovered.modulation_type == 3
    assert abs(recovered.modulation_amount - 0.8) < 0.02


def test_descriptor_from_preset_reads_zero_for_effects_absent_from_the_chain():
    # A descriptor whose modulation/delay amounts are below the "wanted"
    # threshold produces a chain with no modulation.v1/delay.v1 module at
    # all (see build_default_chain()) - the round trip should read that
    # absence back as "not wanted" (0.0), not error or guess a leftover
    # value.
    quiet = ToneDescriptor(modulation_amount=0.0, delay_amount=0.0)
    preset = _preset_from_descriptor(quiet)
    assert not any(m.module_id == "modulation.v1" for m in preset.chain)
    assert not any(m.module_id == "delay.v1" for m in preset.chain)

    recovered = descriptor_from_preset(preset)
    assert recovered.modulation_amount == 0.0
    assert recovered.delay_amount == 0.0


def test_descriptor_from_preset_handles_a_preset_with_no_gain_stage_module():
    # A hand-built or hypothetical preset that doesn't include drive.v1 or
    # distortion.v1 at all must not raise - falls back to the
    # ToneDescriptor() defaults for gain/brightness/distortion_style.
    preset = PresetDTO(
        id="p",
        name="n",
        chain=[ModuleStateDTO(moduleId="noise_gate.v1", parameters={"threshold_db": -45.0})],
    )
    recovered = descriptor_from_preset(preset)
    assert recovered.distortion_style == "overdrive"  # ToneDescriptor()'s default, untouched


def test_build_default_chain_never_exceeds_the_slot_budget_across_extreme_descriptors():
    extreme = ToneDescriptor(
        gain_amount=1.0,
        brightness=1.0,
        compression_amount=1.0,
        delay_amount=1.0,
        reverb_amount=1.0,
        modulation_amount=1.0,
        noise_gate_aggressiveness=1.0,
    )
    chain = build_default_chain(extreme)
    assert len(chain) <= 8
