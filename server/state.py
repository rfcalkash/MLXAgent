import asyncio
from collections import deque
import threading
from typing import Any, Optional
import uuid
from schemas import ChatRequest, ServerStatus


class ServerManager:

    def __init__(self):
        self.status: ServerStatus = ServerStatus.IDLE
        self.active_model_id: Optional[str] = None
        self.active_capabilities: list[str] = []
        self.is_vlm: bool = False
        self.text_only: bool = False
        self.model: Any = None
        self.processor: Any = None
        self.tasks: dict[str, dict] = {}

        # Очередь задач на генерацию и управление их остановкой
        self.generation_queue: deque[tuple[str, ChatRequest]] = deque()
        self.queue_condition: asyncio.Condition = asyncio.Condition()
        self.current_gen_task_id: Optional[str] = None
        self.current_gen_chat_id: Optional[str] = None
        self.cancel_event: threading.Event = threading.Event()

    def create_task(self, task_type: str, initial_status: str = "running") -> str:
        task_id = str(uuid.uuid4())
        self.tasks[task_id] = {
            "id": task_id,
            "type": task_type,
            "status": initial_status,
            "progress": 0.0,
            "result": None,
            "error": None,
        }
        return task_id

    def get_chat_active_task_id(self, chat_id: str) -> Optional[str]:
        if self.current_gen_chat_id == chat_id and self.current_gen_task_id:
            return self.current_gen_task_id
        for tid, req in self.generation_queue:
            if req.chat_id == chat_id:
                return tid
        return None


manager = ServerManager()
