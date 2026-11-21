#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantList>
#include <functional>

#include "ChatModel.h"
#include "PropertyMacros.h"

class ApiClient : public QObject {
    Q_OBJECT
    QML_ELEMENT

    RW_REF_PROPERTY_DEF(QString, serverUrl, setServerUrl, "http://127.0.0.1:8000")
    RO_REF_PROPERTY_DEF(QString, serverStatus, "OFFLINE")
    RO_REF_PROPERTY(QString, activeModel)
    RO_REF_PROPERTY(QVariantList, models)

    Q_PROPERTY(ChatModel* chatModel READ chatModel CONSTANT)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY serverStatusChanged)

public:
    explicit ApiClient(QObject* parent = nullptr);

    ChatModel* chatModel() const { return m_chatModel; }
    bool isBusy() const;

    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void downloadModel(const QString& repoId);
    Q_INVOKABLE void deleteModel(const QString& modelId);
    Q_INVOKABLE void loadModel(const QString& modelId);
    Q_INVOKABLE void unloadModel();
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void clearChat();

signals:
    void errorOccurred(const QString& message);

private slots:
    void pollCurrentTask();

private:
    using SuccessCb = std::function<void(const QJsonObject&)>;
    using ErrorCb = std::function<void(const QString&)>;

    void send(const QString& method,
        const QString& endpoint,
        const QJsonObject& body = { },
        SuccessCb onSuccess = nullptr,
        ErrorCb onError = nullptr,
        const QUrlQuery& query = { });

    void setStatus(const QString& status);
    void fetchState();
    void fetchModels();
    void startTaskPolling(const QString& taskId);

    QNetworkAccessManager* m_nam;
    QTimer* m_pollTimer;
    ChatModel* m_chatModel;
    QString m_currentTaskId;
};
