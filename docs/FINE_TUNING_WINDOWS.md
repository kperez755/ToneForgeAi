# Fine-tuning the local tone model on Windows (RTX 3070)

This walks through QLoRA fine-tuning `unsloth/gemma-3-4b-it-unsloth-bnb-4bit`
on ToneForge AI's own synthetic + curated-song dataset, natively on Windows
(no WSL needed), using [Unsloth](https://github.com/unslothai/unsloth). It
assumes an NVIDIA GPU with at least ~8GB VRAM (an RTX 3070 is sufficient with
headroom for this model size).

Why this exists: see `docs/ROADMAP.md`'s M6 log - the AI's tone quality for
songs outside the curated database and outside the model's own training
knowledge was hitting a real ceiling that web-search/Spotify grounding could
have fixed, but both were rejected as impractical / a bad fit for a
local-first, eventually-open-source tool. Fine-tuning bakes the project's own
existing deterministic mapping logic (and the curated song data) directly
into a small local model's weights instead, with no ongoing external
dependency.

This entire fine-tuning process runs on your machine, not in the sandbox
this project is otherwise developed in - I have no GPU to test any of this
against. Treat the first run as a trial: watch the training loss, run the
smoke test the script prints at the end, and report back anything that looks
wrong before doing a longer/bigger run.

## 1. Install prerequisites

**NVIDIA driver.** Open PowerShell and run:

```powershell
nvidia-smi
```

You should see your GPU listed along with a "CUDA Version" in the top right
corner (this is the max CUDA version your *driver* supports, not necessarily
what's installed - you'll match PyTorch to this number in step 3). If this
command fails, install/update your [NVIDIA driver](https://www.nvidia.com/en-us/drivers/)
first.

**Miniconda.** In PowerShell:

```powershell
Invoke-WebRequest -Uri "https://repo.anaconda.com/miniconda/Miniconda3-latest-Windows-x86_64.exe" -OutFile ".\miniconda.exe"
Start-Process -FilePath ".\miniconda.exe" -ArgumentList "/S" -Wait
del .\miniconda.exe
```

Then open **Anaconda Powershell Prompt** from the Start menu (not regular
PowerShell) for every step below.

## 2. Create the environment

```powershell
conda create --name unsloth_env python==3.12 -y
conda activate unsloth_env
```

## 3. Install PyTorch (must match your CUDA version)

Re-check `nvidia-smi`'s "CUDA Version" from step 1. Pick the matching
`cuXXX` index URL below (as of this writing Unsloth/PyTorch support cu118,
cu121, cu124, cu126, cu128, cu130 - if your driver reports something newer
than what's listed at [pytorch.org](https://pytorch.org), use the closest
one at or below it):

```powershell
pip3 install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu130
```

(Replace `cu130` with your actual version, e.g. `cu124`.)

**Verify it actually works before continuing** - this is the step most
likely to silently go wrong:

```powershell
python
```

```python
import torch
print(torch.cuda.is_available())
A = torch.ones((10, 10), device="cuda")
B = torch.ones((10, 10), device="cuda")
A @ B
```

You should see `True` and a 10x10 matrix of 10s. If `torch.cuda.is_available()`
is `False`, do not continue to step 4 - your CUDA/driver install needs
fixing first (see Troubleshooting below). Exit Python with `exit()`.

## 4. Install Unsloth

```powershell
pip install unsloth
```

## 5. Get the project + generate training data (if not already done)

The dataset generator (`ai-backend/scripts/generate_training_data.py`) has
already been run once and its output committed to
`ai-backend/training_data/tone_training_data.jsonl` (642 examples). If you
want to regenerate it (e.g. after adding more curated songs to
`song_reference/db.py`), re-run it - it's pure Python, no GPU needed:

```powershell
cd path\to\ToneForgeSecondary\ai-backend
python scripts\generate_training_data.py
```

## 6. Run the fine-tune

```powershell
cd path\to\ToneForgeSecondary\ai-backend
python scripts\finetune_tone_model.py
```

What to expect:
- Model download (~2-3GB for the 4-bit quantized base) the first time.
- Training itself: with 642 examples, batch size 2, grad accum 4 (effective
  batch 8), 3 epochs, expect roughly a few hundred optimizer steps - on an
  RTX 3070 this should be well under an hour, likely 15-30 minutes, though
  this is an estimate, not a guarantee - watch the actual logged
  `train_loss` in the console.
- At the end: a quick inference smoke test against 2 real prompts (printed
  to the console) before anything is exported - read this output. If the
  JSON looks malformed or nonsensical, stop and report back rather than
  proceeding to export.
- Then GGUF export (Q4_K_M quantization) - **this step alone takes 5-10+
  minutes**, this is normal, let it finish.

Output layout:
```
ai-backend/training_output/
  checkpoints/        - intermediate training checkpoints (safe to delete after)
  lora_adapter/        - ~100MB LoRA adapter alone (portable, reusable)
  gguf_export/          - merged Q4_K_M .gguf file + an auto-generated Modelfile
```

## 7. Export to Ollama

The GGUF export step already writes a `Modelfile` for you (Unsloth generates
this automatically, including the correct chat template it was trained
with). From `ai-backend/training_output/gguf_export/`:

```powershell
ollama create toneforge-tone -f Modelfile
```

Verify it's registered:

```powershell
ollama list
```

You should see `toneforge-tone` in the list.

## 8. Point the backend at it

Set the env var the same way you already configure `TONEFORGE_OLLAMA_MODEL`
(see `app/config.py`), then restart uvicorn:

```powershell
$env:TONEFORGE_OLLAMA_MODEL = "toneforge-tone"
uvicorn app.main:app --reload
```

## 9. Compare against base gemma3:4b

Re-run the same live tests we've already done against the base model
(`/generate` with "warm bluesy overdrive with a bit of spring reverb",
`/song-reference` for "Sweet Child O' Mine", "Freaking Out the Neighborhood
by Mac DeMarco", "Cliffs of Dover") and compare the results side by side.
The things to actually look for: does `modulation.v1` show up when it
should (the Mac DeMarco gap that motivated this whole effort), are
`reverb_character`/`modulation_type`/`distortion_style` chosen sensibly, and
does the response stay valid JSON matching the expected shape consistently
(not just on cherry-picked prompts).

---

## Troubleshooting

These are Unsloth's own general troubleshooting steps, most relevant to a
first-time Windows install:

- **`torch.cuda.is_available()` is `False`**: your installed CUDA toolkit
  version needs to actually match what your NVIDIA driver reports in
  `nvidia-smi`. Try `nvcc --version` - if that fails entirely, you likely
  need to reinstall NVIDIA drivers (not just CUDA toolkit).
- **`unsloth` install succeeds but errors on import/first run**: force a
  clean reinstall:
  ```powershell
  pip install --upgrade --force-reinstall --no-cache-dir --no-deps unsloth
  pip install --upgrade --force-reinstall --no-cache-dir --no-deps unsloth_zoo
  ```
- **`xformers`/`bitsandbytes` errors**: check each in isolation:
  ```powershell
  python -m xformers.info
  python -m bitsandbytes
  ```
- **CUDA out-of-memory during training**: in
  `scripts/finetune_tone_model.py`, first try halving
  `PER_DEVICE_BATCH_SIZE` (e.g. 2 -> 1) and doubling `GRAD_ACCUM_STEPS`
  (e.g. 4 -> 8) - same effective batch size, lower peak memory. Only reduce
  `MAX_SEQ_LENGTH` as a last resort (our examples are short, so this
  shouldn't be necessary).
- **Training runs fine but the exported Ollama model produces gibberish or
  never stops generating**: this is almost always a chat-template/EOS-token
  mismatch between training and inference - Unsloth's auto-generated
  `Modelfile` should already have this right (it's built from the exact
  template used during training), so if this happens, don't hand-edit the
  Modelfile's template - report back instead, since it likely means
  something upstream (a newer Unsloth/Ollama/gemma3 release) changed
  behavior since these instructions were written.

## Sources

- [How to Fine-Tune LLMs on Windows with Unsloth](https://unsloth.ai/docs/get-started/install/windows-installation)
- [Install Unsloth via pip and uv](https://unsloth.ai/docs/get-started/install/pip-install)
- [Chat Templates](https://unsloth.ai/docs/basics/chat-templates)
- [Saving models to Ollama](https://unsloth.ai/docs/basics/inference-and-deployment/saving-to-ollama)
- [Saving to GGUF](https://unsloth.ai/docs/basics/inference-and-deployment/saving-to-gguf)
