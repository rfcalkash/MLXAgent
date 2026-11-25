import asyncio
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
        self.generation_queue: asyncio.Queue[tuple[str, ChatRequest]] = asyncio.Queue()

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


manager = ServerManager()
