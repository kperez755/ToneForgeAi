"""Deterministic, network-free provider.

This is not a toy: it is the default provider in tests/CI, and the runtime
fallback (see app/api/routes_generate.py) whenever Ollama is unreachable or
returns something we can't validate. Its job is to guarantee the "Generate
Tone" button always does *something* sensible, even fully offline with no
model installed.
"""

from __future__ import annotations

import copy

from app.core.mapping import descriptor_to_preset
from app.core.tone_descriptor import descriptor_from_text
from app.schemas.preset import AiProvenanceDTO, PresetDTO


class MockProvider:
    name = "mock"

    async def generate(self, prompt: str) -> PresetDTO:
        descriptor = descriptor_from_text(prompt)
        title = _title_from_tags(descriptor.summary_tags)
        return descriptor_to_preset(
            descriptor,
            name=title,
            prompt=prompt,
            provider_name=self.name,
            match_type="generated",
            confidence=1.0,
            disclaimer="AI-generated starting point - tweak to taste.",
        )

    async def infer_song_tone(self, query: str) -> PresetDTO:
        # No reasoning to improve here - the deterministic keyword mapper
        # doesn't "know" anything about specific songs either way, so the
        # honesty/genre-fallback distinction OllamaProvider's version makes
        # doesn't apply. Preserves exactly what app/song_reference/
        # lookup.py used to do inline before infer_song_tone() existed as
        # its own provider method.
        return await self.generate(f"tone reminiscent of the song {query}")

    async def modify(self, preset: PresetDTO, instruction: str) -> PresetDTO:
        # Reuses the same keyword mapping, applied as a delta: re-derive a
        # descriptor from the instruction and nudge the existing preset's
        # gain/eq/reverb/delay modules toward it, rather than regenerating
        # the whole chain from scratch (which would discard the user's
        # manual edits to unrelated modules). Same scope limit as
        # OllamaProvider.modify(): this can only adjust modules already
        # present in preset.chain, never add one that build_default_chain()
        # didn't include (e.g. reverb dropped for a slot-budget reason - see
        # app/core/mapping.py) - "add reverb" on a preset with no reverb.v1
        # module is a Generate Tone job, not a Modify Tone one.
        descriptor = descriptor_from_text(instruction)
        modified = copy.deepcopy(preset)

        for module in modified.chain:
            if module.module_id in ("drive.v1", "distortion.v1") and "gain_db" in module.parameters:
                # drive.v1 and distortion.v1 have different real gain_db
                # ranges (0-40 vs 0-60 - see core/include/tfcore/dsp/modules/
                # Distortion.h) - blend toward each module's own ceiling,
                # not a shared assumed one.
                ceiling = 60.0 if module.module_id == "distortion.v1" else 40.0
                module.parameters["gain_db"] = _blend(module.parameters["gain_db"], 0.0, ceiling, descriptor.gain_amount)
            if module.module_id == "reverb.v1" and "mix" in module.parameters:
                module.parameters["mix"] = _blend(module.parameters["mix"], 0.0, 1.0, descriptor.reverb_amount)
                if descriptor.reverb_amount > 0.4:
                    module.bypassed = False  # instruction clearly wants noticeable reverb - make sure it's actually audible
            if module.module_id == "delay.v1" and "mix" in module.parameters:
                module.parameters["mix"] = _blend(module.parameters["mix"], 0.0, 1.0, descriptor.delay_amount)
                if descriptor.delay_amount > 0.4:
                    module.bypassed = False

        modified.description = f"{modified.description} | Modified: \"{instruction}\""
        modified.ai_provenance = AiProvenanceDTO(
            provider=self.name,
            prompt=instruction,
            matchType="modified",
            confidence=1.0,
            disclaimer="AI-modified from your existing preset - review before saving.",
        )
        return modified

    async def explain(self, preset: PresetDTO) -> str:
        active = [m.module_id for m in preset.chain if not m.bypassed]
        return (
            f"\"{preset.name}\" runs signal through {len(active)} active module(s): "
            f"{', '.join(active) if active else 'none (fully bypassed)'}. "
            "This explanation is generated locally without a language model; "
            "the Ollama-backed explain endpoint (docs/ROADMAP.md M6) will produce "
            "richer, more specific descriptions."
        )

    async def is_available(self) -> bool:
        return True


def _blend(current: float, lo: float, hi: float, target01: float) -> float:
    target = lo + (hi - lo) * max(0.0, min(1.0, target01))
    return (current + target) / 2.0


def _title_from_tags(tags: list[str]) -> str:
    if not tags or tags == ["general"]:
        return "AI Generated Tone"
    return " ".join(word.capitalize() for word in tags[:3]) + " Tone"
