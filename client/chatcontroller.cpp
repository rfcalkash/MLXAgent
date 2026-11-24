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
    connect(m_pollTimer, &QTimer::timeout, this, &ChatController::pollGenerationTask);
    connect(this, &ChatController::activeChatIdChanged, this, &ChatController::activeDraftAttachmentsChanged);
    connect(this, &ChatController::activeChatIdChanged, this, &ChatController::activeChatTitleChanged);
    connect(this, &ChatController::chatsChanged, this, &ChatController::activeChatTitleChanged);
}

void ChatController::setToolsProvider(ToolsProvider provider)
{
    m_toolsProvider = std::move(provider);
}

QString ChatController::formatContent(const QString& content, const QColor& textColor, double fontSizePt)
{
    return MathFormatter::toRichHtml(content, textColor, fontSizePt);
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

void ChatController::setUserUuid(const QString& userUuid)
{
    if (m_userUuid == userUuid)
        return;
    m_userUuid = userUuid;

    m_chats.clear();
    m_activeChatId.clear();
    m_allDrafts.clear();
    m_chatModel->clear();

    emit chatsChanged();
    emit activeChatIdChanged();
    emit activeDraftAttachmentsChanged();

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
        foreach (const auto& v, res["chats"].toArray()) {
            m_chats.append(v.toObject().toVariantMap());
        }
        emit chatsChanged(); }, nullptr, q);
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
    emit activeChatIdChanged();

    m_transport->send("GET", "/chats/" + chatId, { }, [this, chatId](const QJsonObject& res) {
        m_chatModel->clear();
        foreach (const auto& val, res["messages"].toArray()) {
            const auto obj = val.toObject();
            QVariantList atts;
            foreach (const auto& a, obj["attachments"].toArray()) {
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

// --- Отправка сообщения, цикл Tool Calling и поллинг ответа ---

void ChatController::sendMessage(const QString& text)
{
    if (m_userUuid.isEmpty())
        return;
    if (text.trimmed().isEmpty() && activeDraftAttachments().isEmpty())
        return;
    m_toolTurnDepth = 0;
    auto dispatchChat = [this, text](const QString& targetChatId) {
        QVariantList readyAtts;
        for (int i = m_allDrafts.size() - 1; i >= 0; --i) {
            const auto map = m_allDrafts[i].toMap();
            if (map["chatId"].toString() == targetChatId && map["status"].toString() == "ready") {
                readyAtts.prepend(QVariantMap {
                    { "type", map["mediaType"].toString() },
                    { "file_id", map["fileId"].toString() },
                    { "url", map["remoteUrl"].toString() } });
                m_allDrafts.removeAt(i);
            }
        }
        emit activeDraftAttachmentsChanged();
        m_chatModel->appendMessage({ .role = "user",
            .content = text,
            .attachments = readyAtts });
        sendChatPayload(targetChatId);
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
void ChatController::sendChatPayload(const QString& targetChatId)
{
    const QJsonArray msgArray = m_chatModel->toApiMessages();
    m_chatModel->appendMessage({ .role = "assistant" });
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
    m_transport->send("POST", "/chat", body, [this](const QJsonObject& res) {
        m_currentTaskId = res["task_id"].toString();
        emit generationStarted();
        m_pollTimer->start();
    });
}
void ChatController::onToolBatchCompleted(const QString& chatId, const QJsonArray& toolResultMessages)
{
    if (chatId != m_activeChatId)
        return;
    for (const auto& val : toolResultMessages) {
        const auto obj = val.toObject();
        m_chatModel->appendMessage({ .role = "tool",
            .content = obj["content"].toString(),
            .toolCallId = obj["tool_call_id"].toString() });
    }
    m_toolTurnDepth++;
    sendChatPayload(chatId);
}
void ChatController::pollGenerationTask()
{
    if (m_currentTaskId.isEmpty()) {
        m_pollTimer->stop();
        return;
    }
    m_transport->send("GET", "/tasks/" + m_currentTaskId, { }, [this](const QJsonObject& obj) {
        const QString status = obj["status"].toString();
        const QString type = obj["type"].toString();
        const QJsonObject result = obj["result"].toObject();
        if (type == "generate" && (status == "running" || status == "completed")) {
            const QString warning = (status == "completed" && !obj["error"].isNull())
                ? obj["error"].toString()
                : QString();
            const QJsonArray toolCalls = result["tool_calls"].toArray();
            m_chatModel->updateLastAssistantMessage(
                result["think"].toString(),
                result["answer"].toString(),
                warning,
                toolCalls);
        }
        if (status == "completed") {
            m_pollTimer->stop();
            m_currentTaskId.clear();
            const QJsonArray toolCalls = result["tool_calls"].toArray();
            if (!toolCalls.isEmpty() && m_toolTurnDepth < MAX_TOOL_TURN_DEPTH) {
                emit toolCallsRequested(m_activeChatId, toolCalls);
            } else {
                fetchChats();
                emit generationFinished();
            }
        } else if (status == "failed") {
            m_pollTimer->stop();
            m_currentTaskId.clear();
            emit errorOccurred("Ошибка генерации: " + obj["error"].toString());
            emit generationFinished();
        }
    });
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
