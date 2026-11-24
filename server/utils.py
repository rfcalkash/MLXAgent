import hashlib
import json
from pathlib import Path
import re
import uuid
from typing import Optional, Any
from huggingface_hub import scan_cache_dir

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

    for start_pat, _ in THINKING_PAIRS:
        open_match = re.match(rf"^{start_pat}(.*)", text, flags=re.DOTALL)
        if open_match:
            think_part = open_match.group(1).strip()
            return {"think": think_part if think_part else None, "answer": ""}

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


def compute_file_sha256(file_path: Path) -> str:
    hasher = hashlib.sha256()
    with file_path.open("rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest()


def detect_media_type(filename: str, content_type: str = "") -> str:
    ext = Path(filename).suffix.lower()
    if content_type.startswith("image/") or ext in (
        ".png",
        ".jpg",
        ".jpeg",
        ".webp",
    ):
        return "image"
    if content_type.startswith("video/") or ext in (".mp4", ".mov", ".mkv"):
        return "video"
    if content_type.startswith("audio/") or ext in (
        ".wav",
        ".mp3",
        ".m4a",
        ".ogg",
    ):
        return "audio"
    return "file"


def get_model_cache_path(model_id: str) -> Optional[Path]:
    cache_info = scan_cache_dir()
    for repo in cache_info.repos:
        if repo.repo_id == model_id and repo.revisions:
            latest_rev = max(repo.revisions, key=lambda r: r.last_modified)
            return latest_rev.snapshot_path
    return None


def detect_model_capabilities(snapshot_path: Optional[Path]) -> list[str]:
    caps = ["text"]
    if not snapshot_path or not snapshot_path.exists():
        return caps

    config_file = snapshot_path / "config.json"
    preproc_file = snapshot_path / "preprocessor_config.json"

    has_vision = preproc_file.exists()
    has_video = (snapshot_path / "video_preprocessor_config.json").exists()
    has_audio = False

    if config_file.exists():
        try:
            cfg = json.loads(config_file.read_text(encoding="utf-8"))
            if any(k in cfg for k in ("vision_config", "vision_tower")):
                has_vision = True
            if any(k in cfg for k in ("video_token_id", "temporal_patch_size")):
                has_video = True
            if any(k in cfg for k in ("audio_config", "audio_encoder")):
                has_audio = True
        except Exception:
            pass

    if has_vision:
        caps.append("image")
    if has_video:
        caps.append("video")
    if has_audio:
        caps.append("audio")
    return caps

def _parse_gemma_args(raw_args: str) -> dict[str, Any]:
    """Парсит аргументы Gemma 4 формата key:value, key2:"str" в dict."""
    args: dict[str, Any] = {}
    # Простой парсер пар ключ:значение с учетом кавычек
    pattern = re.compile(r'(\w+)\s*:\s*("(?:\\.|[^"])*"|\'(?:\\.|[^\'])*\'|[^,}]+)')
    for m in pattern.finditer(raw_args):
        key = m.group(1)
        val_str = m.group(2).strip()
        if (val_str.startswith('"') and val_str.endswith('"')) or (
            val_str.startswith("'") and val_str.endswith("'")
        ):
            args[key] = val_str[1:-1].replace('\\"', '"').replace("\\n", "\n")
        elif val_str.lower() == "true":
            args[key] = True
        elif val_str.lower() == "false":
            args[key] = False
        else:
            try:
                args[key] = int(val_str) if "." not in val_str else float(val_str)
            except ValueError:
                args[key] = val_str
    return args


def parse_model_output(raw_text: str) -> dict[str, Any]:
    parsed = parse_thinking(raw_text)
    answer = parsed["answer"] or ""
    tool_calls: list[dict[str, Any]] = []

    # 1. Qwen / Hermes: <tool_call>{"name": "...", "arguments": {...}}</tool_call>
    qwen_matches = list(re.finditer(r"<tool_call>\s*(\{.*?\})\s*</tool_call>", answer, flags=re.DOTALL))
    if qwen_matches:
        for idx, m in enumerate(qwen_matches):
            try:
                data = json.loads(m.group(1))
                tool_calls.append({
                    "id": f"call_{uuid.uuid4().hex[:8]}",
                    "type": "function",
                    "function": {
                        "name": data.get("name", ""),
                        "arguments": data.get("arguments", {}),
                    },
                })
            except json.JSONDecodeError:
                pass
        answer = re.sub(r"<tool_call>.*?</tool_call>", "", answer, flags=re.DOTALL).strip()

    # 2. Mistral: [TOOL_CALLS] [{"name": "...", "arguments": {...}}]
    if not tool_calls and "[TOOL_CALLS]" in answer:
        m = re.search(r"\[TOOL_CALLS\]\s*(\[.*\])", answer, flags=re.DOTALL)
        if m:
            try:
                arr = json.loads(m.group(1))
                for item in arr:
                    tool_calls.append({
                        "id": f"call_{uuid.uuid4().hex[:8]}",
                        "type": "function",
                        "function": {
                            "name": item.get("name", ""),
                            "arguments": item.get("arguments", {}),
                        },
                    })
                answer = answer[: m.start()].strip()
            except json.JSONDecodeError:
                pass

    # 3. Gemma 4: <|call>call:func_name{key:value}<call|>
    if not tool_calls and "<|call>" in answer:
        gemma_matches = list(re.finditer(r"<\|call>call:(\w+)\{(.*?)\}<call\|>", answer, flags=re.DOTALL))
        for m in gemma_matches:
            tool_calls.append({
                "id": f"call_{uuid.uuid4().hex[:8]}",
                "type": "function",
                "function": {
                    "name": m.group(1),
                    "arguments": _parse_gemma_args(m.group(2)),
                },
            })
        answer = re.sub(r"<\|call>.*?<call\|>", "", answer, flags=re.DOTALL).strip()

    return {
        "think": parsed["think"],
        "answer": answer,
        "tool_calls": tool_calls,
    }