import hashlib
import json
from pathlib import Path
import re
import uuid
from typing import Optional, Any
from huggingface_hub import scan_cache_dir
import mimetypes
import pymupdf  # PyMuPDF

MAX_VLM_PDF_IMAGE_PAGES = 10
MIN_SIGNIFICANT_IMG_PX = 150


class UnreadableScanPdfError(ValueError):
    """PDF состоит только из сканов без текстового слоя, а модель запущена в текстовом режиме."""


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


def detect_media_type(
    filename: str,
    content_type: str = "",
    file_path: Optional[Path] = None,
) -> str:
    head = b""
    if file_path and file_path.exists() and file_path.is_file():
        with file_path.open("rb") as f:
            head = f.read(4096)

    # 1. Проверка по магическим байтам (сигнатурам) в начале файла
    if head:
        if head.startswith(b"%PDF-"):
            return "document"

        # Изображения: PNG, JPEG, GIF, WebP, BMP
        if (
            head.startswith(b"\x89PNG\r\n\x1a\n")
            or head.startswith(b"\xff\xd8\xff")
            or head.startswith((b"GIF87a", b"GIF89a"))
            or head.startswith(b"BM")
            or (head.startswith(b"RIFF") and head[8:12] == b"WEBP")
        ):
            return "image"

        # Аудио: MP3, OGG, FLAC, WAV, M4A/M4B
        if (
            head.startswith((b"ID3", b"\xff\xfb", b"\xff\xf3", b"\xff\xf2"))
            or head.startswith((b"OggS", b"fLaC"))
            or (head.startswith(b"RIFF") and head[8:12] == b"WAVE")
            or (head[4:8] == b"ftyp" and head[8:12] in (b"M4A ", b"M4B "))
        ):
            return "audio"

        # Видео: MP4/MOV/M4V (ftyp), MKV/WebM (EBML), AVI (RIFF AVI), FLV, MPEG-TS
        if (
            head[4:8] == b"ftyp"
            or head.startswith(b"\x1a\x45\xdf\xa3")
            or (head.startswith(b"RIFF") and head[8:12] == b"AVI ")
            or head.startswith(b"FLV\x01")
            or (len(head) >= 377 and head[0] == 0x47 and head[188] == 0x47 and head[376] == 0x47)
        ):
            return "video"

    # 2. Проверка через системную базу mimetypes (исключая ложный video/mp2t для TypeScript .ts)
    guessed_mime, _ = mimetypes.guess_type(filename)
    mime = content_type or guessed_mime or ""
    if Path(filename).suffix.lower() == ".ts":
        mime = "text/typescript"

    if mime.startswith("image/"):
        return "image"
    if mime.startswith("audio/"):
        return "audio"
    if mime.startswith("video/"):
        return "video"
    if mime == "application/pdf" or mime.startswith("text/"):
        return "document"

    # 3. Эвристика текстового файла / исходного кода (без нулевых байтов и валидный UTF-8)
    if head and b"\x00" not in head:
        try:
            (head[:-3] if len(head) == 4096 else head).decode("utf-8")
            return "document"
        except UnicodeDecodeError:
            pass

    return "file"


def get_model_cache_path(model_id: str) -> Optional[Path]:
    cache_info = scan_cache_dir()
    for repo in cache_info.repos:
        if repo.repo_id == model_id and repo.revisions:
            latest_rev = max(repo.revisions, key=lambda r: r.last_modified)
            return latest_rev.snapshot_path
    return None


def detect_model_capabilities(snapshot_path: Optional[Path]) -> list[str]:
    caps = ["text", "document"]
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

    # 3. Gemma 4: <|tool_call>call:func_name{key:value}<tool_call|>
    if not tool_calls and "<|tool_call>" in answer:
        gemma_matches = list(
            re.finditer(
                r"<\|tool_call>call:(\w+)\{(.*?)\}<tool_call\|>",
                answer,
                flags=re.DOTALL,
            )
        )
        for m in gemma_matches:
            tool_calls.append({
                "id": f"call_{uuid.uuid4().hex[:8]}",
                "type": "function",
                "function": {
                    "name": m.group(1),
                    "arguments": _parse_gemma_args(m.group(2)),
                },
            })
        answer = re.sub(
            r"<\|tool_call>.*?<tool_call\|>", "", answer, flags=re.DOTALL
        ).strip()


    return {
        "think": parsed["think"],
        "answer": answer,
        "tool_calls": tool_calls,
    }

def _page_has_visual_content(page: pymupdf.Page) -> bool:
    for img in page.get_image_info():
        if img.get("width", 0) >= MIN_SIGNIFICANT_IMG_PX and img.get("height", 0) >= MIN_SIGNIFICANT_IMG_PX:
            return True
    if len(page.get_text().strip()) < 30 and len(page.get_drawings()) > 15:
        return True
    return False


def process_document_attachment(
    file_path: Path,
    is_vlm: bool,
    temp_dir: Optional[Path] = None,
) -> tuple[str, list[str], Optional[str]]:
    ext = file_path.suffix.lower()
    display_name = file_path.name

    # 1. Обычный текстовый файл / код
    if ext != ".pdf":
        raw_text = file_path.read_text(encoding="utf-8", errors="replace")
        if len(raw_text) > 100_000:
            raw_text = raw_text[:100_000] + "\n...[truncated]"
        return f'<file name="{display_name}">\n{raw_text}\n</file>', [], None

    # 2. PDF-документ
    doc = pymupdf.open(file_path)
    try:
        total_pages = len(doc)
        if total_pages == 0:
            return f'<file name="{display_name}">(пустой PDF)</file>', [], None

        # Режим А: Текстовая модель (mlx_lm или text_only=True)
        if not is_vlm:
            pages_text: list[str] = []
            skipped_visual_pages: list[int] = []

            for idx, page in enumerate(doc):
                page_num = idx + 1
                txt = page.get_text().strip()
                has_visual = _page_has_visual_content(page)

                if has_visual:
                    skipped_visual_pages.append(page_num)

                if txt:
                    note = " [Примечание: на странице есть изображения/схемы, недоступные в текстовом режиме]" if has_visual else ""
                    pages_text.append(f"--- Страница {page_num}{note} ---\n{txt}")
                elif has_visual:
                    pages_text.append(f"--- Страница {page_num} ---\n[Изображение/скан без текстового слоя]")

            has_any_real_text = any(len(doc[i].get_text().strip()) >= 15 for i in range(total_pages))
            if not has_any_real_text and skipped_visual_pages:
                raise UnreadableScanPdfError(
                    f"Файл '{display_name}' является сканом без текстового слоя. "
                    "Загрузите мультимодальную (VLM) модель без флага Text-only."
                )

            warning_msg = None
            if skipped_visual_pages:
                pages_str = ", ".join(map(str, skipped_visual_pages[:15]))
                if len(skipped_visual_pages) > 15:
                    pages_str += "..."
                warning_msg = f"В '{display_name}' пропущены изображения/сканы на стр.: {pages_str} (текстовый режим)"

            joined = "\n\n".join(pages_text)
            if len(joined) > 100_000:
                joined = joined[:100_000] + "\n...[truncated]"
            return f'<pdf name="{display_name}">\n{joined}\n</pdf>', [], warning_msg

        # Режим Б: Мультимодальная модель (mlx_vlm) — постраничный гибрид
        rendered_images: list[str] = []
        pages_parts: list[str] = []
        truncated_visual_pages: list[int] = []

        for idx, page in enumerate(doc):
            page_num = idx + 1
            txt = page.get_text().strip()
            has_visual = _page_has_visual_content(page)

            if has_visual and temp_dir is not None:
                if len(rendered_images) < MAX_VLM_PDF_IMAGE_PAGES:
                    pix = page.get_pixmap(dpi=150)
                    img_out = temp_dir / f"{file_path.stem}_p{page_num}.png"
                    pix.save(str(img_out))
                    rendered_images.append(str(img_out))
                    extra = f"\nТекст со страницы:\n{txt}" if txt else ""
                    pages_parts.append(f"--- Страница {page_num} [передана как изображение] ---{extra}")
                else:
                    truncated_visual_pages.append(page_num)
                    if txt:
                        pages_parts.append(f"--- Страница {page_num} ---\n{txt}")
            else:
                if txt:
                    pages_parts.append(f"--- Страница {page_num} ---\n{txt}")

        warning_msg = None
        if truncated_visual_pages:
            warning_msg = (
                f"В '{display_name}' отрендерены первые {MAX_VLM_PDF_IMAGE_PAGES} визуальных страниц "
                f"(пропущены изображения на стр. {truncated_visual_pages}–{truncated_visual_pages[-1]})"
            )

        joined = "\n\n".join(pages_parts)
        return f'<pdf name="{display_name}">\n{joined}\n</pdf>', rendered_images, warning_msg
    finally:
        doc.close()
