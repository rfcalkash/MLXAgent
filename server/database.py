from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import sqlite3
from typing import Any, Optional
import uuid

from storage import STORAGE_ROOT, storage

DB_PATH = STORAGE_ROOT / "chat_history.db"


class DatabaseManager:

    def __init__(self, db_path: Path = DB_PATH):
        self.db_path = db_path
        self._init_db()

    def _get_conn(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA foreign_keys = ON;")
        return conn

    def _init_db(self):
        with self._get_conn() as conn:
            conn.executescript("""
                CREATE TABLE IF NOT EXISTS users (
                    uuid TEXT PRIMARY KEY,
                    login TEXT UNIQUE NOT NULL,
                    password_hash TEXT NOT NULL,
                    created_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS chats (
                    id TEXT PRIMARY KEY,
                    user_uuid TEXT NOT NULL,
                    title TEXT NOT NULL,
                    created_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL,
                    FOREIGN KEY (user_uuid) REFERENCES users(uuid) ON DELETE CASCADE
                );

                CREATE TABLE IF NOT EXISTS messages (
                    id TEXT PRIMARY KEY,
                    chat_id TEXT NOT NULL,
                    role TEXT NOT NULL,
                    content TEXT NOT NULL,
                    think TEXT,
                    warning TEXT,
                    tool_calls TEXT,
                    tool_call_id TEXT,
                    created_at TEXT NOT NULL,
                    FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE
                );

                CREATE TABLE IF NOT EXISTS attachments (
                    id TEXT PRIMARY KEY,
                    message_id TEXT NOT NULL,
                    chat_id TEXT NOT NULL,
                    user_uuid TEXT NOT NULL,
                    file_id TEXT NOT NULL,
                    rel_path TEXT NOT NULL,
                    media_type TEXT NOT NULL,
                    created_at TEXT NOT NULL,
                    FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE,
                    FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE
                );
            """)

            # Мягкая миграция для уже существующей БД
            for col in ("tool_calls TEXT", "tool_call_id TEXT"):
                try:
                    conn.execute(f"ALTER TABLE messages ADD COLUMN {col};")
                except sqlite3.OperationalError:
                    pass

    # --- Пользователи ---

    @staticmethod
    def hash_password(password: str, salt: Optional[str] = None) -> str:
        salt = salt or os.urandom(16).hex()
        dk = hashlib.pbkdf2_hmac(
            "sha256", password.encode("utf-8"), salt.encode("utf-8"), 100_000
        )
        return f"{salt}${dk.hex()}"

    @classmethod
    def verify_password(cls, password: str, stored_hash: str) -> bool:
        try:
            salt, _ = stored_hash.split("$", 1)
            return cls.hash_password(password, salt) == stored_hash
        except ValueError:
            return False

    def create_user(self, login: str, password: str) -> dict:
        user_uuid = str(uuid.uuid4())
        now = datetime.utcnow().isoformat()
        pwd_hash = self.hash_password(password)

        with self._get_conn() as conn:
            conn.execute(
                "INSERT INTO users (uuid, login, password_hash, created_at) VALUES (?, ?, ?, ?)",
                (user_uuid, login, pwd_hash, now),
            )
        storage.ensure_user_dir(user_uuid)
        return {"uuid": user_uuid, "login": login}

    def authenticate_user(self, login: str, password: str) -> Optional[dict]:
        with self._get_conn() as conn:
            row = conn.execute(
                "SELECT uuid, login, password_hash FROM users WHERE login = ?",
                (login,),
            ).fetchone()
            if row and self.verify_password(password, row["password_hash"]):
                return {"uuid": row["uuid"], "login": row["login"]}
        return None

    # --- Чаты ---

    def create_chat(self, user_uuid: str, title: Optional[str] = None) -> dict:
        chat_id = str(uuid.uuid4())
        now = datetime.utcnow().isoformat()
        default_title = (
            title or f"Чат от {datetime.now().strftime('%d.%m %H:%M')}"
        )

        with self._get_conn() as conn:
            conn.execute(
                "INSERT INTO chats (id, user_uuid, title, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
                (chat_id, user_uuid, default_title, now, now),
            )
        storage.ensure_chat_dir(user_uuid, chat_id)
        return {"id": chat_id, "user_uuid": user_uuid, "title": default_title}

    def update_chat_title(self, chat_id: str, title: str):
        now = datetime.utcnow().isoformat()
        with self._get_conn() as conn:
            conn.execute(
                "UPDATE chats SET title = ?, updated_at = ? WHERE id = ?",
                (title, now, chat_id),
            )

    def get_user_chats(self, user_uuid: str) -> list[dict]:
        with self._get_conn() as conn:
            rows = conn.execute(
                "SELECT id, title, created_at, updated_at FROM chats WHERE user_uuid = ? ORDER BY updated_at DESC",
                (user_uuid,),
            ).fetchall()
            return [dict(r) for r in rows]

    def delete_chat(self, chat_id: str, user_uuid: str):
        with self._get_conn() as conn:
            conn.execute(
                "DELETE FROM chats WHERE id = ? AND user_uuid = ?",
                (chat_id, user_uuid),
            )
        storage.delete_chat_dir(user_uuid, chat_id)

    # --- Сообщения и артефакты ---

    def add_message(
        self,
        chat_id: str,
        user_uuid: str,
        role: str,
        content: str,
        think: Optional[str] = None,
        warning: Optional[str] = None,
        attachments: Optional[list[dict]] = None,
        tool_calls: Optional[list[dict[str, Any]]] = None,
        tool_call_id: Optional[str] = None,
    ) -> str:
        msg_id = str(uuid.uuid4())
        now = datetime.utcnow().isoformat()
        tool_calls_json = (
            json.dumps(tool_calls, ensure_ascii=False) if tool_calls else None
        )

        with self._get_conn() as conn:
            conn.execute(
                """INSERT INTO messages (id, chat_id, role, content, think, warning, tool_calls, tool_call_id, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    msg_id,
                    chat_id,
                    role,
                    content,
                    think,
                    warning,
                    tool_calls_json,
                    tool_call_id,
                    now,
                ),
            )
            conn.execute(
                "UPDATE chats SET updated_at = ? WHERE id = ?", (now, chat_id)
            )

            if attachments:
                for att in attachments:
                    rel_path = storage.get_relative_path(
                        user_uuid, chat_id, att["file_id"]
                    )
                    conn.execute(
                        """INSERT INTO attachments (id, message_id, chat_id, user_uuid, file_id, rel_path, media_type, created_at)
                           VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
                        (
                            str(uuid.uuid4()),
                            msg_id,
                            chat_id,
                            user_uuid,
                            att["file_id"],
                            rel_path,
                            att["type"],
                            now,
                        ),
                    )
        return msg_id

    def get_chat_messages(self, chat_id: str) -> list[dict]:
        with self._get_conn() as conn:
            msgs = conn.execute(
                """SELECT id, role, content, think, warning, tool_calls, tool_call_id, created_at
                   FROM messages WHERE chat_id = ? ORDER BY created_at ASC""",
                (chat_id,),
            ).fetchall()

            result = []
            for m in msgs:
                msg_dict = dict(m)
                if msg_dict["tool_calls"]:
                    msg_dict["tool_calls"] = json.loads(msg_dict["tool_calls"])
                atts = conn.execute(
                    "SELECT file_id, media_type as type, rel_path FROM attachments WHERE message_id = ?",
                    (m["id"],),
                ).fetchall()
                msg_dict["attachments"] = [dict(a) for a in atts]
                result.append(msg_dict)
            return result

    def is_file_attached(
        self, user_uuid: str, chat_id: str, file_id: str
    ) -> bool:
        with self._get_conn() as conn:
            row = conn.execute(
                "SELECT 1 FROM attachments WHERE user_uuid = ? AND chat_id = ? AND file_id = ?",
                (user_uuid, chat_id, file_id),
            ).fetchone()
            return row is not None

    def get_committed_file_ids(self, user_uuid: str, chat_id: str) -> set[str]:
        with self._get_conn() as conn:
            rows = conn.execute(
                "SELECT file_id FROM attachments WHERE user_uuid = ? AND chat_id = ?",
                (user_uuid, chat_id),
            ).fetchall()
            return {r["file_id"] for r in rows}


db = DatabaseManager()
