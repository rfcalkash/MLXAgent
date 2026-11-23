#include "modelcontroller.h"
#include <QJsonArray>

ModelController::ModelController(RestTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ModelController::pollTask);
    connect(this, &ModelController::modelsChanged, this, &ModelController::updateActiveModelIndex);
    connect(this, &ModelController::activeModelChanged, this, &ModelController::updateActiveModelIndex);
}

bool ModelController::isBusy() const
{
    return m_serverStatus == "DOWNLOADING" || m_serverStatus == "LOADING"
           || m_serverStatus == "GENERATING" || m_serverStatus == "UNLOADING"
           || m_serverStatus == "OFFLINE";
}

void ModelController::setGenerating(bool generating)
{
    const QString next = generating ? "GENERATING" : "LOADED";
    if (m_serverStatus != next) {
        m_serverStatus = next;
        emit serverStatusChanged();
    }
}

void ModelController::updateActiveModelIndex()
{
    int index = -1;
    for (int i = 0; i < m_models.size(); ++i) {
        if (m_models[i].toMap().value("id").toString() == m_activeModel) {
            index = i;
            break;
        }
    }
    if (m_activeModelIndex != index) {
        m_activeModelIndex = index;
        emit activeModelIndexChanged();
    }
}

void ModelController::refresh()
{
    fetchState();
    fetchModels();
}

void ModelController::fetchState()
{
    m_transport->send("GET", "/state", { }, [this](const QJsonObject& res) {
        const QString status = res["status"].toString();
        if (m_serverStatus != status) {
            m_serverStatus = status;
            emit serverStatusChanged();
        }

        const QString model = res["active_model"].toString();
        if (m_activeModel != model) {
            m_activeModel = model;
            emit activeModelChanged();
        }

        QStringList caps;
        for (const auto& v : res["capabilities"].toArray()) {
            caps.append(v.toString());
        }
        if (m_capabilities != caps) {
            m_capabilities = caps;
            emit capabilitiesChanged();
        }

        const bool to = res["text_only"].toBool();
        if (m_textOnlyMode != to) {
            m_textOnlyMode = to;
            emit textOnlyModeChanged();
        }
    }, [this](const QString&) {
                          if (m_serverStatus != "OFFLINE") {
                              m_serverStatus = "OFFLINE";
                              emit serverStatusChanged();
                          }
                          if (!m_activeModel.isEmpty()) {
                              m_activeModel.clear();
                              emit activeModelChanged();
                          }
                          if (!m_capabilities.isEmpty()) {
                              m_capabilities.clear();
                              emit capabilitiesChanged();
                          }
                      });
}

void ModelController::fetchModels()
{
    m_transport->send("GET", "/models", { }, [this](const QJsonObject& res) {
        m_models.clear();
        for (const auto& val : res["models"].toArray()) {
            m_models.append(val.toObject().toVariantMap());
        }
        emit modelsChanged();
    });
}

void ModelController::downloadModel(const QString& repoId)
{
    m_transport->send("POST", "/models", { { "model_id", repoId } }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ModelController::deleteModel(const QString& modelId)
{
    QUrlQuery q;
    q.addQueryItem("id", modelId);
    m_transport->send("DELETE", "/models", { }, [this](const QJsonObject&) { fetchModels(); }, nullptr, q);
}

void ModelController::loadModel(const QString& modelId, bool textOnly)
{
    m_transport->send("POST", "/state/load", { { "model_id", modelId }, { "text_only", textOnly } }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ModelController::unloadModel()
{
    m_transport->send("POST", "/state/unload", { }, [this](const QJsonObject& res) {
        startTaskPolling(res["task_id"].toString());
    });
}

void ModelController::startTaskPolling(const QString& taskId)
{
    m_currentTaskId = taskId;
    fetchState();
    m_pollTimer->start();
}

void ModelController::pollTask()
{
    if (m_currentTaskId.isEmpty()) {
        m_pollTimer->stop();
        return;
    }

    m_transport->send("GET", "/tasks/" + m_currentTaskId, { }, [this](const QJsonObject& obj) {
        const QString status = obj["status"].toString();
        if (status == "completed") {
            m_pollTimer->stop();
            m_currentTaskId.clear();
            refresh();
        } else if (status == "failed") {
            m_pollTimer->stop();
            m_currentTaskId.clear();
            emit errorOccurred("Ошибка операции с моделью: " + obj["error"].toString());
            refresh();
        }
    });
}
