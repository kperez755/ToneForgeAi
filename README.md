# ToneForge AI

An AI-powered guitar effects suite: describe a tone in plain English (or
reference a song/artist), get a full signal chain — amp, cab, EQ, drive,
delay, reverb, compression, modulation — generated and ready to tweak by
hand. Runs as a VST3 plugin and as a standalone desktop app.

**Status: MVP foundation (Milestone 1 of the roadmap).** Read
`docs/ARCHITECTURE.md` first, then `docs/ROADMAP.md` for what's built vs.
planned. Short version: the DSP core and the AI backend are implemented and
tested; the JUCE plugin UI is scaffolded but not yet built or run anywhere
(needs a real desktop toolchain — see `docs/SETUP_WINDOWS.md`).

## Layout

```
core/         ToneForgeCore - pure C++20 DSP + signal chain + preset model. No JUCE dependency.
plugin/       JUCE VST3 / Standalone wrapper around core. Not yet built/verified.
ai-backend/   Python FastAPI service: language -> preset JSON.
presets/      JSON schema + factory presets.
docs/         Architecture, thread model, DSP, AI, Windows setup, roadmap.
scripts/      Dev-loop helpers (e.g. verify_factory_presets.cpp).
```

## Quick start

**DSP core** (works on any C++20 toolchain, no JUCE required):

```bash
cmake -S . -B build -DTONEFORGE_BUILD_PLUGIN=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

27/27 tests passing as of this milestone, verified additionally under
AddressSanitizer + UndefinedBehaviorSanitizer.

**AI backend:**

```bash
cd ai-backend
pip install -r requirements.txt
python3 -m pytest -q        # 25/25 passing, no Ollama required
uvicorn app.main:app --host 127.0.0.1 --port 8765 --reload
```

**Plugin (VST3 + Standalone):** see `docs/SETUP_WINDOWS.md` — requires
JUCE (fetched via CMake `FetchContent`) and a full desktop toolchain, so it
hasn't been built in the environment this scaffold was produced in. That's
flagged explicitly rather than glossed over.

## Why the DSP has zero third-party or JUCE dependencies

So it can be built, unit-tested, and reasoned about anywhere a C++20
compiler exists — including a headless sandbox with no GUI, no DAW, and no
package registry access, which is exactly the environment this milestone
was verified in. See `docs/ARCHITECTURE.md` for the full rationale.
