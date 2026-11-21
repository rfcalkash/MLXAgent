#include "apiclient.h"
#include <QJsonArray>
#include <QJsonDocument>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
    , m_chatModel(new ChatModel(this))
{
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ApiClient::pollCurrentTask);
    connect(this, &ApiClient::serverUrlChanged, this, &ApiClient::refreshAll);
}

void ApiClient::sendMessage(const QString& text)
{
    if (text.trimmed().isEmpty())
        return;

    m_chatModel->appendMessage({ .role = "user", .content = text });
    const QJsonArray msgArray = m_chatModel->toApiMessages();
    m_chatModel->appendMessage({ .role = "assistant" });

    send("POST", "/chat", { { "messages", msgArray }, { "max_tokens", 1024 } }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ApiClient::pollCurrentTask()
{
    if (m_currentTaskId.isEmpty()) {
        m_pollTimer->stop();
        return;
    }

    send("GET", "/tasks/" + m_currentTaskId, { }, [this](const QJsonObject& obj) {
        const QString status = obj["status"].toString();
        const QString type = obj["type"].toString();
        const QJsonObject result = obj["result"].toObject();

        if (type == "generate" && (status == "running" || status == "completed")) {
            const QString warning = (status == "completed" && !obj["error"].isNull())
                ? obj["error"].toString()
                : QString();

            m_chatModel->updateLastAssistantMessage(
                result["think"].toString(),
                result["answer"].toString(),
                warning);
        }

        if (status == "completed") {
            m_pollTimer->stop();
            m_currentTaskId.clear();
            refreshAll();
        } else if (status == "failed") {
            m_pollTimer->stop();
            m_currentTaskId.clear();
            emit errorOccurred("Ошибка задачи: " + obj["error"].toString());
            refreshAll();
        }
    });
}

bool ApiClient::isBusy() const
{
    return m_serverStatus == "DOWNLOADING" || m_serverStatus == "LOADING" || m_serverStatus == "GENERATING" || m_serverStatus == "UNLOADING" || m_serverStatus == "OFFLINE";
}

void ApiClient::setStatus(const QString& status)
{
    if (m_serverStatus != status) {
        m_serverStatus = status;
        emit serverStatusChanged();
    }
}

// --- Единая точка сетевых запросов ---

void ApiClient::send(const QString& method,
    const QString& endpoint,
    const QJsonObject& body,
    SuccessCb onSuccess,
    ErrorCb onError,
    const QUrlQuery& query)
{
    QUrl url(m_serverUrl + endpoint);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QByteArray payload = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);

    // sendCustomRequest поддерживает любые HTTP-методы (GET, POST, DELETE, PATCH...)
    QNetworkReply* reply = m_nam->sendCustomRequest(req, method.toUtf8(), payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        reply->deleteLater();

        const QByteArray rawData = reply->readAll();
        const QJsonObject jsonObj = QJsonDocument::fromJson(rawData).object();

        if (reply->error() != QNetworkReply::NoError) {
            // Если FastAPI вернул детальную ошибку (например, 409 Conflict с полем "detail")
            QString errMsg = jsonObj.contains("detail")
                ? jsonObj["detail"].toString()
                : reply->errorString();

            if (onError) {
                onError(errMsg);
            } else {
                emit errorOccurred(errMsg);
            }
            return;
        }

        if (onSuccess) {
            onSuccess(jsonObj);
        }
    });
}

// --- Бизнес-логика ---

void ApiClient::refreshAll()
{
    fetchState();
    fetchModels();
}

void ApiClient::fetchState()
{
    send("GET", "/state", { }, [this](const QJsonObject& res) {
             setStatus(res["status"].toString());
             if (m_activeModel != res["active_model"].toString()) {
                 m_activeModel = res["active_model"].toString();
                 emit activeModelChanged();
             } }, [this](const QString&) {
             // При недоступности сервера тихо переводим в OFFLINE без всплывающего окна ошибки
             setStatus("OFFLINE");
             if (!m_activeModel.isEmpty()) {
                 m_activeModel.clear();
                 emit activeModelChanged();
             } });
}

void ApiClient::fetchModels()
{
    send("GET", "/models", { }, [this](const QJsonObject& res) {
        const auto arr = res["models"].toArray();
        m_models.clear();
        for (const auto& val : arr) {
            m_models.append(val.toObject().toVariantMap());
        }
        emit modelsChanged();
    });
}

void ApiClient::downloadModel(const QString& repoId)
{
    send("POST", "/models", { { "model_id", repoId } }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ApiClient::deleteModel(const QString& modelId)
{
    QUrlQuery query;
    query.addQueryItem("id", modelId);

    send("DELETE", "/models", { }, [this](const QJsonObject&) { fetchModels(); }, nullptr, query);
}

void ApiClient::loadModel(const QString& modelId)
{
    send("POST", "/state/load", { { "model_id", modelId } }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ApiClient::unloadModel()
{
    send("POST", "/state/unload", { }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ApiClient::clearChat()
{
    m_chatModel->clear();
}

void ApiClient::startTaskPolling(const QString& taskId)
{
    m_currentTaskId = taskId;
    fetchState();
    m_pollTimer->start();
}
