import asyncio

from fastapi import FastAPI, File, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse
from huggingface_hub import scan_cache_dir

from database import db
from sqlite3 import IntegrityError
from schemas import (
    ChatRequest,
    CompleteUploadRequest,
    CreateChatRequest,
    DownloadModelRequest,
    LoadModelRequest,
    ServerStatus,
    UserAuthRequest,
)
from state import manager
from storage import storage
from utils import detect_media_type, detect_model_capabilities
from workers import task_download, task_generate, task_load, task_unload

app = FastAPI(title="MLX Local Server")


# --- Пользователи ---


@app.post("/users/register")
def register_user(req: UserAuthRequest):
    try:
        return db.create_user(req.login, req.password)
    except Exception:
        raise HTTPException(409, "Login already exists")


@app.post("/users/login")
def login_user(req: UserAuthRequest):
    user = db.authenticate_user(req.login, req.password)
    if not user:
        raise HTTPException(401, "Invalid login or password")
    return user


# --- Чаты ---


@app.post("/chats")
def create_chat(req: CreateChatRequest):
    try:
        return db.create_chat(req.user_uuid, req.title)
    except IntegrityError:
        raise HTTPException(401, "User not found. Please log in again.")



@app.get("/chats")
def list_chats(user_uuid: str):
    return {"chats": db.get_user_chats(user_uuid)}


@app.get("/chats/{chat_id}")
def get_chat_history(chat_id: str):
    return {"chat_id": chat_id, "messages": db.get_chat_messages(chat_id)}


@app.delete("/chats/{chat_id}")
def delete_chat(chat_id: str, user_uuid: str):
    db.delete_chat(chat_id, user_uuid)
    return {"status": "deleted", "chat_id": chat_id}


# --- Файлы: быстрая загрузка + чанковая дозагрузка с SHA-256 ---


@app.post("/files")
async def upload_file_simple(
    user_uuid: str, chat_id: str, file: UploadFile = File(...)
):
    filename = file.filename or "upload.bin"
    file_id = storage.save_stream(user_uuid, chat_id, filename, file.file)
    return {
        "file_id": file_id,
        "media_type": detect_media_type(filename, file.content_type or ""),
    }


@app.get("/files/status")
def get_upload_status(user_uuid: str, chat_id: str, upload_id: str):
    received = storage.get_chunk_status(user_uuid, chat_id, upload_id)
    return {"upload_id": upload_id, "received_bytes": received}


@app.put("/files/chunk")
async def upload_file_chunk(
    user_uuid: str,
    chat_id: str,
    upload_id: str,
    offset: int,
    request: Request,
):
    try:
        chunk_data = await request.body()
        received = storage.write_chunk(
            user_uuid, chat_id, upload_id, offset, chunk_data
        )
        return {"upload_id": upload_id, "received_bytes": received}
    except ValueError as e:
        raise HTTPException(409, str(e))


@app.post("/files/complete")
async def complete_chunked_upload(req: CompleteUploadRequest):
    try:
        file_id = await asyncio.to_thread(
            storage.finalize_chunked_upload,
            req.user_uuid,
            req.chat_id,
            req.upload_id,
            req.filename,
        )
        return {
            "file_id": file_id,
            "media_type": detect_media_type(req.filename),
        }
    except FileNotFoundError as e:
        raise HTTPException(404, str(e))
    except ValueError as e:
        raise HTTPException(422, str(e))


@app.delete("/files/{user_uuid}/{chat_id}/{file_id}")
def delete_uncommitted_file(user_uuid: str, chat_id: str, file_id: str):
    if db.is_file_attached(user_uuid, chat_id, file_id):
        raise HTTPException(
            409, "Cannot delete file already committed to chat history"
        )

    if storage.delete_file(user_uuid, chat_id, file_id):
        return {"status": "deleted", "file_id": file_id}
    raise HTTPException(404, "File not found")


@app.get("/files/{user_uuid}/{chat_id}/{file_id}")
def get_file(user_uuid: str, chat_id: str, file_id: str):
    file_path = storage.get_file_path(user_uuid, chat_id, file_id)
    if not file_path:
        raise HTTPException(404, "File not found")
    return FileResponse(file_path)


# --- Состояние сервера и модели ---


@app.get("/state")
def get_state():
    return {
        "status": manager.status,
        "active_model": manager.active_model_id,
        "capabilities": manager.active_capabilities,
        "text_only": manager.text_only,
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
            latest_rev = (
                max(repo.revisions, key=lambda r: r.last_modified)
                if repo.revisions
                else None
            )
            caps = detect_model_capabilities(
                latest_rev.snapshot_path if latest_rev else None
            )
            models.append({
                "id": repo.repo_id,
                "size_mb": round(repo.size_on_disk / (1024 * 1024), 2),
                "capabilities": caps,
            })
    return {"models": models}


@app.post("/models", status_code=202)
async def download_model(req: DownloadModelRequest):
    if manager.status not in [ServerStatus.IDLE, ServerStatus.LOADED]:
        raise HTTPException(409, f"Server is busy: {manager.status}")

    manager.status = ServerStatus.DOWNLOADING
    task_id = manager.create_task("download")
    asyncio.create_task(task_download(task_id, req.model_id))
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
async def load_model(req: LoadModelRequest):
    if manager.status != ServerStatus.IDLE:
        raise HTTPException(
            409, f"Server must be IDLE to load a model. Current: {manager.status}"
        )

    manager.status = ServerStatus.LOADING
    task_id = manager.create_task("load")
    asyncio.create_task(task_load(task_id, req))
    return {"task_id": task_id}


@app.post("/state/unload", status_code=202)
async def unload_model():
    if manager.status != ServerStatus.LOADED:
        raise HTTPException(
            409, f"Cannot unload in current state: {manager.status}"
        )

    manager.status = ServerStatus.UNLOADING
    task_id = manager.create_task("unload")
    asyncio.create_task(task_unload(task_id))
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
    asyncio.create_task(task_generate(task_id, req))
    return {"task_id": task_id}
