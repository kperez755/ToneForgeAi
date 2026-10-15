"""Curated song -> tone descriptor database.

This is a deliberately small seed set for the MVP (docs/ROADMAP.md M7 grows
this substantially and adds a feedback loop). Every entry's descriptor is a
best-effort, ear-informed approximation - never claimed as an exact
recreation of the original recording. See app/song_reference/lookup.py for
how a query is matched against this table, and app/schemas/preset.py /
AiProvenanceDTO for how the "inspired by" disclaimer is enforced downstream.
"""

from __future__ import annotations

from dataclasses import dataclass

from app.core.tone_descriptor import ToneDescriptor


@dataclass(frozen=True)
class SongReference:
    title: str
    artist: str
    tags: list[str]
    descriptor: ToneDescriptor


CURATED_SONGS: list[SongReference] = [
    SongReference(
        title="Cliffs of Dover",
        artist="Eric Johnson",
        tags=["lead", "bright", "compressed", "instrumental"],
        descriptor=ToneDescriptor(
            gain_amount=0.55,
            brightness=0.75,
            compression_amount=0.6,
            delay_amount=0.15,
            reverb_amount=0.2,
            modulation_amount=0.0,
            noise_gate_aggressiveness=0.4,
        ),
    ),
    SongReference(
        title="Sultans of Swing",
        artist="Dire Straits",
        tags=["clean", "bright", "fingerstyle"],
        descriptor=ToneDescriptor(
            gain_amount=0.1,
            brightness=0.65,
            compression_amount=0.35,
            delay_amount=0.05,
            reverb_amount=0.15,
            modulation_amount=0.0,
            noise_gate_aggressiveness=0.2,
        ),
    ),
    SongReference(
        title="Money for Nothing",
        artist="Dire Straits",
        tags=["crunch", "bright", "riff"],
        descriptor=ToneDescriptor(
            gain_amount=0.3,
            brightness=0.6,
            compression_amount=0.3,
            delay_amount=0.0,
            reverb_amount=0.1,
            modulation_amount=0.0,
            noise_gate_aggressiveness=0.25,
        ),
    ),
    SongReference(
        title="Comfortably Numb (solo)",
        artist="Pink Floyd",
        tags=["lead", "sustain", "emotive"],
        descriptor=ToneDescriptor(
            gain_amount=0.45,
            brightness=0.4,
            compression_amount=0.5,
            delay_amount=0.2,
            reverb_amount=0.35,
            modulation_amount=0.0,
            noise_gate_aggressiveness=0.3,
        ),
    ),
    SongReference(
        title="Slow Dancing in a Burning Room",
        artist="John Mayer",
        tags=["clean", "warm", "expressive"],
        descriptor=ToneDescriptor(
            gain_amount=0.35,
            brightness=0.45,
            compression_amount=0.4,
            delay_amount=0.05,
            reverb_amount=0.25,
            modulation_amount=0.0,
            noise_gate_aggressiveness=0.25,
        ),
    ),
    SongReference(
        title="Texas Flood",
        artist="Stevie Ray Vaughan",
        tags=["blues", "warm", "dynamic"],
        descriptor=ToneDescriptor(
            gain_amount=0.4,
            brightness=0.35,
            compression_amount=0.2,
            delay_amount=0.05,
            reverb_amount=0.2,
            modulation_amount=0.0,
            noise_gate_aggressiveness=0.2,
        ),
    ),
    # Added after the OllamaProvider-inferred guess for this song came back
    # noticeably off (no modulation.v1 at all, generic mid-gain rock chain) -
    # a real example of the inferred path's actual limits (see
    # docs/ROADMAP.md's M6 log and lookup.py's docstring): a 4B local model
    # doesn't reliably know a moderately-known indie artist's actual gear.
    # Sourced from real gear-rundown coverage rather than guessed: DeMarco's
    # signature "warbly, dreamy" tone comes from CLEAN small tube amps
    # (Fender Vibro-Champ in studio; Twin Reverb/Roland JC-120 live) - no
    # meaningful gain/distortion - run through a Boss chorus pedal (CE-3
    # studio era, CE-5 live) driven hard, an Electro-Harmonix Holy Grail
    # Reverb, and slapback delay, both used sparingly. The chorus is the
    # defining element here, not an afterthought - hence modulation_amount
    # well above every other curated entry's (all currently 0.0).
    SongReference(
        title="Freaking Out the Neighborhood",
        artist="Mac DeMarco",
        tags=["chorus", "warbly", "clean", "warm", "dreamy"],
        descriptor=ToneDescriptor(
            gain_amount=0.08,
            brightness=0.4,
            compression_amount=0.2,
            delay_amount=0.18,
            reverb_amount=0.22,
            modulation_amount=0.65,
            modulation_type=0,  # Chorus - Boss CE-3/CE-5, not flanger/phaser/tremolo
            noise_gate_aggressiveness=0.15,
        ),
    ),
]
