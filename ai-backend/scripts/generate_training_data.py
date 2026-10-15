"""Synthetic training data generator for fine-tuning a local guitar-tone LLM.

Generates (system, user, assistant) chat-format training examples in EXACTLY
the format OllamaProvider uses at real inference time, so a QLoRA fine-tune
on this JSONL learns to reproduce the same descriptor-JSON contract
_merge_descriptor() expects - not some other schema that would need a second
translation layer. See app/providers/ollama_provider.py for _SYSTEM_PROMPT /
_SONG_INFERENCE_SYSTEM_PROMPT and the JSON shape both require.

Two families of examples:

1. "generate" examples - natural-language tone descriptions -> descriptor
   JSON, using _SYSTEM_PROMPT as the system message. Ground truth comes from
   descriptor_from_text() (the same deterministic keyword mapper MockProvider
   uses, and the baseline every real OllamaProvider response is merged onto)
   applied to synthetic sentences built from tone_descriptor.py's own
   _KEYWORD_RULES vocabulary - so the target JSON is always internally
   consistent with what the prompt text actually says. Sentence *templates*
   vary the phrasing around those keywords so the model learns to respond to
   natural language, not just to the literal presence of a magic word.

2. "song inference" examples - "<title> by <artist>"-style queries ->
   descriptor JSON, using _SONG_INFERENCE_SYSTEM_PROMPT as the system
   message. Ground truth comes directly from the hand-curated
   SongReference.descriptor entries in app/song_reference/db.py - real,
   ear-informed data, not the keyword baseline - because the whole point of
   fine-tuning at all (see docs/ROADMAP.md M6 log: the Mac DeMarco test) is
   to bake this curated knowledge, and the *pattern* of honest genre-based
   reasoning it represents, into the model's own weights, so obscure songs
   that AREN'T in the curated table also get a better-than-generic guess.

This script is pure Python, no network/GPU required, and is meant to be run
and spot-checked in any environment before the resulting JSONL is handed off
to Unsloth/SFTTrainer on the RTX 3070 box (see Task #24 - the actual
fine-tuning script - which consumes this file's output).

Run:
    cd ai-backend
    python3 scripts/generate_training_data.py

Output:
    ai-backend/training_data/tone_training_data.jsonl
"""

from __future__ import annotations

import itertools
import json
import random
import sys
from dataclasses import asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.core.tone_descriptor import ToneDescriptor, descriptor_from_text  # noqa: E402
from app.providers.ollama_provider import _SONG_INFERENCE_SYSTEM_PROMPT, _SYSTEM_PROMPT  # noqa: E402
from app.song_reference.db import CURATED_SONGS  # noqa: E402

_SEED = 20260729  # today's date, for reproducibility - not a magic number
_OUTPUT_PATH = Path(__file__).resolve().parent.parent / "training_data" / "tone_training_data.jsonl"

_REVERB_CHARACTER_NAMES = {0: "hall", 1: "plate", 2: "spring", 3: "room"}
_MODULATION_TYPE_NAMES = {0: "chorus", 1: "flanger", 2: "phaser", 3: "tremolo"}


def _descriptor_to_response_json(descriptor: ToneDescriptor, tags_override: list[str] | None = None) -> dict:
    """Mirror the exact JSON shape _SYSTEM_PROMPT / _SONG_INFERENCE_SYSTEM_PROMPT
    ask the model for (see ollama_provider.py) - NOT ToneDescriptor's raw
    field representation (reverb_character/modulation_type are ints there,
    but the model is asked for - and _merge_descriptor() parses - short
    strings, since a local model reliably emits "spring" far more often than
    it correctly recalls that spring is index 2 of 4).

    tags_override: SongReference entries in db.py don't populate
    descriptor.summary_tags (that field only gets filled in by
    descriptor_from_text()'s keyword matching) - every curated song would
    otherwise train the model to respond with the useless placeholder
    ["general"] instead of the real, hand-picked tags already sitting right
    there on SongReference.tags. Pass those through here instead.
    """
    tags = tags_override if tags_override is not None else descriptor.summary_tags
    return {
        "gain_amount": round(descriptor.gain_amount, 3),
        "brightness": round(descriptor.brightness, 3),
        "compression_amount": round(descriptor.compression_amount, 3),
        "delay_amount": round(descriptor.delay_amount, 3),
        "reverb_amount": round(descriptor.reverb_amount, 3),
        "modulation_amount": round(descriptor.modulation_amount, 3),
        "noise_gate_aggressiveness": round(descriptor.noise_gate_aggressiveness, 3),
        "reverb_character": _REVERB_CHARACTER_NAMES[descriptor.reverb_character],
        "modulation_type": _MODULATION_TYPE_NAMES[descriptor.modulation_type],
        "distortion_style": descriptor.distortion_style,
        "summary_tags": (tags[:5] or ["general"]),
    }


# ---------------------------------------------------------------------------
# Family 1: "generate" examples
# ---------------------------------------------------------------------------

# Grouped so combos stay semantically sane (e.g. we never combine "clean"
# with "metal" in the same sentence) - each group is a set of mutually
# substitutable descriptors pulled straight from _KEYWORD_RULES's own keys,
# so descriptor_from_text() run on the final sentence always matches.
_GAIN_WORDS = ["clean", "crunch", "overdrive", "distorted", "heavy", "metal", "djent", "high gain"]
_BRIGHTNESS_WORDS = ["bright", "warm", "dark"]
_GENRE_WORDS = ["jazz", "ambient", "shoegaze", "funk", "neo soul"]
_SPACE_WORDS = ["delay", "echo", "reverb", "spring", "plate reverb", "hall reverb", "cathedral", "small room"]
_MOD_WORDS = ["chorus", "flanger", "phaser", "tremolo"]
_DYNAMICS_WORDS = ["compressed", "squashed", "dynamic", "tight"]
_LEAD_WORDS = ["lead", "solo"]

_ALL_GROUPS = [
    _GAIN_WORDS,
    _BRIGHTNESS_WORDS,
    _GENRE_WORDS,
    _SPACE_WORDS,
    _MOD_WORDS,
    _DYNAMICS_WORDS,
    _LEAD_WORDS,
]

# Sentence templates - {words} is replaced with a natural joining of
# whichever keyword slots got filled for a given combo. Varying the carrier
# sentence (not just the keyword list) is what should let a fine-tuned model
# generalize past exact keyword-matching, unlike descriptor_from_text() itself.
_TEMPLATES = [
    "Give me a {words} tone.",
    "I want a {words} guitar sound.",
    "Create a {words} tone for me.",
    "Make it {words}.",
    "Can you dial in something {words}?",
    "I'm looking for a {words} sound.",
    "Set up a {words} patch.",
    "I need a {words} tone for a recording.",
    "Something {words}, please.",
    "Build me a {words} preset.",
]


def _join_words(words: list[str]) -> str:
    if len(words) == 1:
        return words[0]
    if len(words) == 2:
        return f"{words[0]} and {words[1]}"
    return f"{', '.join(words[:-1])}, and {words[-1]}"


def _generate_prompts(rng: random.Random, count: int) -> list[str]:
    prompts: set[str] = set()
    attempts = 0
    # Cap attempts generously - some (words, template) combos may collide
    # after lowercasing, so we overshoot count rather than risk an infinite
    # loop on a saturated combination space.
    while len(prompts) < count and attempts < count * 20:
        attempts += 1
        combo_size = rng.choice([1, 1, 2, 2, 2, 3, 3])
        groups = rng.sample(_ALL_GROUPS, k=min(combo_size, len(_ALL_GROUPS)))
        words = [rng.choice(group) for group in groups]
        template = rng.choice(_TEMPLATES)
        prompt = template.format(words=_join_words(words))
        prompts.add(prompt)
    return sorted(prompts)


def _build_generate_examples(rng: random.Random, count: int) -> list[dict]:
    examples = []
    for prompt in _generate_prompts(rng, count):
        descriptor = descriptor_from_text(prompt)
        examples.append(
            {
                "messages": [
                    {"role": "system", "content": _SYSTEM_PROMPT},
                    {"role": "user", "content": prompt},
                    {"role": "assistant", "content": json.dumps(_descriptor_to_response_json(descriptor))},
                ]
            }
        )
    return examples


# ---------------------------------------------------------------------------
# Family 2: song inference examples (curated ground truth)
# ---------------------------------------------------------------------------

_SONG_QUERY_TEMPLATES = [
    "{title} by {artist}",
    "{artist} - {title}",
    "the tone from {title} by {artist}",
    "{artist}'s {title}",
    "guitar tone for {title} ({artist})",
    "how does {artist} get their tone on {title}",
]


def _build_song_examples() -> list[dict]:
    examples = []
    for song in CURATED_SONGS:
        response_json = _descriptor_to_response_json(song.descriptor, tags_override=song.tags)
        for template in _SONG_QUERY_TEMPLATES:
            query = template.format(title=song.title, artist=song.artist)
            examples.append(
                {
                    "messages": [
                        {"role": "system", "content": _SONG_INFERENCE_SYSTEM_PROMPT},
                        {"role": "user", "content": query},
                        {"role": "assistant", "content": json.dumps(response_json)},
                    ]
                }
            )
    return examples


# ---------------------------------------------------------------------------


def main() -> None:
    rng = random.Random(_SEED)
    generate_examples = _build_generate_examples(rng, count=600)
    song_examples = _build_song_examples()

    all_examples = generate_examples + song_examples
    rng.shuffle(all_examples)

    _OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with _OUTPUT_PATH.open("w", encoding="utf-8") as f:
        for example in all_examples:
            f.write(json.dumps(example, ensure_ascii=False) + "\n")

    print(f"Wrote {len(all_examples)} examples ({len(generate_examples)} generate + {len(song_examples)} song) to {_OUTPUT_PATH}")


if __name__ == "__main__":
    main()
