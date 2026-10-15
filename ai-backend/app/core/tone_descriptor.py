"""ToneDescriptor: the intermediate representation between "language" and
"parameter values".

Both MockProvider (keyword heuristics) and, later, OllamaProvider's JSON
output are normalized into a ToneDescriptor before being expanded into a
full PresetDTO by `descriptor_to_preset()`. This indirection means the
gain/brightness/compression/etc -> actual module parameter mapping lives in
exactly one place, shared by every provider, instead of being duplicated
per-provider (and drifting).
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class ToneDescriptor:
    # All fields are 0..1 normalized "how much of this quality" scores,
    # except gain_amount which maps directly to drive gain_db range.
    gain_amount: float = 0.3          # 0 = clean, 1 = max saturation
    brightness: float = 0.5           # 0 = dark/warm, 1 = bright/glassy
    compression_amount: float = 0.3   # 0 = uncompressed/dynamic, 1 = squashed
    delay_amount: float = 0.0         # 0 = no delay, 1 = prominent/long
    reverb_amount: float = 0.15       # 0 = dry, 1 = washy/ambient
    modulation_amount: float = 0.0    # 0 = none, 1 = strong chorus/phaser character
    noise_gate_aggressiveness: float = 0.3  # 0 = permissive, 1 = tight/aggressive

    # Categorical picks, not 0..1 amounts - these choose *which* module
    # variant or which discrete character/type a module should use, not how
    # much of it. Added for the M6 "flesh out the AI's expressive range"
    # pass: previously every prompt got the same fixed module list with only
    # continuous knobs to tune, so "spring reverb" and "hall reverb" (or
    # "tremolo" and "phaser") produced an identical signal chain - only the
    # *amount* of a generic reverb/modulation could change, never which one.
    # See core/include/tfcore/dsp/modules/Reverb.h and Modulation.h for what
    # each index actually sounds like.
    reverb_character: int = 0    # 0=Hall 1=Plate 2=Spring 3=Room
    modulation_type: int = 0     # 0=Chorus 1=Flanger 2=Phaser 3=Tremolo
    distortion_style: str = "overdrive"  # "overdrive" (drive.v1) | "distortion" (distortion.v1)

    summary_tags: list[str] = field(default_factory=list)


def _clamp01(x: float) -> float:
    return max(0.0, min(1.0, x))


# Keyword -> ToneDescriptor field adjustments. This is intentionally a small,
# readable table for the MVP (docs/ROADMAP.md M6 replaces the *generation*
# path with a real LLM; this table remains useful as the deterministic
# MockProvider used in tests/CI and as an offline fallback, and as the
# baseline every OllamaProvider response is merged onto - see
# ollama_provider.py's generate()).
#
# Values are normally a *delta* applied to the matching numeric field's
# current value (clamped to [0,1] after). The three categorical fields
# (reverb_character, modulation_type, distortion_style - see ToneDescriptor)
# are the exception: a value there is a direct *assignment*, not a delta,
# since "which reverb character" isn't a quantity that accumulates. See
# _CATEGORICAL_FIELDS below and descriptor_from_text()'s branch on it.
_KEYWORD_RULES: dict[str, dict[str, float | int | str]] = {
    "clean": {"gain_amount": -0.25, "compression_amount": -0.1},
    "crunch": {"gain_amount": 0.2},
    "overdrive": {"gain_amount": 0.25, "distortion_style": "overdrive"},
    "distortion": {"gain_amount": 0.4, "distortion_style": "distortion"},
    "distorted": {"gain_amount": 0.4, "distortion_style": "distortion"},
    "heavy": {"gain_amount": 0.35, "brightness": -0.1, "distortion_style": "distortion"},
    "metal": {"gain_amount": 0.45, "brightness": -0.05, "distortion_style": "distortion"},
    "djent": {"gain_amount": 0.5, "brightness": -0.1, "compression_amount": 0.2, "distortion_style": "distortion"},
    "high gain": {"gain_amount": 0.4, "distortion_style": "distortion"},
    "high-gain": {"gain_amount": 0.4, "distortion_style": "distortion"},
    "hi-gain": {"gain_amount": 0.4, "distortion_style": "distortion"},
    "lead": {"compression_amount": 0.2, "brightness": 0.15, "gain_amount": 0.15},
    "solo": {"compression_amount": 0.2, "brightness": 0.15, "gain_amount": 0.15},
    "bright": {"brightness": 0.3},
    "warm": {"brightness": -0.25},
    "dark": {"brightness": -0.3},
    "jazz": {"brightness": -0.2, "gain_amount": -0.2, "compression_amount": -0.1},
    "ambient": {"reverb_amount": 0.4, "delay_amount": 0.2, "gain_amount": -0.1},
    "shoegaze": {"reverb_amount": 0.35, "modulation_amount": 0.3, "gain_amount": 0.2},
    "delay": {"delay_amount": 0.35},
    "echo": {"delay_amount": 0.3},
    "reverb": {"reverb_amount": 0.3},
    # Reverb character keywords - each also nudges reverb_amount up a bit so
    # naming a character alone ("give it some spring") does something even
    # without the word "reverb" itself.
    "spring": {"reverb_amount": 0.15, "reverb_character": 2},
    "plate reverb": {"reverb_amount": 0.15, "reverb_character": 1},
    "hall reverb": {"reverb_amount": 0.2, "reverb_character": 0},
    "cathedral": {"reverb_amount": 0.35, "reverb_character": 0},
    "small room": {"reverb_amount": 0.1, "reverb_character": 3},
    # Modulation type keywords - "chorus"/"phaser"/"flanger" already carried
    # a modulation_amount bump; they now also pick the matching module type
    # instead of always defaulting to Chorus regardless of what was asked
    # for. "tremolo" is a new keyword entirely - it wasn't recognized at all
    # before this pass, despite Modulation supporting it as one of its four
    # types.
    "chorus": {"modulation_amount": 0.35, "modulation_type": 0},
    "flanger": {"modulation_amount": 0.3, "modulation_type": 1},
    "flange": {"modulation_amount": 0.3, "modulation_type": 1},
    "jet swoosh": {"modulation_amount": 0.25, "modulation_type": 1},
    "phaser": {"modulation_amount": 0.3, "modulation_type": 2},
    "phase shift": {"modulation_amount": 0.25, "modulation_type": 2},
    "tremolo": {"modulation_amount": 0.3, "modulation_type": 3},
    "funk": {"compression_amount": 0.3, "gain_amount": -0.15, "noise_gate_aggressiveness": 0.2},
    "neo soul": {"brightness": -0.1, "compression_amount": 0.25, "reverb_amount": 0.2},
    "compressed": {"compression_amount": 0.3},
    "squashed": {"compression_amount": 0.4},
    "dynamic": {"compression_amount": -0.25},
    "tight": {"noise_gate_aggressiveness": 0.3},
}

# Fields in ToneDescriptor that are a categorical *choice*, not a 0..1
# amount - see the _KEYWORD_RULES docstring above.
_CATEGORICAL_FIELDS = {"reverb_character", "modulation_type", "distortion_style"}


def descriptor_from_text(text: str) -> ToneDescriptor:
    """Deterministic keyword-based mapping used by MockProvider. Not meant
    to be a sophisticated NLP model - it's the RT-safe, network-free,
    always-correct baseline every other provider is measured against.
    """
    descriptor = ToneDescriptor()
    lowered = text.lower()
    matched_tags: list[str] = []

    for keyword, adjustments in _KEYWORD_RULES.items():
        if keyword in lowered:
            matched_tags.append(keyword)
            for field_name, value in adjustments.items():
                if field_name in _CATEGORICAL_FIELDS:
                    # A later-matching keyword's categorical choice wins
                    # over an earlier one (e.g. "metal phaser tone" ends up
                    # with modulation_type=Phaser, not whatever came before
                    # it in the table) - a direct assignment, not a delta.
                    setattr(descriptor, field_name, value)
                else:
                    current = getattr(descriptor, field_name)
                    setattr(descriptor, field_name, _clamp01(current + value))

    descriptor.summary_tags = matched_tags or ["general"]
    return descriptor
