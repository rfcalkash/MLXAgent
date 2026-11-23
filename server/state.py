from typing import Any, Optional
import uuid
from schemas import ServerStatus


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
