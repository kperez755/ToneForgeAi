# ToneForge AI — Windows Build Setup

This project was scaffolded and its `core` (DSP) and `ai-backend` (Python)
layers were built and tested in a sandboxed Linux environment with no JUCE,
no VST3 SDK, and no DAW. `plugin/` has been structured but **not compiled
anywhere yet**. This doc is what you need to actually build it on Windows.

## Prerequisites

1. **Visual Studio 2022** (Community edition is fine) with the "Desktop
   development with C++" workload — provides the MSVC toolchain CMake will
   use.
2. **CMake ≥ 3.22** (the version bundled with recent Visual Studio installs
   is sufficient; you can also install it standalone).
3. **Git**.
4. **Python 3.10+** (for `ai-backend`), and optionally **Ollama for
   Windows** (https://ollama.com) if you want local AI generation rather
   than only the mock provider.
5. A **VST3 SDK / licensing decision**. Steinberg's VST3 SDK is available
   under GPLv3 or a commercial license — JUCE's `Projucer`/CMake tooling can
   fetch the SDK, but which license applies to your build is a business
   decision that needs to be made before any commercial distribution (see
   docs/ROADMAP.md risks).

## Building `core` (DSP library + tests) — works today

```powershell
cmake -S . -B build -DTONEFORGE_BUILD_PLUGIN=OFF
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

This is the same thing that was verified in the Linux sandbox (27/27 tests
passing, including under ASan+UBSan) — it should behave identically on
Windows/MSVC since `core` has zero platform-specific code. If it doesn't,
that's a real portability bug worth filing, not an environment quirk.

## Building `plugin` (JUCE VST3 + Standalone) — not yet verified anywhere

```powershell
cmake -S . -B build -DTONEFORGE_BUILD_PLUGIN=ON
cmake --build build --config Release
```

This will use CMake's `FetchContent` to pull JUCE (configured in
`plugin/CMakeLists.txt`) — expect a large download and a long first build.
After building, the VST3 will be under
`build/plugin/ToneForgeAI_artefacts/Release/VST3/`. Load it in a DAW (Reaper
is a lightweight, free-to-trial option good for iteration) to smoke-test.

**This step has never actually been run** — it's the first thing to do
on your machine, and the first real test of whether `plugin/CMakeLists.txt`
as written is correct. Expect to need to fix real build errors; that's
normal for a first build of a JUCE integration, not a sign anything here
was done carelessly.

## Running `ai-backend`

```powershell
cd ai-backend
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
uvicorn app.main:app --host 127.0.0.1 --port 8765 --reload
```

Then, separately, run Ollama (natively, or in a container - see below) and pull a
model if you want real AI generation instead of the deterministic mock provider.
The plugin defaults to talking to `http://127.0.0.1:8765`.

### Ollama via Docker

Ollama doesn't have to be installed natively - running it in a container works the
same way from `ai-backend`'s point of view, as long as the container's port is
published to the host:

```powershell
docker run -d -p 11434:11434 --name ollama ollama/ollama
docker exec -it ollama ollama pull gemma3:4b
```

Confirm it's reachable from the host with `curl http://localhost:11434/api/tags` -
it should list the models you've pulled. If that works, `ai-backend`'s defaults
(`TONEFORGE_OLLAMA_URL=http://localhost:11434`) already point at it correctly with
no extra config, since `-p 11434:11434` maps the container's port straight onto
`localhost` from Windows' perspective.

This project's defaults (`app/config.py`) are set to `gemma3:4b` -
`TONEFORGE_OLLAMA_MODEL` only needs to be set if you're using a different model or
tag (check what you actually have with `docker exec -it ollama ollama list`). If
`ai-backend` itself ever also runs inside a container (not the setup above - here
it runs directly on Windows via `uvicorn`), you'd need `host.docker.internal` or a
shared Docker network instead of `localhost`.

## Recommended order of operations on your machine

1. Build and run `core`'s tests (should just work — confirms your toolchain
   is sane).
2. Stand up `ai-backend` and hit `/health`, `/generate` with `curl` or the
   FastAPI auto-docs at `http://127.0.0.1:8765/docs`.
3. Attempt the `plugin` build. Budget real time for this — first JUCE
   integration builds commonly surface a handful of CMake/linker issues
   that are fast to fix once you can see the actual compiler output, which
   this sandbox cannot produce.
