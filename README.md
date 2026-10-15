# ToneForge AI

A guitar-effects plugin where you describe a tone in plain English, or name a song, and get a full pedalboard you can adjust by hand. It runs as a VST3 plugin and as a standalone desktop app. The language side runs locally through [Ollama](https://ollama.com), so no cloud service is involved.

I built it for my own use, and I plan to release it as open source.

## Status

Working on Windows, and still early.

- The DSP core, the JUCE plugin (VST3 and standalone), and the AI backend all build and run.
- Results are rough approximations of a tone, not recreations. The amp is a simple waveshaper plus tone stack, and the cabinet is an EQ curve rather than impulse-response convolution.
- Tone quality has only been judged by ear on my own machine. There is no formal evaluation.
- Songs outside the small curated table are inferred by a 4B-parameter local model, which does not reliably know obscure songs. Those results are labelled as approximations.
- Not yet built: saving and loading presets from the UI, and a model picker in the plugin (the model is set by an environment variable).
- No license file yet. TODO: choose one before accepting contributions.

## How it works

```
Plugin UI (JUCE)  ──HTTP──>  FastAPI backend  ──>  provider
 AI panel:                    /generate              ├─ Ollama (local model, default gemma3:4b)
 Generate / Modify /          /modify                └─ Mock (deterministic keywords; tests + fallback)
 Explain / Reference Song     /explain
                              /song-reference
                              /health
```

1. The backend turns text into a small **tone descriptor**: gain, brightness, compression, delay, reverb, modulation, noise gate, plus reverb type, modulation type, and overdrive-versus-distortion.
2. One mapping function expands that descriptor into a full preset of pedal parameters. The model never writes raw parameter values, and both providers share this mapping.
3. The plugin loads the preset into its eight pedalboard slots.

If Ollama is unavailable or returns unusable output after retries, the backend falls back to the deterministic provider instead of failing.

**Song references.** A small hand-curated table is matched by fuzzy search. A miss goes to the local model, and the result carries a match type and confidence and is presented as an approximation.

## Layout

```
core/         ToneForgeCore: pure C++20 DSP, signal chain, preset model. No JUCE dependency.
plugin/       JUCE VST3 / Standalone wrapper and the AI panel.
ai-backend/   Python FastAPI service: language -> preset JSON.
presets/      JSON schema and factory presets.
docs/         Architecture, thread model, DSP, AI, Windows setup, roadmap.
scripts/      Dev-loop helpers.
```

The effect modules are noise gate, compressor, overdrive, distortion, amp, cabinet, EQ, modulation (chorus, flanger, phaser, tremolo), delay, and reverb (hall, plate, spring, room).

## Build and run (Windows)

Requires Visual Studio 2022 with the C++ workload, CMake 3.22+, Python 3.10+, and optionally Docker for Ollama. See `docs/SETUP_WINDOWS.md` for details.

**DSP core and tests** (no JUCE needed):

```powershell
cmake -S . -B build -DTONEFORGE_BUILD_PLUGIN=OFF
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

**Plugin** (fetches JUCE with CMake, so the first build is long):

```powershell
cmake -S . -B build -DTONEFORGE_BUILD_PLUGIN=ON
cmake --build build --config Release
```

The standalone app is at `build\plugin\ToneForgeAI_artefacts\Release\Standalone\ToneForge AI.exe`. The filename contains a space, so call it with `&` and quotes in PowerShell.

**AI backend:**

```powershell
cd ai-backend
python -m venv venv
.\venv\Scripts\Activate.ps1
pip install -r requirements.txt
python -m pytest -q
uvicorn app.main:app --host 127.0.0.1 --port 8765 --reload
```

The plugin expects the backend on port 8765. The backend works without Ollama, using the mock provider.

**Local model (optional):**

```powershell
docker run -d -p 11434:11434 --name ollama ollama/ollama
docker exec -it ollama ollama pull gemma3:4b
```

The default model is `gemma3:4b`. Set `TONEFORGE_OLLAMA_MODEL` to use another (see `ai-backend/app/config.py`).

## Docs

Start with `docs/ARCHITECTURE.md`, then `docs/ROADMAP.md` for what is built and what is planned.

`ai-backend/scripts/` also holds a training-data generator and a fine-tuning script (`docs/FINE_TUNING_WINDOWS.md`). I wrote them while exploring fine-tuning a small model, but never ran a training job, so they are untested.
