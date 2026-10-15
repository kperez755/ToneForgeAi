"""ToneDescriptor -> PresetDTO expansion.

This is the one place that knows the default signal-chain topology (which
modules, in which order) and how a normalized ToneDescriptor maps onto each
module's actual parameter ranges. Module ids and parameter ids here must
stay in sync with core/include/tfcore/dsp/modules/*.h - see the comment in
app/schemas/preset.py for why that coupling is currently manual.
"""

from __future__ import annotations

import uuid

from app.core.tone_descriptor import ToneDescriptor
from app.schemas.preset import AiProvenanceDTO, ModuleStateDTO, PresetDTO

# Must match toneforge::kNumSlots (plugin/Source/state/ParameterBridge.h).
# This constant exists here - not just in the plugin - because of a real bug
# this fixes: build_default_chain() used to unconditionally emit 10 modules
# (including a limiter.v1 the plugin can't even instantiate yet - see
# ModuleRegistry::create()) while PluginProcessor::applyPreset() maps
# preset.chain[i] onto pedalboard slot i by raw position, dropping anything
# past index 7 with no warning. Concretely: every AI-generated preset's
# reverb.v1 and limiter.v1 entries (indices 8 and 9) were silently discarded
# before ever reaching the plugin, regardless of how much reverb the prompt
# asked for. See docs/ARCHITECTURE.md 5.4 and docs/ROADMAP.md's M6 log entry
# for how this was found and verified.
_NUM_SLOTS = 8

# A module is "wanted" if its driving amount is above this - the same cutoff
# the old code used for bypassing a module it emitted unconditionally. Now
# it decides whether the module is emitted into the chain AT ALL, which is
# what makes the slot-budget logic below meaningful (an unwanted effect
# frees its slot for something else, rather than occupying one while
# bypassed).
_INCLUDE_THRESHOLD = 0.02


def _lerp(lo: float, hi: float, t: float) -> float:
    return lo + (hi - lo) * max(0.0, min(1.0, t))


def _unlerp(lo: float, hi: float, value: float) -> float:
    """Inverse of _lerp: given a value already in [lo, hi], returns the
    0..1 fraction of the way through that range - used by
    descriptor_from_preset() below to reconstruct a ToneDescriptor's
    amounts from an already-built preset's real parameter values.
    """
    if hi == lo:
        return 0.0
    return max(0.0, min(1.0, (value - lo) / (hi - lo)))


def _noise_gate(descriptor: ToneDescriptor) -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="noise_gate.v1",
        bypassed=False,
        parameters={
            "threshold_db": _lerp(-60.0, -30.0, descriptor.noise_gate_aggressiveness),
            "attack_ms": 2.0,
            "hold_ms": _lerp(100.0, 20.0, descriptor.noise_gate_aggressiveness),
            "release_ms": _lerp(200.0, 60.0, descriptor.noise_gate_aggressiveness),
        },
    )


def _compressor(descriptor: ToneDescriptor) -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="compressor.v1",
        bypassed=False,  # only ever appended when already deemed "wanted" - see build_default_chain()
        parameters={
            "ratio": _lerp(1.5, 8.0, descriptor.compression_amount),
            "threshold_db": _lerp(-6.0, -24.0, descriptor.compression_amount),
            "attack_ms": 5.0,
            "release_ms": 80.0,
            "makeup_db": _lerp(0.0, 6.0, descriptor.compression_amount),
        },
    )


def _drive_or_distortion(descriptor: ToneDescriptor) -> ModuleStateDTO:
    """Picks drive.v1 (Overdrive - transparent, asymmetric soft clip) or
    distortion.v1 (high-gain - symmetric, earlier-clipping, cascaded twice,
    plus a presence control) based on descriptor.distortion_style, rather
    than always emitting drive.v1 and just turning its gain knob up. A
    "heavy"/"metal"/"djent" prompt now gets a genuinely different clip
    character, not a bigger number on the same clip curve - see
    core/include/tfcore/dsp/modules/Distortion.h's class comment for what
    actually differs between the two DSP-wise.

    Always present (this is one of the tonal-spine slots - see
    build_default_chain()), but left bypassed for a near-fully-clean prompt
    exactly like before.
    """
    bypassed = descriptor.gain_amount <= _INCLUDE_THRESHOLD

    if descriptor.distortion_style == "distortion":
        return ModuleStateDTO(
            moduleId="distortion.v1",
            bypassed=bypassed,
            parameters={
                # A prompt that explicitly asked for "distortion" style
                # implies real gain even at ToneDescriptor's moderate 0.3
                # default - a barely-driven high-gain pedal doesn't do what
                # the word promises. 15 dB floor vs drive.v1's 0 dB floor
                # reflects that; 60 dB ceiling matches the module's real
                # range (double Drive's, by design - see Distortion.h).
                "gain_db": _lerp(15.0, 60.0, descriptor.gain_amount),
                "tone": _lerp(0.3, 0.9, descriptor.brightness),
                "presence": _lerp(0.2, 0.7, descriptor.brightness),
                "level_db": -6.0,
            },
        )

    return ModuleStateDTO(
        moduleId="drive.v1",
        bypassed=bypassed,
        parameters={
            "gain_db": _lerp(0.0, 40.0, descriptor.gain_amount),
            "tone": _lerp(0.3, 0.9, descriptor.brightness),
            "level_db": -3.0,
        },
    )


def _amp(descriptor: ToneDescriptor) -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="amp.v1",
        bypassed=False,
        parameters={
            "gain": _lerp(0.2, 0.85, descriptor.gain_amount),
            "bass": _lerp(0.6, 0.4, descriptor.brightness),
            "mid": 0.5,
            "treble": _lerp(0.35, 0.75, descriptor.brightness),
            "presence": _lerp(0.3, 0.7, descriptor.brightness),
        },
        resourceRef="amp_model.generic_tube_v1",
    )


def _cab() -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="cab.v1",
        bypassed=False,
        parameters={"mix": 1.0},
        resourceRef="cab_ir.generic_4x12_v1",
    )


def _eq(descriptor: ToneDescriptor) -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="eq.v1",
        bypassed=False,
        parameters={
            "low_gain_db": _lerp(1.0, -2.0, descriptor.brightness),
            "mid_gain_db": 0.0,
            "high_gain_db": _lerp(-2.0, 3.0, descriptor.brightness),
        },
    )


def _modulation(descriptor: ToneDescriptor) -> ModuleStateDTO:
    mod_type = descriptor.modulation_type
    # Flanger(1)/Phaser(2) need real feedback/regeneration to sound like
    # themselves - Chorus(0) and Tremolo(3) ignore the feedback parameter
    # entirely (see core/include/tfcore/dsp/modules/Modulation.h), so
    # leaving it at 0 for those two is correct, not an oversight.
    feedback = 0.0 if mod_type in (0, 3) else _lerp(0.25, 0.55, descriptor.modulation_amount)
    return ModuleStateDTO(
        moduleId="modulation.v1",
        bypassed=False,  # only ever appended when already deemed "wanted"
        parameters={
            "type": float(mod_type),
            "rate_hz": 4.0 if mod_type == 3 else 1.2,  # tremolo reads naturally faster than chorus/flanger/phaser
            "depth": _lerp(0.2, 0.75, descriptor.modulation_amount),
            "feedback": feedback,
            "mix": _lerp(0.2, 0.55, descriptor.modulation_amount),
        },
    )


def _delay(descriptor: ToneDescriptor) -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="delay.v1",
        bypassed=False,  # only ever appended when already deemed "wanted"
        parameters={
            "time_ms": 380.0,
            "feedback": _lerp(0.15, 0.45, descriptor.delay_amount),
            "tone": 0.6,
            "mix": _lerp(0.1, 0.4, descriptor.delay_amount),
        },
    )


def _reverb(descriptor: ToneDescriptor) -> ModuleStateDTO:
    return ModuleStateDTO(
        moduleId="reverb.v1",
        bypassed=False,  # only ever appended when already deemed "wanted"
        parameters={
            "character": float(descriptor.reverb_character),
            "size": _lerp(0.2, 0.8, descriptor.reverb_amount),
            "damping": _lerp(0.5, 0.35, descriptor.reverb_amount),
            "predelay_ms": _lerp(5.0, 20.0, descriptor.reverb_amount),
            "mix": _lerp(0.08, 0.4, descriptor.reverb_amount),
        },
    )


def build_default_chain(descriptor: ToneDescriptor) -> list[ModuleStateDTO]:
    """Builds a signal chain that fits within the pedalboard's _NUM_SLOTS (8)
    slots, in two tiers:

    1. Tonal spine (always present, 5-6 slots): Noise Gate, an optional
       Compressor, Overdrive-or-Distortion (see _drive_or_distortion),
       Amp, Cab, Eq. These are the modules that give the signal its basic
       amp-like character - always included, same as before this change
       (Compressor is the one spine module that's conditional, matching its
       existing "usually on a little, sometimes not" role).
    2. Back-of-chain effects (compete for whatever slots are left):
       Modulation, Delay, Reverb. Each is only included if its descriptor
       amount clears _INCLUDE_THRESHOLD - and if more of them are wanted
       than there's room for, the highest-scoring ones win and the rest are
       simply left off the board, in canonical signal-chain order
       (modulation -> delay -> reverb) among whichever were chosen. This is
       a real, honest trade-off a physical 8-slot pedalboard has too - it's
       not a workaround, it's what "AI-generated pedalboard" should
       actually mean. Limiter is not emitted at all: core/ModuleRegistry
       doesn't implement limiter.v1 yet (docs/ROADMAP.md), so it always
       resolved to an empty "None" slot anyway - better to spend that slot
       budget on an effect that can actually play.
    """
    chain: list[ModuleStateDTO] = [_noise_gate(descriptor)]

    include_compressor = descriptor.compression_amount > _INCLUDE_THRESHOLD
    if include_compressor:
        chain.append(_compressor(descriptor))

    chain.append(_drive_or_distortion(descriptor))
    chain.append(_amp(descriptor))
    chain.append(_cab())
    chain.append(_eq(descriptor))

    remaining_budget = max(0, _NUM_SLOTS - len(chain))

    candidates = (
        ("modulation", descriptor.modulation_amount, _modulation),
        ("delay", descriptor.delay_amount, _delay),
        ("reverb", descriptor.reverb_amount, _reverb),
    )
    wanted = [c for c in candidates if c[1] > _INCLUDE_THRESHOLD]
    wanted.sort(key=lambda c: c[1], reverse=True)
    chosen_names = {c[0] for c in wanted[:remaining_budget]}

    # Re-walk `candidates` (not `wanted`) so whichever survive the budget
    # cut still land in canonical signal-chain order, not score order.
    for name, _score, factory in candidates:
        if name in chosen_names:
            chain.append(factory(descriptor))

    return chain


def descriptor_to_preset(
    descriptor: ToneDescriptor,
    *,
    name: str,
    prompt: str,
    provider_name: str,
    match_type: str = "generated",
    confidence: float = 1.0,
    disclaimer: str = "AI-generated starting point - tweak to taste.",
) -> PresetDTO:
    return PresetDTO(
        id=str(uuid.uuid4()),
        name=name,
        description=f"Generated from: \"{prompt}\"",
        author="ToneForge AI",
        tags=descriptor.summary_tags,
        chain=build_default_chain(descriptor),
        aiProvenance=AiProvenanceDTO(
            provider=provider_name,
            prompt=prompt,
            matchType=match_type,
            confidence=confidence,
            disclaimer=disclaimer,
        ),
    )


def descriptor_from_preset(preset: PresetDTO) -> ToneDescriptor:
    """Inverse of build_default_chain(): reconstructs an approximate
    ToneDescriptor from an already-built PresetDTO's real module
    parameters, reading every quality the chain actually encodes rather
    than just one.

    Built for app/song_reference/lookup.py's "inferred" path (asking a
    provider to infer a song's tone from its own knowledge, then handing
    the result back through the shared descriptor pipeline): that code
    used to only pull a single gain hint out of the generated preset and
    discard everything else the provider actually inferred (brightness,
    compression, reverb character, modulation type, distortion style),
    falling back to a keyword match on the song's title/artist for every
    other field - which usually matches nothing at all, since song titles
    rarely contain tone-descriptor words. This function is what lets the
    provider's full response survive being funneled through generate() and
    back, instead of collapsing to "generic descriptor plus one gain
    number." See docs/ROADMAP.md's M6 log for how this gap was found.

    Not a perfect round-trip - fixed lerp ranges lose some precision, and a
    module's absence from the chain (because build_default_chain()'s
    8-slot budget didn't have room for it, not because the amount was
    truly zero) is read back as "not wanted" rather than reconstructing
    whatever pre-budget-cut amount it might have had. Close enough to be a
    real improvement over the keyword-only fallback it replaces.
    """
    descriptor = ToneDescriptor()
    modules = {m.module_id: m for m in preset.chain}

    if (drive := modules.get("drive.v1")) is not None:
        descriptor.gain_amount = _unlerp(0.0, 40.0, drive.parameters.get("gain_db", 0.0))
        descriptor.brightness = _unlerp(0.3, 0.9, drive.parameters.get("tone", 0.5))
        descriptor.distortion_style = "overdrive"
    elif (distortion := modules.get("distortion.v1")) is not None:
        descriptor.gain_amount = _unlerp(15.0, 60.0, distortion.parameters.get("gain_db", 15.0))
        descriptor.brightness = _unlerp(0.3, 0.9, distortion.parameters.get("tone", 0.5))
        descriptor.distortion_style = "distortion"

    if (compressor := modules.get("compressor.v1")) is not None:
        descriptor.compression_amount = _unlerp(1.5, 8.0, compressor.parameters.get("ratio", 1.5))
    else:
        descriptor.compression_amount = 0.0

    if (delay := modules.get("delay.v1")) is not None:
        descriptor.delay_amount = _unlerp(0.1, 0.4, delay.parameters.get("mix", 0.1))
    else:
        descriptor.delay_amount = 0.0

    if (reverb := modules.get("reverb.v1")) is not None:
        descriptor.reverb_amount = _unlerp(0.08, 0.4, reverb.parameters.get("mix", 0.08))
        descriptor.reverb_character = int(round(max(0.0, min(3.0, reverb.parameters.get("character", 0.0)))))
    else:
        descriptor.reverb_amount = 0.0

    if (modulation := modules.get("modulation.v1")) is not None:
        descriptor.modulation_amount = _unlerp(0.2, 0.55, modulation.parameters.get("mix", 0.2))
        descriptor.modulation_type = int(round(max(0.0, min(3.0, modulation.parameters.get("type", 0.0)))))
    else:
        descriptor.modulation_amount = 0.0

    if (gate := modules.get("noise_gate.v1")) is not None:
        descriptor.noise_gate_aggressiveness = _unlerp(-60.0, -30.0, gate.parameters.get("threshold_db", -60.0))

    descriptor.summary_tags = list(preset.tags) or ["general"]
    return descriptor
