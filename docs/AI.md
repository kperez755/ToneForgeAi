# ToneForge AI — AI Backend

Run it locally:

```bash
cd ai-backend
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --host 127.0.0.1 --port 8765 --reload
```

Run the tests (no Ollama required — forced to the mock provider, see
`tests/conftest.py`):

```bash
cd ai-backend
python3 -m pytest -q
```

## Endpoints

| Method | Path | Request | Response |
|---|---|---|---|
| POST | `/generate` | `GenerateRequest{prompt}` | `PresetDTO` |
| POST | `/modify` | `ModifyRequest{preset, instruction}` | `PresetDTO` |
| POST | `/explain` | `ExplainRequest{preset}` | `ExplainResponse{explanation}` |
| POST | `/song-reference` | `SongReferenceRequest{query}` | `PresetDTO` |
| GET | `/health` | — | `HealthResponse{status, providers[]}` |

All request/response bodies are Pydantic models in `app/schemas/`. The
`PresetDTO` shape is a field-for-field mirror of `tfcore::Preset`
(`core/include/tfcore/preset/Preset.h`) — see the module docstring in
`app/schemas/preset.py` for why that coupling is currently manual, and why
it's flagged as an M9 hardening item (generate a JSON Schema from one side
and validate the other against it in CI).

## Providers

`app/providers/base.py` defines the `AIProvider` Protocol. Two
implementations exist today:

- **MockProvider** (`app/providers/mock_provider.py`) — deterministic
  keyword-to-parameter mapping (`app/core/tone_descriptor.py` +
  `app/core/mapping.py`). No network, no model, always available. This is
  the provider used by every test in `ai-backend/tests/`, and the automatic
  fallback (see `app/api/routes_generate.py`) whenever the primary provider
  raises `ProviderUnavailableError`.
- **OllamaProvider** (`app/providers/ollama_provider.py`) — calls a local
  Ollama instance over HTTP, asks for a small JSON tone-descriptor object
  (not a full preset — the actual module/parameter expansion still goes
  through the same `descriptor_to_preset()` used by MockProvider, so both
  providers produce presets with an identical, validated shape), validates
  the response, and retries with a corrective prompt up to
  `max_retries` times before raising `ProviderUnavailableError`.

Provider selection is controlled by the `TONEFORGE_PRIMARY_PROVIDER`
environment variable (`ollama` by default; tests force `mock`). Adding a
cloud provider later is a new class implementing `AIProvider` plus a new
branch in `app/deps.py::get_primary_provider` — no route code changes.

## Song reference system

`app/song_reference/db.py` holds a small curated `song -> ToneDescriptor`
table (6 entries at MVP time — see docs/ROADMAP.md M7 for how this grows).
`app/song_reference/lookup.py` fuzzy-matches the query against it
(`rapidfuzz.fuzz.token_set_ratio`, threshold 70/100); on a miss it asks the
configured provider to infer a descriptor and returns that instead. Every
result carries `match_type` (`"song_curated"` or `"song_inferred"`), a
`confidence` score, and a non-empty `disclaimer` string enforced at the
schema level (`AiProvenanceDTO.disclaimer` has a validator rejecting blank
values) — this is what keeps "inspired by Cliffs of Dover" from ever being
presented as "this is Cliffs of Dover."

## Known limitations (honest, not hidden)

- The Ollama JSON-descriptor prompt has not been tested against a running
  Ollama instance in this environment (no Ollama installed in this
  sandbox). The retry/repair loop and JSON-extraction logic are unit-tested
  only insofar as they're exercised through `_extract_json_object` and
  `_merge_descriptor`'s logic paths — real-world prompt reliability tuning
  is explicitly scoped to docs/ROADMAP.md M6.
- The curated song database is intentionally tiny (6 songs) — a real
  product needs hundreds to thousands of entries and almost certainly a
  proper data pipeline (curation UI, versioning, contributor review) rather
  than a hardcoded Python list. That's an M7 problem, flagged now.
