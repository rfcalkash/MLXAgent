import hashlib
from pathlib import Path
import shutil
import time
from typing import BinaryIO, Optional
import uuid

STORAGE_ROOT = Path(__file__).parent / "storage"
STORAGE_ROOT.mkdir(parents=True, exist_ok=True)

ORPHAN_TTL_SECONDS = 6 * 3600  # 6 часов защиты для незавершенных загрузок


class StorageManager:

    def __init__(self, root: Path = STORAGE_ROOT):
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)

    # --- Директории и пути ---

    def ensure_user_dir(self, user_uuid: str) -> Path:
        user_dir = self.root / user_uuid
        user_dir.mkdir(parents=True, exist_ok=True)
        return user_dir

    def ensure_chat_dir(self, user_uuid: str, chat_id: str) -> Path:
        chat_dir = self.root / user_uuid / chat_id
        chat_dir.mkdir(parents=True, exist_ok=True)
        return chat_dir

    def get_file_path(
        self, user_uuid: str, chat_id: str, file_id: str
    ) -> Optional[Path]:
        path = self.root / user_uuid / chat_id / file_id
        return path if path.exists() and path.is_file() else None

    def get_relative_path(
        self, user_uuid: str, chat_id: str, file_id: str
    ) -> str:
        return f"{user_uuid}/{chat_id}/{file_id}"

    # --- Обычная загрузка и удаление ---

    def save_stream(
        self, user_uuid: str, chat_id: str, filename: str, stream: BinaryIO
    ) -> str:
        ext = Path(filename).suffix.lower() or ".bin"
        file_id = f"{uuid.uuid4()}{ext}"
        dest_path = self.ensure_chat_dir(user_uuid, chat_id) / file_id

        with dest_path.open("wb") as buffer:
            shutil.copyfileobj(stream, buffer)

        return file_id

    def delete_file(self, user_uuid: str, chat_id: str, file_id: str) -> bool:
        path = self.root / user_uuid / chat_id / file_id
        if path.exists():
            path.unlink(missing_ok=True)
            return True
        return False

    def delete_chat_dir(self, user_uuid: str, chat_id: str):
        chat_dir = self.root / user_uuid / chat_id
        if chat_dir.exists():
            shutil.rmtree(chat_dir, ignore_errors=True)

    # --- Чанковая дозагрузка с проверкой SHA-256 ---

    def _get_part_path(
        self, user_uuid: str, chat_id: str, upload_id: str
    ) -> Path:
        return self.ensure_chat_dir(user_uuid, chat_id) / f".part_{upload_id}"

    def get_chunk_status(
        self, user_uuid: str, chat_id: str, upload_id: str
    ) -> int:
        part_path = self._get_part_path(user_uuid, chat_id, upload_id)
        return part_path.stat().st_size if part_path.exists() else 0

    def write_chunk(
        self,
        user_uuid: str,
        chat_id: str,
        upload_id: str,
        offset: int,
        data: bytes,
    ) -> int:
        part_path = self._get_part_path(user_uuid, chat_id, upload_id)
        current_size = part_path.stat().st_size if part_path.exists() else 0

        if offset > current_size:
            raise ValueError(
                f"Offset {offset} exceeds current received bytes {current_size}"
            )

        mode = "r+b" if part_path.exists() else "wb"
        with part_path.open(mode) as f:
            f.seek(offset)
            f.write(data)
            f.truncate()

        return part_path.stat().st_size

    @staticmethod
    def compute_sha256(file_path: Path) -> str:
        hasher = hashlib.sha256()
        with file_path.open("rb") as f:
            while chunk := f.read(65536):
                hasher.update(chunk)
        return hasher.hexdigest()

    def finalize_chunked_upload(
        self, user_uuid: str, chat_id: str, upload_id: str, filename: str
    ) -> str:
        part_path = self._get_part_path(user_uuid, chat_id, upload_id)
        if not part_path.exists():
            raise FileNotFoundError("Upload session not found")

        actual_sha256 = self.compute_sha256(part_path)
        if actual_sha256.lower() != upload_id.lower():
            part_path.unlink(missing_ok=True)
            raise ValueError(
                f"Checksum mismatch: expected {upload_id}, got {actual_sha256}. File deleted."
            )

        ext = Path(filename).suffix.lower() or ".bin"
        file_id = f"{uuid.uuid4()}{ext}"
        final_path = self.ensure_chat_dir(user_uuid, chat_id) / file_id
        part_path.replace(final_path)
        return file_id

    # --- Сборка мусора (Diff GC) ---

    def cleanup_orphans(
        self,
        user_uuid: str,
        chat_id: str,
        committed_file_ids: set[str],
        ttl_seconds: int = ORPHAN_TTL_SECONDS,
    ):
        chat_dir = self.root / user_uuid / chat_id
        if not chat_dir.exists():
            return

        now = time.time()
        for item in chat_dir.iterdir():
            if not item.is_file() or item.name in committed_file_ids:
                continue
            if now - item.stat().st_mtime > ttl_seconds:
                item.unlink(missing_ok=True)


storage = StorageManager()
