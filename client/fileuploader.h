#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrlQuery>
#include <functional>

struct UploadTask {
    QString localId;
    QString localPath;
    QString simpleUrl;
    QString statusUrl;
    QString chunkUrl;
    QString completeUrl;
    QUrlQuery queryParams;
    QJsonObject extraBody;
};
Q_DECLARE_METATYPE(UploadTask)

class FileUploader : public QObject {
    Q_OBJECT

public:
    explicit FileUploader(QObject* parent = nullptr);

public slots:
    void init();
    void startUpload(const UploadTask& task);

signals:
    void progressUpdated(const QString& localId, double progress);
    void uploadSucceeded(const QString& localId, const QJsonObject& response);
    void uploadFailed(const QString& localId, const QString& error);

private:
    using SuccessCb = std::function<void(const QJsonObject&)>;
    using ErrorCb = std::function<void(const QString&)>;

    void sendJson(const QString& method, const QString& urlStr, const QJsonObject& body, SuccessCb onSuccess, ErrorCb onError, const QUrlQuery& query = { });
    void uploadSimple(const UploadTask& task);
    void uploadNextChunk(const UploadTask& task, const QString& sha256Hex, qint64 offset);
    void finalizeChunked(const UploadTask& task, const QString& sha256Hex);

    QNetworkAccessManager* m_nam = nullptr;
};
