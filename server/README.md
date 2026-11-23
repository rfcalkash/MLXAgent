# MLX Local Server & Cross-Platform Qt/QML Client

Локальная клиент-серверная система для запуска языковых и мультимодальных моделей (LLM / VLM) на компьютерах Apple Silicon (macOS) с кроссплатформенным нативным клиентом (macOS, Windows, Linux, iOS, Android).

---

## Часть 1. Сервер (Python + FastAPI + MLX)

Сервер работает на macOS, управляет жизненным циклом моделей в Unified Memory и хранит историю переписок и медиафайлов.

### 1.1. Ключевые возможности и архитектура

* **Двойной движок инференса (`mlx_lm` + `mlx_vlm`):**
  * Автоматически сканирует `config.json` и `preprocessor_config.json` скачанных моделей Hugging Face и определяет их модальности (`capabilities`: `["text"]`, `["text", "image"]`, `["text", "image", "video", "audio"]`).
  * Поддерживает принудительный режим **`text_only: true`** при загрузке: позволяет загружать мультимодальные модели (например, Gemma 4 или Qwen-VL) через быстрый текстовый движок `mlx_lm`, экономя Unified Memory и сохраняя максимальную скорость генерации текста.
* **Конечная машина состояний и взаимоблокировки:**
  * Состояния: `IDLE`, `DOWNLOADING`, `LOADING`, `LOADED`, `GENERATING`, `UNLOADING`.
  * Долгие операции выполняются асинхронно в фоновых потоках (`asyncio.to_thread`), мгновенно возвращая клиенту `task_id` (`HTTP 202 Accepted`).
  * Конфликтующие операции блокируются (`HTTP 409 Conflict`): нельзя выгрузить или удалить модель во время генерации, нельзя запустить две долгие задачи одновременно.
* **Умный потоковый парсер рассуждений (`<think>`):**
  * Генерация идет потоково (`stream_generate`), промежуточные срезы парсятся раз в 500 мс.
  * Поддерживает форматы Qwen3 / DeepSeek-R1 (`<think>...</think>`), Gemma 4 (`<|channel>thought...<channel|>`), gpt-oss, Mistral (`[THINK]`), Nemotron и др.
  * Разделяет ответ на `{ "think": "...", "answer": "..." }`, чтобы рассуждения не засоряли контекст следующих ходов. При достижении лимита `max_tokens` выставляет предупреждение `"Out of tokens"`.
* **Персистентность и файловое хранилище:**
  * **SQLite (`storage/chat_history.db`):** хранит пользователей (хэши паролей PBKDF2-SHA256), чаты, сообщения и привязки вложений.
  * **Авто-нейминг бесед:** после первого осмысленного обмена репликами модель сама генерирует короткий заголовок чата (игнорируя короткие приветствия).
  * **Иерархия файлов `storage/<user_uuid>/<chat_id>/<file_id>`:** поддерживает как быструю загрузку (`multipart/form-data`), так и чанковую дозагрузку с проверкой контрольной суммы **SHA-256** по завершении. Встроенный Diff-сборщик мусора очищает непривязанные файлы старше 6 часов.

### 1.2. Структура модулей сервера

```text
server/
├── storage/              # Создается автоматически (БД SQLite и папки пользователей)
├── schemas.py            # Pydantic-схемы запросов и Enum состояний
├── storage.py            # StorageManager: работа с ФС, чанками, SHA-256 и сборкой мусора
├── database.py           # DatabaseManager: SQLite (users, chats, messages, attachments)
├── utils.py              # Парсер тегов мыслей, сканер кэша HF и определение capabilities
├── state.py              # ServerManager: синглтон состояния GPU, загруженной модели и задач
├── workers.py            # Фоновые воркеры (download, load/unload, LM/VLM stream_generate)
└── main.py               # Точка входа FastAPI и REST-эндпойнты
```

### 1.3. Спецификация REST API

#### Пользователи и чаты

| Метод | Эндпойнт | Описание |
| :--- | :--- | :--- |
| `POST` | `/users/register` | Регистрация (`{"login", "password"}`) → `{"uuid", "login"}` |
| `POST` | `/users/login` | Авторизация → `{"uuid", "login"}` |
| `POST` | `/chats` | Создание нового чата (`{"user_uuid", "title"}`) → `{"id", "title", ...}` |
| `GET` | `/chats?user_uuid=...` | Список чатов пользователя (сортировка по обновлению) |
| `GET` | `/chats/{chat_id}` | Полная история сообщений чата с массивом `attachments` |
| `DELETE` | `/chats/{chat_id}?user_uuid=...` | Удаление чата из БД и удаление его папки с диска |

#### Файлы и чанковая дозагрузка

| Метод | Эндпойнт | Описание |
| :--- | :--- | :--- |
| `POST` | `/files?user_uuid=...&chat_id=...` | Быстрая загрузка файла (`multipart/form-data`) → `{"file_id", "media_type"}` |
| `GET` | `/files/status?user_uuid=...&chat_id=...&upload_id=...` | Проверка принятых байт (`received_bytes`) для возобновления загрузки |
| `PUT` | `/files/chunk?user_uuid=...&chat_id=...&upload_id=...&offset=...` | Загрузка бинарного чанка (`application/octet-stream`) |
| `POST` | `/files/complete` | Сверка SHA-256 (`upload_id`) и финализация файла (`HTTP 422` при битом хэше) |
| `DELETE` | `/files/{user_uuid}/{chat_id}/{file_id}` | Принудительное удаление непривязанного файла |
| `GET` | `/files/{user_uuid}/{chat_id}/{file_id}` | Скачивание/отображение файла (`FileResponse`) |

#### Модели, состояние и генерация

| Метод | Эндпойнт | Описание |
| :--- | :--- | :--- |
| `GET` | `/state` | Текущий статус (`IDLE`, `LOADED`, `GENERATING`...), `active_model`, `capabilities`, `text_only` |
| `GET` | `/tasks/{task_id}` | Статус фоновой задачи (`running`, `completed`, `failed`), прогресс и `result` |
| `GET` | `/models` | Список моделей из локального кэша HF с размером и `capabilities` |
| `POST` | `/models` | Скачать модель с HF (`{"model_id"}`) → `202` + `task_id` |
| `DELETE` | `/models?id=...` | Удалить модель из кэша HF (запрещено, если модель загружена) |
| `POST` | `/state/load` | Загрузить модель в память (`{"model_id", "text_only": bool}`) → `202` + `task_id` |
| `POST` | `/state/unload` | Выгрузить модель и очистить кэш Metal (`mx.metal.clear_cache()`) → `202` + `task_id` |
| `POST` | `/chat` | Отправить запрос в модель (`{"user_uuid", "chat_id", "messages", "max_tokens", "temp", "enable_thinking"}`) → `202` + `task_id` |

### 1.4. Установка и запуск сервера

Требуется macOS на Apple Silicon (M1/M2/M3/M4) и пакетный менеджер `uv`:

```bash
cd server
uv init --no-readme
uv add fastapi uvicorn mlx-lm mlx-vlm huggingface_hub python-multipart pillow
uv run uvicorn main:app --host 0.0.0.0 --port 8000
```

Интерактивная документация Swagger UI доступна после запуска по адресу `http://127.0.0`.

---

## Часть 2. Клиент (C++20 + Qt 6 + QML)

Кроссплатформенный клиент с адаптивным интерфейсом (десктоп + мобильные экраны), построенный по модульной архитектуре без блокировки GUI-потока.

### 2.1. Архитектура клиента (Топология «Звезда»)

Вся бизнес-логика разделена на независимые контроллеры, которые не знают друг о друге и коммутируются через единый фасад **`AppContext`**:

* **`RestTransport`:** низкоуровневый HTTP-диспетчер (`QNetworkAccessManager`), сохранение `serverUrl` в `QSettings`, парсинг ошибок `detail` от FastAPI. Владеет изолированным фоновым потоком `QThread`.
* **`FileUploader`:** stateless-воркер, работающий в отдельном `QThread`. Файлы до 2 МБ отправляет одним запросом, файлы больше 2 МБ хэширует в фоне (`SHA-256`), проверяет смещение на сервере и гонит чанками по 1 МБ с автоматической докачкой при обрывах связи.
* **`AuthController`:** управление аккаунтом (`login`, `register`, `logout`) и кэширование сессии в `QSettings`.
* **`ModelController`:** управление сервером и моделями, поллинг задач загрузки/скачивания, отслеживание `capabilities` и режима `textOnlyMode`.
* **`ChatController`:** управление списком чатов, история сообщений, поллинг генерации (1 раз в секунду) и общий пул черновиков вложений (`m_allDrafts`) с жесткой привязкой каждого файла к своему `chatId`.
* **`ChatModel` (`QAbstractListModel`):** реактивная модель списка сообщений с точечным обновлением (`dataChanged`) только последней карточки при стриминге, без пересоздания делегатов QML.
* **`MathFormatter`:** конвейер рендеринга текста: экранирует формулы LaTeX (`$ ... $`, `$$ ... $$`), конвертирует Markdown в HTML встроенным движком Qt (`QTextDocument::setMarkdown`) и рендерит формулы в векторный **SVG** (через `JKQTMathText` + `QSvgGenerator`) с кэшированием в памяти.

### 2.2. Структура проекта клиента

```text
client/
├── CMakeLists.txt           # Сборка Qt 6.10+, FetchContent для JKQTMathText
├── main.cpp                 # Точка входа QGuiApplication + QQmlApplicationEngine
├── PropertyMacros.h         # Конструктор макросов для Q_PROPERTY (RO/RW, REF/VAL)
├── fileuploader.h/.cpp      # Фоновый многопоточный загрузчик файлов (Simple + Chunked SHA-256)
├── resttransport.h/.cpp     # Сетевой слой и управление потоком загрузки
├── authcontroller.h/.cpp    # Контроллер авторизации и сессии
├── modelcontroller.h/.cpp   # Контроллер состояния GPU и моделей
├── chatmodel.h/.cpp         # QAbstractListModel для сообщений чата
├── chatcontroller.h/.cpp    # Контроллер бесед, черновиков вложений и поллинга генерации
├── mathformatter.h/.cpp     # Конвертер Markdown + LaTeX -> RichText + векторный SVG
├── appcontext.h/.cpp        # Корневой фасад-медиатор для QML
└── Main.qml                 # Адаптивный интерфейс (Drawer чатов, сворачиваемый header, лента превью)
```

### 2.3. Сборка и запуск клиента

#### Зависимости
* **Qt 6.5+** (протестировано на Qt 6.10 / 6.12) с модулями `Quick`, `Network`, `Svg`.
* **CMake 3.16+** и компилятор с поддержкой **C++20**.
* Доступ к интернету при первой конфигурации CMake (для автоматического скачивания `JKQTMathText` через `FetchContent`).

#### Сборка из терминала

```bash
cd client
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Также проект можно открыть напрямую через `CMakeLists.txt` в **Qt Creator** и собрать под нужный Kit (macOS, Desktop, Android или iOS).

#### Настройка для мобильных платформ (iOS / Android)
Для работы по незашифрованному `http://` в локальной сети при сборке под мобильные ОС необходимо разрешить локальный cleartext-трафик:
* **Android (`AndroidManifest.xml`):** добавить `android:usesCleartextTraffic="true"` в тег `<application>`.
* **iOS (`Info.plist`):** добавить ключ `NSAppTransportSecurity` → `NSAllowsLocalNetworking = YES`.

---

## Часть 3. Дорожная карта и будущие расширения (Roadmap)

### 3.1. Голосовой ввод (Hybrid STT: локальный `whisper.cpp` + серверный Omni)
Архитектура поддерживает два независимых сценария работы со звуком:
* **Локальное распознавание на клиенте (`whisper.cpp`):**
  * В C++ клиент встраивается библиотека `whisper.cpp` (через `FetchContent` в CMake) с аппаратным ускорением (Metal/CoreML на Apple, NEON на Android).
  * Запись с микрофона идет через нативный модуль `Qt6::Multimedia` (`QAudioSource` в PCM 16 kHz mono), распознавание выполняется в фоновом `QThread` (по аналогии с `FileUploader`).
  * **Преимущества:** работает офлайн, не тратит мобильный трафик на передачу аудиофайлов и не занимает Unified Memory на сервере. Идеально для надиктовки промптов любым текстовым моделям.
* **Нативный мультимодальный звук (Server Omni):**
  * Если загружена модель с возможностью `"audio"` (например, Gemma 4, Qwen-Omni, Phi-4 Multimodal), клиент может отправить сырой аудиофайл через существующий чанковый `FileUploader` (`attachments: [{"type": "audio", "file_id": "..."}]`) для анализа интонаций, музыки или звуков окружения.

### 3.2. Вызов инструментов и агентские функции (Tool Calling)
Переход от простого диалога к локальному ИИ-агенту без изменения базового транспортного протокола:
* **На стороне сервера:**
  * Расширение схемы `ChatRequest` полем `tools: list[dict]` (JSON Schema доступных инструментов) и поддержкой роли `tool` в истории `messages`.
  * Парсинг вызовов инструментов из вывода модели (теги `<tool_call>...</tool_call>` или нативные токены вызова функций в `mlx_lm` / `mlx_vlm`).
  * Возврат структурированного результата в задаче `/tasks/{task_id}`: `{"think": "...", "answer": "...", "tool_calls": [{"id": "...", "name": "...", "arguments": {...}}]}`.
* **На стороне клиента:**
  * Новый контроллер `ToolController` (подключаемый в звезду `AppContext`), регистрирующий локальные инструменты клиента (поиск по файлам, работа с календарем/буфером обмена, выполнение команд или веб-поиск) и отправляющий результат выполнения обратно в `/chat` с ролью `"tool"`.
  * Отображение плашек вызова инструментов и их результатов внутри пузырей `ChatModel` (с возможностью подтверждения опасных действий пользователем в UI).

### 3.3. Многовкладочный интерфейс (Multi-Tab Sessions)
* Расширение `ChatController` списком открытых вкладок (`openChats`) по аналогии с редакторами кода: каждая вкладка хранит свой недописанный текст (`draftText`), активный `chatId` и фоновые загрузки в общем пуле `m_allDrafts` с индикацией прогресса прямо на корешке вкладки.
