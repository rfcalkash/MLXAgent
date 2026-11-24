#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QThread>
#include <QUrlQuery>
#include <functional>

#include "PropertyMacros.h"
#include "fileuploader.h"

class RestTransport : public QObject {
    Q_OBJECT

    RW_REF_PROPERTY_DEF(QString, serverUrl, setServerUrl, "http://127.0.0.1:8000")

public:
    using SuccessCb = std::function<void(const QJsonObject&)>;
    using ErrorCb = std::function<void(const QString&)>;

    explicit RestTransport(QObject* parent = nullptr);
    ~RestTransport() override;

    void send(const QString& method,
        const QString& endpoint,
        const QJsonObject& body = { },
        SuccessCb onSuccess = nullptr,
        ErrorCb onError = nullptr,
        const QUrlQuery& query = { });

    void enqueueUpload(UploadTask task);

signals:
    void networkError(const QString& message);
    void unauthorized();
    void uploadProgress(const QString& localId, double progress);
    void uploadSucceeded(const QString& localId, const QJsonObject& response);
    void uploadFailed(const QString& localId, const QString& error);
    void reqStartUpload(const UploadTask& task);

private:
    QNetworkAccessManager* m_nam;
    QThread m_uploadThread;
    FileUploader* m_uploader = nullptr;
};
