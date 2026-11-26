from enum import Enum
from typing import Any, Optional
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
    upload_id: str
    filename: str


class DownloadModelRequest(BaseModel):
    model_id: str


class LoadModelRequest(BaseModel):
    model_id: str
    text_only: bool = False


class Attachment(BaseModel):
    type: str
    file_id: str


class ChatMessageIn(BaseModel):
    role: str
    content: Optional[str] = ""
    attachments: list[Attachment] = []
    tool_calls: Optional[list[dict[str, Any]]] = None
    tool_call_id: Optional[str] = None


class ChatRequest(BaseModel):
    user_uuid: str
    chat_id: str
    messages: list[ChatMessageIn]
    tools: Optional[list[dict[str, Any]]] = None
    max_tokens: int = 16000
    temp: float = 0.7
    repetition_penalty: float = 1.05
    enable_thinking: bool = True


class StopChatRequest(BaseModel):
    chat_id: Optional[str] = None
