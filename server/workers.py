import asyncio
import gc
import re
import time
from typing import Any, Optional

from huggingface_hub import snapshot_download
import mlx.core as mx
from mlx_lm import load as lm_load, stream_generate as lm_stream_generate
from mlx_lm.sample_utils import make_sampler, make_repetition_penalty
from mlx_vlm import load as vlm_load, stream_generate as vlm_stream_generate
from mlx_vlm.prompt_utils import apply_chat_template as vlm_apply_chat_template


from database import db
from schemas import ChatRequest, LoadModelRequest, ServerStatus
from state import manager
from storage import storage
from utils import (
    detect_model_capabilities,
    get_model_cache_path,
    parse_model_output,
    parse_thinking,
    process_document_attachment
)

from pathlib import Path 
import tempfile

TRIVIAL_WORDS = {
    "привет",
    "хай",
    "здравствуй",
    "здравствуйте",
    "ку",
    "hello",
    "hi",
    "hey",
    "тест",
    "test",
    "как дела",
    "че как",
    "чё как",
}


def _is_trivial_dialog(user_text: str, answer_text: str) -> bool:
    clean_u = re.sub(r"[^\w\s]", "", user_text.lower()).strip()
    if len(clean_u) < 4 or clean_u in TRIVIAL_WORDS:
        return True
    if len(user_text.strip()) + len(answer_text.strip()) < 30:
        return True
    return False


def _generate_chat_title_sync(user_text: str, answer_text: str) -> Optional[str]:
    if _is_trivial_dialog(user_text, answer_text):
        return None

    sys_prompt = (
        "Придумай короткий заголовок чата (2-4 слова) на языке пользователя по сути диалога. "
        "Без кавычек, без точки в конце. Если диалог бессмысленный или состоит из приветствия/непонимания, "
        "ответь ровно одним словом: DEFAULT"
    )
    dialog_snippet = f"Пользователь: {user_text[:300]}\nАссистент: {answer_text[:300]}"
    msgs = [
        {"role": "system", "content": sys_prompt},
        {"role": "user", "content": dialog_snippet},
    ]

    try:
        if manager.is_vlm:
            prompt = vlm_apply_chat_template(
                manager.processor, manager.model.config, msgs, num_images=0
            )
            out = "".join(
                r.text
                for r in vlm_stream_generate(
                    manager.model,
                    manager.processor,
                    prompt,
                    max_tokens=20,
                    temp=0.2,
                )
            )
        else:
            try:
                prompt = manager.processor.apply_chat_template(
                    msgs,
                    tokenize=False,
                    add_generation_prompt=True,
                    enable_thinking=False,
                )
            except TypeError:
                prompt = manager.processor.apply_chat_template(
                    msgs, tokenize=False, add_generation_prompt=True
                )
            out = "".join(
                r.text
                for r in lm_stream_generate(
                    manager.model,
                    manager.processor,
                    prompt=prompt,
                    max_tokens=20,
                    sampler=make_sampler(temp=0.2),
                )
            )

        parsed = parse_thinking(out)["answer"].strip().strip("\"'«».")
        if not parsed or "DEFAULT" in parsed.upper() or len(parsed) > 60:
            return None
        return parsed
    except Exception:
        return None


def _build_template_messages(
    req: ChatRequest,
    is_vlm: bool,
    temp_dir: Optional[Path] = None,
) -> tuple[list[dict[str, Any]], list[str], list[str]]:
    formatted: list[dict[str, Any]] = []
    all_image_paths: list[str] = []
    last_turn_warnings: list[str] = []

    for msg_idx, m in enumerate(req.messages):
        is_last_user_msg = (msg_idx == len(req.messages) - 1) and (m.role == "user")
        doc_blocks: list[str] = []

        for att in m.attachments:
            resolved = storage.get_file_path(req.user_uuid, req.chat_id, att.file_id)
            if not resolved:
                continue

            if att.type == "image":
                if is_vlm:
                    all_image_paths.append(str(resolved))
            elif att.type == "document":
                doc_text, pdf_imgs, warn = process_document_attachment(
                    resolved, is_vlm=is_vlm, temp_dir=temp_dir
                )
                doc_blocks.append(doc_text)
                if is_vlm and pdf_imgs:
                    all_image_paths.extend(pdf_imgs)
                if warn and is_last_user_msg:
                    last_turn_warnings.append(warn)

        full_content = m.content or ""
        if doc_blocks:
            docs_joined = "\n\n".join(doc_blocks)
            full_content = f"{docs_joined}\n\n{full_content}".strip()

        msg_dict: dict[str, Any] = {
            "role": m.role,
            "content": full_content,
        }
        if m.tool_calls:
            msg_dict["tool_calls"] = m.tool_calls
        if m.tool_call_id:
            msg_dict["tool_call_id"] = m.tool_call_id
        formatted.append(msg_dict)

    return formatted, all_image_paths, last_turn_warnings



async def task_download(task_id: str, repo_id: str):
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


async def task_load(task_id: str, req: LoadModelRequest):
    try:
        snapshot_path = get_model_cache_path(req.model_id)
        native_caps = detect_model_capabilities(snapshot_path)
        use_vlm = (not req.text_only) and (len(native_caps) > 1)

        if use_vlm:
            model, processor = await asyncio.to_thread(vlm_load, req.model_id)
            manager.is_vlm = True
            manager.active_capabilities = native_caps
        else:
            model, processor = await asyncio.to_thread(lm_load, req.model_id)
            manager.is_vlm = False
            manager.active_capabilities = ["text", "document"]

        manager.model = model
        manager.processor = processor
        manager.active_model_id = req.model_id
        manager.text_only = req.text_only
        manager.status = ServerStatus.LOADED
        manager.tasks[task_id]["status"] = "completed"
    except Exception as e:
        manager.status = ServerStatus.IDLE
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)


async def task_unload(task_id: str):
    try:
        manager.model = None
        manager.processor = None
        manager.active_model_id = None
        manager.active_capabilities = []
        manager.is_vlm = False
        manager.text_only = False

        await asyncio.to_thread(gc.collect)
        mx.metal.clear_cache()

        manager.status = ServerStatus.IDLE
        manager.tasks[task_id]["status"] = "completed"
    except Exception as e:
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)


def _run_lm_generation(task_id: str, req: ChatRequest):
    plain_messages, _, doc_warnings = _build_template_messages(req, is_vlm=False)

    template_kwargs: dict[str, Any] = {
        "tokenize": False,
        "add_generation_prompt": True,
    }
    if req.tools:
        template_kwargs["tools"] = req.tools

    try:
        prompt = manager.processor.apply_chat_template(
            plain_messages,
            enable_thinking=req.enable_thinking,
            **template_kwargs,
        )
    except TypeError:
        try:
            prompt = manager.processor.apply_chat_template(
                plain_messages, **template_kwargs
            )
        except TypeError:
            prompt = manager.processor.apply_chat_template(
                plain_messages, tokenize=False, add_generation_prompt=True
            )

    sampler = make_sampler(temp=req.temp)
    logits_processors = (
        [make_repetition_penalty(penalty=req.repetition_penalty, context_size=64)]
        if req.repetition_penalty and req.repetition_penalty > 1.0
        else None
    )

    accumulated = ""
    last_update = time.monotonic()
    finish_reason = None

    for response in lm_stream_generate(
        manager.model,
        manager.processor,
        prompt=prompt,
        max_tokens=req.max_tokens,
        sampler=sampler,
        logits_processors=logits_processors,
    ):
        if manager.cancel_event.is_set():
            finish_reason = "cancelled"
            break

        accumulated += response.text
        finish_reason = response.finish_reason

        now = time.monotonic()
        if now - last_update >= 0.5:
            partial = parse_thinking(accumulated)
            partial["tool_calls"] = []
            manager.tasks[task_id]["result"] = partial
            last_update = now

    manager.tasks[task_id]["result"] = parse_model_output(accumulated)
    if finish_reason == "cancelled":
        manager.tasks[task_id]["error"] = "Stopped by user"
    elif finish_reason == "length":
        manager.tasks[task_id]["error"] = "Out of tokens"
    if doc_warnings and not manager.tasks[task_id]["error"]:
        manager.tasks[task_id]["error"] = "; ".join(doc_warnings)


def _run_vlm_generation(task_id: str, req: ChatRequest):
    with tempfile.TemporaryDirectory() as tmp_str:
        vlm_messages, image_paths, doc_warnings = _build_template_messages(req, is_vlm=True, temp_dir=Path(tmp_str))

        try:        
                prompt = vlm_apply_chat_template(
                    manager.processor,
                    manager.model.config,
                    vlm_messages,
                    num_images=len(image_paths),
                    tools=req.tools if req.tools else None,
                )
        except TypeError:
            prompt = vlm_apply_chat_template(
                manager.processor,
                manager.model.config,
                vlm_messages,
                num_images=len(image_paths),
            )

        vlm_kwargs: dict[str, Any] = {
            "image": image_paths if image_paths else None,
            "max_tokens": req.max_tokens,
            "temp": req.temp,
        }
        if req.repetition_penalty and req.repetition_penalty > 1.0:
            vlm_kwargs["repetition_penalty"] = req.repetition_penalty

        accumulated = ""
        last_update = time.monotonic()
        finish_reason = None

        for response in vlm_stream_generate(
            manager.model,
            manager.processor,
            prompt,
            **vlm_kwargs,
        ):
            if manager.cancel_event.is_set():
                finish_reason = "cancelled"
                break

            accumulated += response.text
            finish_reason = getattr(response, "finish_reason", None)

            now = time.monotonic()
            if now - last_update >= 0.5:
                partial = parse_thinking(accumulated)
                partial["tool_calls"] = []
                manager.tasks[task_id]["result"] = partial
                last_update = now

        manager.tasks[task_id]["result"] = parse_model_output(accumulated)
        if finish_reason == "cancelled":
            manager.tasks[task_id]["error"] = "Stopped by user"
        elif finish_reason == "length":
            manager.tasks[task_id]["error"] = "Out of tokens"
        if doc_warnings and not manager.tasks[task_id]["error"]:
            manager.tasks[task_id]["error"] = "; ".join(doc_warnings)



def save_incoming_messages(req: ChatRequest):
    """Сохраняет новые входящие сообщения (user или tool) в БД сразу при приеме запроса."""
    new_incoming = []
    for msg in reversed(req.messages):
        if msg.role == "assistant":
            break
        new_incoming.append(msg)
    new_incoming.reverse()

    for msg in new_incoming:
        db.add_message(
            chat_id=req.chat_id,
            user_uuid=req.user_uuid,
            role=msg.role,
            content=msg.content or "",
            attachments=[a.model_dump() for a in msg.attachments],
            tool_calls=msg.tool_calls,
            tool_call_id=msg.tool_call_id,
        )


async def task_generate(task_id: str, req: ChatRequest):
    manager.status = ServerStatus.GENERATING
    manager.current_gen_task_id = task_id
    manager.current_gen_chat_id = req.chat_id
    manager.cancel_event.clear()
    manager.tasks[task_id]["status"] = "running"

    try:
        existing_msgs = db.get_chat_messages(req.chat_id)
        is_first_turn = not any(
            m["role"] == "assistant" and m["content"] for m in existing_msgs
        )

        committed_ids = db.get_committed_file_ids(req.user_uuid, req.chat_id)
        await asyncio.to_thread(
            storage.cleanup_orphans, req.user_uuid, req.chat_id, committed_ids
        )

        worker = _run_vlm_generation if manager.is_vlm else _run_lm_generation
        await asyncio.to_thread(worker, task_id, req)

        final_res = manager.tasks[task_id]["result"]
        warning = manager.tasks[task_id]["error"]
        was_cancelled = manager.cancel_event.is_set()
        tool_calls = [] if was_cancelled else (final_res.get("tool_calls") or [])
        final_res["tool_calls"] = tool_calls

        db.add_message(
            chat_id=req.chat_id,
            user_uuid=req.user_uuid,
            role="assistant",
            content=final_res["answer"],
            think=final_res["think"],
            warning=warning,
            tool_calls=tool_calls if tool_calls else None,
        )

        chat_title = None
        if is_first_turn and final_res["answer"] and not was_cancelled:
            first_user_text = next(
                (m.content or "" for m in req.messages if m.role == "user"),
                "",
            )
            chat_title = await asyncio.to_thread(
                _generate_chat_title_sync,
                first_user_text,
                final_res["answer"],
            )
            if chat_title:
                db.update_chat_title(req.chat_id, chat_title)

        final_res["chat_id"] = req.chat_id
        if chat_title:
            final_res["chat_title"] = chat_title

        manager.tasks[task_id]["status"] = "completed"
    except Exception as e:
        manager.tasks[task_id]["status"] = "failed"
        manager.tasks[task_id]["error"] = str(e)
    finally:
        manager.current_gen_task_id = None
        manager.current_gen_chat_id = None
        manager.cancel_event.clear()
        manager.status = (
            ServerStatus.LOADED if manager.model is not None else ServerStatus.IDLE
        )


async def generation_worker_loop():
    """Фоновый консьюмер очереди deque."""
    while True:
        async with manager.queue_condition:
            while not manager.generation_queue:
                await manager.queue_condition.wait()
            task_id, req = manager.generation_queue.popleft()

        if manager.tasks.get(task_id, {}).get("status") == "cancelled":
            continue

        await task_generate(task_id, req)

