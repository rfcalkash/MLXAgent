from enum import Enum
from typing import Optional
from pydantic import BaseModel


class ServerStatus(str, Enum):
    IDLE = "IDLE"
    DOWNLOADING = "DOWNLOADING"
    LOADING = "LOADING"
    LOADED = "LOADED"
    GENERATING = "GENERATING"
    UNLOADING = "UNLOADING"


class UserAuthRequest(BaseModel):
    login: str
    password: str


class CreateChatRequest(BaseModel):
    user_uuid: str
    title: Optional[str] = None


class CompleteUploadRequest(BaseModel):
    user_uuid: str
    chat_id: str
    upload_id: str  # Ожидаемый SHA-256 хэш всего файла в hex
    filename: str


class DownloadModelRequest(BaseModel):
    model_id: str


class LoadModelRequest(BaseModel):
    model_id: str
    text_only: bool = False


class Attachment(BaseModel):
    type: str  # "image", "video", "audio"
    file_id: str


class ChatMessageIn(BaseModel):
    role: str
    content: str
    attachments: list[Attachment] = []


class ChatRequest(BaseModel):
    user_uuid: str
    chat_id: str
    messages: list[ChatMessageIn]
    max_tokens: int = 1024
    temp: float = 0.7
    enable_thinking: bool = True
