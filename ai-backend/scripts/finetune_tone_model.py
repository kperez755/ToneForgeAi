"""QLoRA fine-tuning script for ToneForge AI's local tone-generation model.

Trains `unsloth/gemma-3-4b-it-unsloth-bnb-4bit` (the same gemma3:4b family
OllamaProvider already targets by default - see app/config.py) on the
synthetic + curated-song dataset produced by
`scripts/generate_training_data.py`, using Unsloth's QLoRA implementation,
then exports the result straight to Ollama-ready GGUF + a Modelfile.

This is meant to run on the user's own Windows machine with an RTX 3070
(8GB VRAM) - it CANNOT be run or verified in the sandbox this project is
otherwise developed in (no GPU there). See docs/FINE_TUNING_WINDOWS.md for
the full install/run/export walkthrough this script assumes.

Design decisions worth knowing before touching this file:

1. System-role messages are merged into the user turn, not kept as a
   separate "system" message. Gemma's own chat template does not have a
   dedicated, reliable system-role slot (this is a known gap in Gemma 3's
   template, not an Unsloth limitation - see docs/FINE_TUNING_WINDOWS.md's
   sources) - so `tone_training_data.jsonl`'s
   {system, user, assistant}-shaped rows are flattened here into a 2-turn
   {user, assistant} conversation before the chat template is ever applied,
   the same way most Gemma-based apps fold system instructions into the
   first user turn. Only two distinct system prompts exist in the dataset
   (_SYSTEM_PROMPT / _SONG_INFERENCE_SYSTEM_PROMPT from
   app/providers/ollama_provider.py) so this is a small, mechanical
   transform, not a lossy one.

2. `train_on_responses_only` masks the loss to the assistant's JSON
   response only (Gemma's `<start_of_turn>model\n` marker onward) - without
   this, the model would also be trained to predict the (very long, mostly
   fixed) system+user text, which wastes capacity and, worse, biases the
   model toward memorizing/echoing prompt boilerplate instead of learning
   the prompt -> JSON mapping this whole exercise is for.

3. `dataset_num_proc = 1`: Windows' lack of native fork() makes
   datasets/multiprocessing `map(..., num_proc > 1)` calls unreliable
   (hangs or silent errors are a common Windows-specific complaint in
   Unsloth's own issue tracker) - single-process mapping is slightly slower
   but is the safe default here given this is a small (~640 row) dataset
   anyway.

Run (after following docs/FINE_TUNING_WINDOWS.md's setup):
    cd ai-backend
    python scripts/finetune_tone_model.py

Output:
    ai-backend/training_output/lora_adapter/      - the ~100MB LoRA adapter alone
    ai-backend/training_output/gguf_export/       - merged Q4_K_M GGUF + auto-generated Modelfile,
                                                       ready for `ollama create`
"""

from __future__ import annotations

# Unsloth must be imported before transformers/peft/trl for its patches to
# take effect - this is Unsloth's own documented requirement, not a style
# preference. Do not reorder these imports.
from unsloth import FastLanguageModel
from unsloth.chat_templates import get_chat_template, train_on_responses_only

import json
from pathlib import Path

import torch
from datasets import Dataset
from trl import SFTConfig, SFTTrainer

# ---------------------------------------------------------------------------
# Config - tuned for an 8GB RTX 3070 and a ~640-row task-specific dataset.
# If you hit a CUDA out-of-memory error, first try halving
# PER_DEVICE_BATCH_SIZE and doubling GRAD_ACCUM_STEPS (same effective batch
# size, less peak memory) before reducing MAX_SEQ_LENGTH.
# ---------------------------------------------------------------------------

BASE_MODEL = "unsloth/gemma-3-4b-it-unsloth-bnb-4bit"  # Unsloth's dynamic-4bit quant of the same gemma3:4b family app/config.py already defaults to
MAX_SEQ_LENGTH = 1024  # our longest system+user+assistant example is well under this; generous headroom, not a bottleneck at this dataset size
LORA_RANK = 16
LORA_ALPHA = 16
PER_DEVICE_BATCH_SIZE = 2
GRAD_ACCUM_STEPS = 4  # effective batch size 8
NUM_TRAIN_EPOCHS = 3  # small, narrow-format dataset - watch train loss; if it plateaus well before epoch 3 ends, that's fine, SFTTrainer just stops improving, it won't hurt
LEARNING_RATE = 2e-4
SEED = 3407  # Unsloth's own examples use this seed throughout - kept for consistency with anything you compare against upstream

_HERE = Path(__file__).resolve().parent
_AI_BACKEND_ROOT = _HERE.parent
TRAINING_DATA_PATH = _AI_BACKEND_ROOT / "training_data" / "tone_training_data.jsonl"
OUTPUT_ROOT = _AI_BACKEND_ROOT / "training_output"
LORA_ADAPTER_DIR = OUTPUT_ROOT / "lora_adapter"
GGUF_EXPORT_DIR = OUTPUT_ROOT / "gguf_export"


def _load_and_flatten_dataset(path: Path) -> Dataset:
    """Read tone_training_data.jsonl (system/user/assistant rows) and
    flatten each row into a 2-turn (user, assistant) conversation - see
    module docstring point 1 for why the system message is merged into the
    user turn rather than kept separate.
    """
    if not path.exists():
        raise FileNotFoundError(
            f"{path} does not exist. Run `python scripts/generate_training_data.py` "
            "first (from ai-backend/) to produce it."
        )

    conversations: list[list[dict[str, str]]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            messages = row["messages"]
            system_content = next(m["content"] for m in messages if m["role"] == "system")
            user_content = next(m["content"] for m in messages if m["role"] == "user")
            assistant_content = next(m["content"] for m in messages if m["role"] == "assistant")

            merged_user = f"{system_content}\n\n{user_content}"
            conversations.append(
                [
                    {"role": "user", "content": merged_user},
                    {"role": "assistant", "content": assistant_content},
                ]
            )

    return Dataset.from_dict({"conversations": conversations})


def main() -> None:
    print(f"Loading base model: {BASE_MODEL}")
    model, tokenizer = FastLanguageModel.from_pretrained(
        model_name=BASE_MODEL,
        max_seq_length=MAX_SEQ_LENGTH,
        load_in_4bit=True,
        load_in_8bit=False,
        full_finetuning=False,
    )

    tokenizer = get_chat_template(tokenizer, chat_template="gemma-3")

    model = FastLanguageModel.get_peft_model(
        model,
        r=LORA_RANK,
        target_modules=["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"],
        lora_alpha=LORA_ALPHA,
        lora_dropout=0,
        bias="none",
        use_gradient_checkpointing="unsloth",  # ~30% less VRAM - meaningful headroom on an 8GB card
        random_state=SEED,
        use_rslora=False,
        loftq_config=None,
    )

    print(f"Loading dataset: {TRAINING_DATA_PATH}")
    dataset = _load_and_flatten_dataset(TRAINING_DATA_PATH)

    def formatting_prompts_func(examples):
        convos = examples["conversations"]
        texts = [
            tokenizer.apply_chat_template(convo, tokenize=False, add_generation_prompt=False) for convo in convos
        ]
        return {"text": texts}

    dataset = dataset.map(formatting_prompts_func, batched=True, num_proc=1)

    # 90/10 split purely as a sanity signal (eval_loss trending down / not
    # diverging) - this dataset is deliberately small and single-purpose,
    # this is not meant to be a rigorous held-out benchmark.
    split = dataset.train_test_split(test_size=0.1, seed=SEED)
    train_dataset, eval_dataset = split["train"], split["test"]
    print(f"Train examples: {len(train_dataset)} | Eval examples: {len(eval_dataset)}")

    trainer = SFTTrainer(
        model=model,
        tokenizer=tokenizer,
        train_dataset=train_dataset,
        eval_dataset=eval_dataset,
        dataset_text_field="text",
        args=SFTConfig(
            max_seq_length=MAX_SEQ_LENGTH,
            per_device_train_batch_size=PER_DEVICE_BATCH_SIZE,
            per_device_eval_batch_size=PER_DEVICE_BATCH_SIZE,
            gradient_accumulation_steps=GRAD_ACCUM_STEPS,
            warmup_steps=10,
            num_train_epochs=NUM_TRAIN_EPOCHS,
            learning_rate=LEARNING_RATE,
            logging_steps=10,
            eval_strategy="steps",
            eval_steps=25,
            optim="adamw_8bit",
            weight_decay=0.01,
            lr_scheduler_type="linear",
            seed=SEED,
            output_dir=str(OUTPUT_ROOT / "checkpoints"),
            dataset_num_proc=1,  # see module docstring point 3 - Windows-specific
            report_to="none",
        ),
    )

    # Mask loss to the assistant's response only - see module docstring
    # point 2. These markers are Gemma 3's own chat-template turn tags, not
    # something specific to this dataset.
    trainer = train_on_responses_only(
        trainer,
        instruction_part="<start_of_turn>user\n",
        response_part="<start_of_turn>model\n",
    )

    gpu_name = torch.cuda.get_device_name(0) if torch.cuda.is_available() else "NO GPU DETECTED"
    print(f"Training on: {gpu_name}")
    if not torch.cuda.is_available():
        raise RuntimeError(
            "CUDA is not available to PyTorch. Fine-tuning on CPU is impractically slow for this "
            "model size - fix your CUDA/PyTorch install before continuing (see "
            "docs/FINE_TUNING_WINDOWS.md's troubleshooting section)."
        )

    trainer_stats = trainer.train()
    print(f"Training complete. Stats: {trainer_stats}")

    print(f"Saving LoRA adapter to {LORA_ADAPTER_DIR}")
    LORA_ADAPTER_DIR.mkdir(parents=True, exist_ok=True)
    model.save_pretrained(str(LORA_ADAPTER_DIR))
    tokenizer.save_pretrained(str(LORA_ADAPTER_DIR))

    # Quick qualitative smoke test - run a couple of real prompts through
    # the just-trained model right now, before touching Ollama at all, so
    # there's an immediate sanity check that training actually did
    # something sensible.
    print("\n--- Quick inference smoke test (pre-export) ---")
    FastLanguageModel.for_inference(model)
    smoke_test_prompts = [
        "Give me a warm bluesy overdrive with a bit of spring reverb.",
        "Freaking Out the Neighborhood by Mac DeMarco",
    ]
    from app.providers.ollama_provider import _SONG_INFERENCE_SYSTEM_PROMPT, _SYSTEM_PROMPT  # local import: only needed for this smoke test

    for i, prompt in enumerate(smoke_test_prompts):
        system_prompt = _SONG_INFERENCE_SYSTEM_PROMPT if i == 1 else _SYSTEM_PROMPT
        merged = f"{system_prompt}\n\n{prompt}"
        messages = [{"role": "user", "content": merged}]
        inputs = tokenizer.apply_chat_template(
            messages, tokenize=True, add_generation_prompt=True, return_tensors="pt"
        ).to("cuda")
        output = model.generate(input_ids=inputs, max_new_tokens=256, temperature=0.3, do_sample=True)
        decoded = tokenizer.decode(output[0][inputs.shape[1] :], skip_special_tokens=True)
        print(f"\nPrompt: {prompt}\nResponse: {decoded}")

    print(f"\nExporting merged model to GGUF (Q4_K_M) + Ollama Modelfile at {GGUF_EXPORT_DIR}")
    print("This step is slow (5-10+ minutes) - this is normal, let it finish.")
    GGUF_EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    model.save_pretrained_gguf(str(GGUF_EXPORT_DIR), tokenizer, quantization_method="q4_k_m")

    print(
        "\nDone. Next steps (see docs/FINE_TUNING_WINDOWS.md 'Export to Ollama' section):\n"
        f"  1. cd {GGUF_EXPORT_DIR}\n"
        "  2. ollama create toneforge-tone -f Modelfile\n"
        "  3. Set TONEFORGE_OLLAMA_MODEL=toneforge-tone in ai-backend's env and restart uvicorn\n"
        "  4. Re-run the same live comparisons we did against base gemma3:4b (Sweet Child O' Mine, "
        "Mac DeMarco, Cliffs of Dover, etc.) and compare."
    )


if __name__ == "__main__":
    main()
