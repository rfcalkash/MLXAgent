#include "chatcontroller.h"
#include "mathformatter.h"

#include <QFileInfo>
#include <QUuid>

namespace {
constexpr int MAX_TOOL_TURN_DEPTH = 8;
}

ChatController::ChatController(RestTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
    , m_chatModel(new ChatModel(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ChatController::pollActiveTasks);
    connect(this, &ChatController::activeChatIdChanged, this, &ChatController::activeDraftAttachmentsChanged);
    connect(this, &ChatController::activeChatIdChanged, this, &ChatController::activeChatTitleChanged);
    connect(this, &ChatController::chatsChanged, this, &ChatController::activeChatTitleChanged);
    connect(this, &ChatController::activeChatIdChanged, this, &ChatController::activeTasksChanged);
}

bool ChatController::isCurrentChatGenerating() const
{
    return !m_activeChatId.isEmpty() && m_activeTasks.contains(m_activeChatId);
}

void ChatController::stopCurrentChat()
{
    if (m_activeChatId.isEmpty())
        return;

    const QString targetChatId = m_activeChatId;
    m_transport->send("POST", "/chat/stop", { { "chat_id", targetChatId } }, [this, targetChatId](const QJsonObject& res) {
        if (res["target"].toString() == "queued") {
            m_activeTasks.remove(targetChatId);
            emit activeTasksChanged();
            emit activeTasksChanged();
            if (targetChatId == m_activeChatId) {
                m_chatModel->updateLastAssistantMessage(QString(), QString(), QStringLiteral("Отменено в очереди"));
            }
            if (m_activeTasks.isEmpty()) {
                emit generationFinished();
            }
        }
        // Если target == "running", таск сам завершится в течение ~50 мс с warning = "Stopped by user"
    });
}

void ChatController::forceStopServerGeneration()
{
    m_transport->send("POST", "/chat/stop", { { "chat_id", QJsonValue::Null } });
}

void ChatController::setToolsProvider(ToolsProvider provider)
{
    m_toolsProvider = std::move(provider);
}

QString ChatController::formatContent(const QString& content, const QColor& textColor, double fontSizePt)
{
    return MathFormatter::toRichHtml(content, textColor, fontSizePt);
}

QString ChatController::activeChatTitle() const
{
    if (m_activeChatId.isEmpty())
        return QStringLiteral("Новый чат");

    for (const auto& item : m_chats) {
        const auto map = item.toMap();
        if (map.value("id").toString() == m_activeChatId) {
            const QString title = map.value("title").toString();
            return title.isEmpty() ? QStringLiteral("Чат") : title;
        }
    }
    return QStringLiteral("Чат");
}

QVariantList ChatController::activeDraftAttachments() const
{
    if (m_activeChatId.isEmpty())
        return { };

    QVariantList filtered;
    for (const auto& item : m_allDrafts) {
        if (item.toMap().value("chatId").toString() == m_activeChatId) {
            filtered.append(item);
        }
    }
    return filtered;
}

void ChatController::markChatUnread(const QString& chatId)
{
    if (chatId.isEmpty() || chatId == m_activeChatId)
        return;

    if (!m_unreadChatIds.contains(chatId)) {
        m_unreadChatIds.append(chatId);
        emit unreadChatIdsChanged();
    }
}

void ChatController::markChatRead(const QString& chatId)
{
    if (m_unreadChatIds.removeAll(chatId) > 0) {
        emit unreadChatIdsChanged();
    }
}

void ChatController::setUserUuid(const QString& userUuid)
{
    if (m_userUuid == userUuid)
        return;
    m_userUuid = userUuid;

    m_chats.clear();
    m_activeChatId.clear();
    m_allDrafts.clear();
    m_unreadChatIds.clear();
    m_activeTasks.clear();
    m_toolTurnDepths.clear();
    m_pollTimer->stop();
    m_chatModel->clear();

    emit chatsChanged();
    emit activeChatIdChanged();
    emit activeDraftAttachmentsChanged();
    emit unreadChatIdsChanged();
    emit activeTasksChanged();

    if (!m_userUuid.isEmpty()) {
        fetchChats();
    }
}

QString ChatController::buildFileUrl(const QString& chatId, const QString& fileId) const
{
    return QString("%1/files/%2/%3/%4").arg(m_transport->serverUrl(), m_userUuid, chatId, fileId);
}

// --- Управление чатами ---

void ChatController::fetchChats()
{
    if (m_userUuid.isEmpty())
        return;
    QUrlQuery q;
    q.addQueryItem("user_uuid", m_userUuid);

    m_transport->send("GET", "/chats", { }, [this](const QJsonObject& res) {
        m_chats.clear();
        bool tasksUpdated = false;
        for (const auto& v : res["chats"].toArray()) {
            const auto obj = v.toObject();
            m_chats.append(obj.toVariantMap());

            const QString cid = obj["id"].toString();
            const QString activeTid = obj["active_task_id"].toString();
            if (!activeTid.isEmpty() && !m_activeTasks.contains(cid)) {
                m_activeTasks[cid] = activeTid;
                emit activeTasksChanged();
                tasksUpdated = true;
            }
        }
        emit chatsChanged();
        if (tasksUpdated) {
            emit activeTasksChanged();
            if (!m_pollTimer->isActive()) {
                m_pollTimer->start();
            }
        } }, nullptr, q);
}

void ChatController::createNewChat()
{
    if (m_userUuid.isEmpty())
        return;
    m_transport->send("POST", "/chats", { { "user_uuid", m_userUuid } }, [this](const QJsonObject& res) {
        m_activeChatId = res["id"].toString();
        m_chatModel->clear();
        emit activeChatIdChanged();
        fetchChats();
    });
}

void ChatController::selectChat(const QString& chatId)
{
    if (chatId.isEmpty() || m_activeChatId == chatId)
        return;

    m_activeChatId = chatId;
    markChatRead(chatId);
    emit activeChatIdChanged();

    m_transport->send("GET", "/chats/" + chatId, { }, [this, chatId](const QJsonObject& res) {
        if (m_activeChatId != chatId)
            return;

        m_chatModel->clear();
        for (const auto& val : res["messages"].toArray()) {
            const auto obj = val.toObject();
            QVariantList atts;
            for (const auto& a : obj["attachments"].toArray()) {
                const auto aObj = a.toObject();
                const QString fid = aObj["file_id"].toString();
                atts.append(QVariantMap {
                    { "type", aObj["type"].toString() },
                    { "file_id", fid },
                    { "url", buildFileUrl(chatId, fid) } });
            }
            m_chatModel->appendMessage({ .role = obj["role"].toString(),
                .content = obj["content"].toString(),
                .think = obj["think"].toString(),
                .warning = obj["warning"].toString(),
                .attachments = atts,
                .toolCalls = obj["tool_calls"].toArray(),
                .toolCallId = obj["tool_call_id"].toString() });
        }

        // Если в этом чате еще идет/стоит в очереди генерация — добавляем плейсхолдер ответа
        if (m_activeTasks.contains(chatId)) {
            m_chatModel->appendMessage({ .role = "assistant" });
        }
    });
}

void ChatController::deleteChat(const QString& chatId)
{
    if (m_userUuid.isEmpty())
        return;
    QUrlQuery q;
    q.addQueryItem("user_uuid", m_userUuid);

    m_transport->send("DELETE", "/chats/" + chatId, { }, [this, chatId](const QJsonObject&) {
        for (int i = m_allDrafts.size() - 1; i >= 0; --i) {
            if (m_allDrafts[i].toMap().value("chatId").toString() == chatId) {
                m_allDrafts.removeAt(i);
            }
        }
        emit activeDraftAttachmentsChanged();
        markChatRead(chatId);
        m_activeTasks.remove(chatId);
        emit activeTasksChanged();
        m_toolTurnDepths.remove(chatId);

        if (m_activeChatId == chatId) {
            m_activeChatId.clear();
            m_chatModel->clear();
            emit activeChatIdChanged();
        }
        fetchChats(); }, nullptr, q);
}

// --- Черновики вложений ---

int ChatController::findDraftIndex(const QString& localId) const
{
    for (int i = 0; i < m_allDrafts.size(); ++i) {
        if (m_allDrafts[i].toMap().value("localId").toString() == localId) {
            return i;
        }
    }
    return -1;
}

void ChatController::attachFile(const QUrl& fileUrl)
{
    if (m_userUuid.isEmpty())
        return;

    const QString localPath = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (fileUrl.isLocalFile() && !QFileInfo::exists(localPath)) {
        emit errorOccurred("Файл не найден: " + localPath);
        return;
    }

    auto startForChat = [this, fileUrl, localPath](const QString& targetChatId) {
        const QString localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_allDrafts.append(QVariantMap {
            { "localId", localId },
            { "chatId", targetChatId },
            { "localUrl", fileUrl.toString() },
            { "localPath", localPath },
            { "remoteUrl", "" },
            { "fileId", "" },
            { "mediaType", "image" },
            { "status", "uploading" },
            { "progress", 0.0 },
            { "error", "" } });
        if (targetChatId == m_activeChatId) {
            emit activeDraftAttachmentsChanged();
        }

        UploadTask task {
            .localId = localId,
            .localPath = localPath,
            .simpleUrl = "/files",
            .statusUrl = "/files/status",
            .chunkUrl = "/files/chunk",
            .completeUrl = "/files/complete",
            .queryParams = { { "user_uuid", m_userUuid }, { "chat_id", targetChatId } },
            .extraBody = { { "user_uuid", m_userUuid }, { "chat_id", targetChatId } }
        };
        m_transport->enqueueUpload(task);
    };

    if (m_activeChatId.isEmpty()) {
        m_transport->send("POST", "/chats", { { "user_uuid", m_userUuid } }, [this, startForChat](const QJsonObject& res) {
            m_activeChatId = res["id"].toString();
            emit activeChatIdChanged();
            fetchChats();
            startForChat(m_activeChatId);
        });
    } else {
        startForChat(m_activeChatId);
    }
}

void ChatController::retryAttachment(const QString& localId)
{
    const int idx = findDraftIndex(localId);
    if (idx < 0)
        return;

    QVariantMap item = m_allDrafts[idx].toMap();
    const QString targetChatId = item["chatId"].toString();
    item["status"] = "uploading";
    item["progress"] = 0.0;
    item["error"] = "";
    m_allDrafts[idx] = item;

    if (targetChatId == m_activeChatId) {
        emit activeDraftAttachmentsChanged();
    }

    UploadTask task {
        .localId = localId,
        .localPath = item["localPath"].toString(),
        .simpleUrl = "/files",
        .statusUrl = "/files/status",
        .chunkUrl = "/files/chunk",
        .completeUrl = "/files/complete",
        .queryParams = { { "user_uuid", m_userUuid }, { "chat_id", targetChatId } },
        .extraBody = { { "user_uuid", m_userUuid }, { "chat_id", targetChatId } }
    };
    m_transport->enqueueUpload(task);
}

void ChatController::removeDraftAttachment(const QString& localId)
{
    const int idx = findDraftIndex(localId);
    if (idx < 0)
        return;

    const QVariantMap item = m_allDrafts[idx].toMap();
    const QString targetChatId = item["chatId"].toString();
    const QString fileId = item["fileId"].toString();

    m_allDrafts.removeAt(idx);
    if (targetChatId == m_activeChatId) {
        emit activeDraftAttachmentsChanged();
    }

    if (!targetChatId.isEmpty() && !fileId.isEmpty()) {
        m_transport->send("DELETE", QString("/files/%1/%2/%3").arg(m_userUuid, targetChatId, fileId), { }, nullptr, [](const QString&) { });
    }
}
void ChatController::onUploadProgress(const QString& localId, double progress)
{
    const int idx = findDraftIndex(localId);
    if (idx < 0)
        return;
    QVariantMap item = m_allDrafts[idx].toMap();
    item["progress"] = progress;
    m_allDrafts[idx] = item;
    if (item["chatId"].toString() == m_activeChatId) {
        emit activeDraftAttachmentsChanged();
    }
}
void ChatController::onUploadSucceeded(const QString& localId, const QJsonObject& response)
{
    const QString fileId = response["file_id"].toString();
    const int idx = findDraftIndex(localId);
    if (idx < 0)
        return;
    QVariantMap item = m_allDrafts[idx].toMap();
    const QString targetChatId = item["chatId"].toString();
    item["status"] = "ready";
    item["progress"] = 1.0;
    item["fileId"] = fileId;
    item["mediaType"] = response["media_type"].toString();
    item["remoteUrl"] = buildFileUrl(targetChatId, fileId);
    m_allDrafts[idx] = item;
    if (targetChatId == m_activeChatId) {
        emit activeDraftAttachmentsChanged();
    }
}
void ChatController::onUploadFailed(const QString& localId, const QString& error)
{
    const int idx = findDraftIndex(localId);
    if (idx < 0)
        return;
    QVariantMap item = m_allDrafts[idx].toMap();
    item["status"] = "error";
    item["error"] = error;
    m_allDrafts[idx] = item;
    if (item["chatId"].toString() == m_activeChatId) {
        emit activeDraftAttachmentsChanged();
    }
    emit errorOccurred(error);
}
// --- Отправка сообщения (с поддержкой очереди), Tool Calling и поллинг ---
void ChatController::sendMessage(const QString& text)
{
    if (m_userUuid.isEmpty())
        return;
    if (text.trimmed().isEmpty() && activeDraftAttachments().isEmpty())
        return;
    auto dispatchChat = [this, text](const QString& targetChatId) {
        m_toolTurnDepths[targetChatId] = 0;
        QVariantList readyAtts;
        for (int i = m_allDrafts.size() - 1; i >= 0; --i) {
            const auto map = m_allDrafts[i].toMap();
            if (map["chatId"].toString() == targetChatId && map["status"].toString() == "ready") {
                readyAtts.prepend(QVariantMap {
                    { "type", map["mediaType"].toString() },
                    { "file_id", map["fileId"].toString() },
                    { "url", map["remoteUrl"].toString() },
                    { "filename", QFileInfo(map["localPath"].toString()).fileName() } });
                m_allDrafts.removeAt(i);
            }
        }

        if (targetChatId == m_activeChatId) {
            emit activeDraftAttachmentsChanged();
            m_chatModel->appendMessage({ .role = "user",
                .content = text,
                .attachments = readyAtts });
        }
        const QJsonArray msgArray = m_chatModel->toApiMessages();
        if (targetChatId == m_activeChatId) {
            m_chatModel->appendMessage({ .role = "assistant" });
        }
        sendChatPayload(targetChatId, msgArray);
    };
    if (m_activeChatId.isEmpty()) {
        m_transport->send("POST", "/chats", { { "user_uuid", m_userUuid } }, [this, dispatchChat](const QJsonObject& res) {
            m_activeChatId = res["id"].toString();
            emit activeChatIdChanged();
            fetchChats();
            dispatchChat(m_activeChatId);
        });
    } else {
        dispatchChat(m_activeChatId);
    }
}
void ChatController::sendChatPayload(const QString& targetChatId, const QJsonArray& msgArray)
{
    QJsonObject body {
        { "user_uuid", m_userUuid },
        { "chat_id", targetChatId },
        { "messages", msgArray },
        { "max_tokens", 16000 }
    };
    if (m_toolsProvider) {
        const QJsonArray tools = m_toolsProvider();
        if (!tools.isEmpty()) {
            body["tools"] = tools;
        }
    }
    m_transport->send("POST", "/chat", body, [this, targetChatId](const QJsonObject& res) {
        m_activeTasks[targetChatId] = res["task_id"].toString();
        emit activeTasksChanged();
        emit generationStarted();
        if (!m_pollTimer->isActive()) {
            m_pollTimer->start();
        }
    });
}
void ChatController::onToolBatchCompleted(const QString& chatId, const QJsonArray& toolResultMessages)
{
    // Подтягиваем актуальную историю чата из БД (даже если юзер переключил вкладку во время клика)
    m_transport->send("GET", "/chats/" + chatId, { }, [this, chatId, toolResultMessages](const QJsonObject& res) {
        QJsonArray historyArr;
        for (const auto& val : res["messages"].toArray()) {
            const auto obj = val.toObject();
            QJsonObject msgObj {
                { "role", obj["role"].toString() },
                { "content", obj["content"].toString() }
            };
            QJsonArray atts;
            for (const auto& a : obj["attachments"].toArray()) {
                const auto aObj = a.toObject();
                atts.append(QJsonObject { { "type", aObj["type"].toString() }, { "file_id", aObj["file_id"].toString() } });
            }
            if (!atts.isEmpty())
                msgObj["attachments"] = atts;
            if (!obj["tool_calls"].toArray().isEmpty())
                msgObj["tool_calls"] = obj["tool_calls"].toArray();
            if (!obj["tool_call_id"].toString().isEmpty())
                msgObj["tool_call_id"] = obj["tool_call_id"].toString();
            historyArr.append(msgObj);
        }
        for (const auto& tVal : toolResultMessages) {
            historyArr.append(tVal.toObject());
            if (chatId == m_activeChatId) {
                const auto tObj = tVal.toObject();
                m_chatModel->appendMessage({ .role = "tool",
                    .content = tObj["content"].toString(),
                    .toolCallId = tObj["tool_call_id"].toString() });
            }
        }
        if (chatId == m_activeChatId) {
            m_chatModel->appendMessage({ .role = "assistant" });
        }
        m_toolTurnDepths[chatId] = m_toolTurnDepths.value(chatId, 0) + 1;
        sendChatPayload(chatId, historyArr);
    });
}
void ChatController::pollActiveTasks()
{
    if (m_activeTasks.isEmpty()) {
        m_pollTimer->stop();
        return;
    }
    const auto keys = m_activeTasks.keys();
    for (const QString& chatId : keys) {
        const QString taskId = m_activeTasks.value(chatId);
        if (taskId.isEmpty())
            continue;
        m_transport->send("GET", "/tasks/" + taskId, { }, [this, chatId, taskId](const QJsonObject& obj) {
            // Защита от гонки: задача могла быть уже обработана предыдущим тиком
            if (m_activeTasks.value(chatId) != taskId)
                return;
            const QString status = obj["status"].toString();
            const QJsonObject result = obj["result"].toObject();
            if (chatId == m_activeChatId) {
                if (status == "queued") {
                    m_chatModel->updateLastAssistantMessage(QString(), QStringLiteral("⏳ В очереди на генерацию..."));
                } else if (status == "running" || status == "completed") {
                    const QString warning = (status == "completed" && !obj["error"].isNull())
                        ? obj["error"].toString()
                        : QString();
                    m_chatModel->updateLastAssistantMessage(
                        result["think"].toString(),
                        result["answer"].toString(),
                        warning,
                        result["tool_calls"].toArray());
                }
            }
            if (status == "completed") {
                m_activeTasks.remove(chatId);
                emit activeTasksChanged();
                const QJsonArray toolCalls = result["tool_calls"].toArray();
                const int depth = m_toolTurnDepths.value(chatId, 0);
                if (!toolCalls.isEmpty() && depth < MAX_TOOL_TURN_DEPTH) {
                    emit toolCallsRequested(chatId, toolCalls);
                } else {
                    if (obj["error"].toString() != QLatin1String("Stopped by user")) {
                        markChatUnread(chatId);
                    }
                    fetchChats();
                    if (m_activeTasks.isEmpty()) {
                        emit generationFinished();
                    }
                }
            } else if (status == "failed") {
                m_activeTasks.remove(chatId);
                emit activeTasksChanged();
                emit errorOccurred("Ошибка генерации: " + obj["error"].toString());
                if (m_activeTasks.isEmpty()) {
                    emit generationFinished();
                }
            }
        });
    }
}