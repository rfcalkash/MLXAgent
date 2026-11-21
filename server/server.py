import asyncio
import gc
import re
import time
import uuid
from enum import Enum
from typing import Any, Optional

from fastapi import FastAPI, HTTPException
from huggingface_hub import scan_cache_dir, snapshot_download
import mlx.core as mx
from mlx_lm import load, stream_generate
from mlx_lm.sample_utils import make_sampler
from pydantic import BaseModel

app = FastAPI(title="MLX Local Server")


class ServerStatus(str, Enum):
    IDLE = "IDLE"
    DOWNLOADING = "DOWNLOADING"
    LOADING = "LOADING"
    LOADED = "LOADED"
    GENERATING = "GENERATING"
    UNLOADING = "UNLOADING"


THINKING_PAIRS = [
    (r"<\|channel>thought\s*", r"<channel\|>"),  # Gemma 4
    (r"(?:<\|channel>)?thought\s*", r"<channel\|>"),  # Gemma 4 (без нач. тега)
    (
        r"<\|channel\|>analysis<\|message\|>",
        r"<\|end\|>(?:<\|start\|>assistant<\|channel\|>final<\|message\|>)?",
    ),  # gpt-oss
    (r"<think>\s*", r"</think>"),  # Qwen3, DeepSeek-R1
    (r"<thinking>\s*", r"</thinking>"),  # Phi / Anthropic-style
    (r"<thought>\s*", r"</thought>"),  # Локальные файнтюны
    (r"\[THINK\]\s*", r"\[/THINK\]"),  # Mistral / Magistral
    (r"<\|start_of_thought\|>\s*", r"<\|end_of_thought\|>"),  # Nemotron / Granite
    (r"<\|begin_of_thought\|>\s*", r"<\|end_of_thought\|>"),  # GLM / Step
]


def parse_thinking(raw_text: str) -> dict[str, Optional[str]]:
    text = raw_text.lstrip()
    if not text:
        return {"think": None, "answer": ""}

    # 1. Полноценные закрытые пары тегов
    for start_pat, end_pat in THINKING_PAIRS:
        full_pat = rf"^{start_pat}(.*?){end_pat}\s*(.*)"
        match = re.match(full_pat, text, flags=re.DOTALL)
        if match:
            think_part = match.group(1).strip()
            answer_part = match.group(2).strip()
            return {
                "think": think_part if think_part else None,
                "answer": answer_part,
            }

    # 2. Незакрытый блок мыслей (модель думает или уперлась в лимит)
    for start_pat, _ in THINKING_PAIRS:
        open_match = re.match(rf"^{start_pat}(.*)", text, flags=re.DOTALL)
        if open_match:
            think_part = open_match.group(1).strip()
            return {"think": think_part if think_part else None, "answer": ""}

    # 3. Fallback: если открывающий тег был зашит в шаблон промпта
    for _, end_pat in THINKING_PAIRS:
        close_match = re.match(rf"^(.*?){end_pat}\s*(.*)", text, flags=re.DOTALL)
        if close_match:
            think_part = close_match.group(1).strip()
            answer_part = close_match.group(2).strip()
            return {
                "think": think_part if think_part else None,
                "answer": answer_part,
            }

    return {"think": None, "answer": text.strip()}


class ServerManager:

    def __init__(self):
        self.status: ServerStatus = ServerStatus.IDLE
        self.active_model_id: Optional[str] = None
        self.model: Any = None
        self.tokenizer: Any = None
        self.tasks: dict[str, dict] = {}

    def create_task(
        self, task_type: str, initial_result: Any = None
    ) -> str:
        task_id = str(uuid.uuid4())
        self.tasks[task_id] = {
            "task_id": task_id,
            "type": task_type,
            "status": "running",
            "result": initial_result,
            "error": None,
        }
        return task_id


manager = ServerManager()


class ModelRequest(BaseModel):
    model_id: str


class ChatRequest(BaseModel):
    messages: list[dict[str, str]]
    max_tokens: int = 1024
    temp: float = 0.7
    enable_thinking: bool = True


async def _task_download(task_id: str, repo_id: str):
    try:
        path = await asyncio.to_thread(snapshot_download, repo_id=repo_id)
        manager.tasks[task_id]["status"] = "completed"
        manager.tasks[task_id]["result"] = {"path": path}
    except Exception as e:
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)
    finally:
        manager.status = (
            ServerStatus.LOADED if manager.model else ServerStatus.IDLE
        )


async def _task_load(task_id: str, model_id: str):
    try:
        model, tokenizer = await asyncio.to_thread(load, model_id)
        manager.model = model
        manager.tokenizer = tokenizer
        manager.active_model_id = model_id
        manager.status = ServerStatus.LOADED
        manager.tasks[task_id]["status"] = "completed"
    except Exception as e:
        manager.status = ServerStatus.IDLE
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)


async def _task_unload(task_id: str):
    try:
        manager.model = None
        manager.tokenizer = None
        manager.active_model_id = None
        await asyncio.to_thread(gc.collect)
        mx.metal.clear_cache()
        manager.status = ServerStatus.IDLE
        manager.tasks[task_id]["status"] = "completed"
    except Exception as e:
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)


def _run_stream_generation(task_id: str, prompt: str, req: ChatRequest):
    sampler = make_sampler(temp=req.temp)
    accumulated = ""
    last_update = time.monotonic()
    finish_reason = None

    for response in stream_generate(
        manager.model,
        manager.tokenizer,
        prompt=prompt,
        max_tokens=req.max_tokens,
        sampler=sampler,
    ):
        accumulated += response.text
        finish_reason = response.finish_reason

        now = time.monotonic()
        if now - last_update >= 0.5:
            manager.tasks[task_id]["result"] = parse_thinking(accumulated)
            last_update = now

    manager.tasks[task_id]["result"] = parse_thinking(accumulated)
    if finish_reason == "length":
        manager.tasks[task_id]["error"] = "Out of tokens"


async def _task_generate(task_id: str, req: ChatRequest):
    try:
        try:
            prompt = manager.tokenizer.apply_chat_template(
                req.messages,
                tokenize=False,
                add_generation_prompt=True,
                enable_thinking=req.enable_thinking,
            )
        except TypeError:
            prompt = manager.tokenizer.apply_chat_template(
                req.messages, tokenize=False, add_generation_prompt=True
            )

        await asyncio.to_thread(_run_stream_generation, task_id, prompt, req)
        manager.tasks[task_id]["status"] = "completed"
    except Exception as e:
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)
    finally:
        manager.status = ServerStatus.LOADED


@app.get("/state")
def get_state():
    return {
        "status": manager.status,
        "active_model": manager.active_model_id,
    }


@app.get("/tasks/{task_id}")
def get_task(task_id: str):
    if task_id not in manager.tasks:
        raise HTTPException(404, "Task not found")
    return manager.tasks[task_id]


@app.get("/models")
def list_models():
    cache_info = scan_cache_dir()
    models = []
    for repo in cache_info.repos:
        if repo.repo_type == "model":
            models.append({
                "id": repo.repo_id,
                "size_mb": round(repo.size_on_disk / (1024 * 1024), 2),
            })
    return {"models": models}


@app.post("/models", status_code=202)
async def download_model(req: ModelRequest):
    if manager.status not in [ServerStatus.IDLE, ServerStatus.LOADED]:
        raise HTTPException(409, f"Server is busy: {manager.status}")

    manager.status = ServerStatus.DOWNLOADING
    task_id = manager.create_task("download")
    asyncio.create_task(_task_download(task_id, req.model_id))
    return {"task_id": task_id}


@app.delete("/models")
def delete_model(id: str):
    if manager.status not in [ServerStatus.IDLE, ServerStatus.LOADED]:
        raise HTTPException(409, f"Server is busy: {manager.status}")
    if manager.active_model_id == id:
        raise HTTPException(
            409, "Cannot delete currently loaded model. Unload it first."
        )

    cache_info = scan_cache_dir()
    for repo in cache_info.repos:
        if repo.repo_id == id:
            delete_strategy = cache_info.delete_revisions(
                *[rev.commit_hash for rev in repo.revisions]
            )
            delete_strategy.execute()
            return {"status": "deleted", "id": id}
    raise HTTPException(404, "Model not found in cache")


@app.post("/state/load", status_code=202)
async def load_model(req: ModelRequest):
    if manager.status != ServerStatus.IDLE:
        raise HTTPException(
            409, f"Server must be IDLE to load a model. Current: {manager.status}"
        )

    manager.status = ServerStatus.LOADING
    task_id = manager.create_task("load")
    asyncio.create_task(_task_load(task_id, req.model_id))
    return {"task_id": task_id}


@app.post("/state/unload", status_code=202)
async def unload_model():
    if manager.status != ServerStatus.LOADED:
        raise HTTPException(
            409, f"Cannot unload in current state: {manager.status}"
        )

    manager.status = ServerStatus.UNLOADING
    task_id = manager.create_task("unload")
    asyncio.create_task(_task_unload(task_id))
    return {"task_id": task_id}


@app.post("/chat", status_code=202)
async def chat(req: ChatRequest):
    if manager.status != ServerStatus.LOADED:
        raise HTTPException(
            409, f"Model is not ready. Current state: {manager.status}"
        )

    manager.status = ServerStatus.GENERATING
    task_id = manager.create_task(
        "generate", initial_result={"think": None, "answer": ""}
    )
    asyncio.create_task(_task_generate(task_id, req))
    return {"task_id": task_id}
