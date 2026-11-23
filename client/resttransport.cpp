#include "resttransport.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>

RestTransport::RestTransport(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_uploader(new FileUploader())
{
    qRegisterMetaType<UploadTask>("UploadTask");

    QSettings s("MlxLocal", "Client");
    m_serverUrl = s.value("serverUrl", "http://127.0.0.1:8000").toString();

    connect(this, &RestTransport::serverUrlChanged, this, [this]() {
        QSettings s("MlxLocal", "Client");
        s.setValue("serverUrl", m_serverUrl);
    });

    m_uploader->moveToThread(&m_uploadThread);
    connect(&m_uploadThread, &QThread::started, m_uploader, &FileUploader::init);
    connect(&m_uploadThread, &QThread::finished, m_uploader, &QObject::deleteLater);

    connect(this, &RestTransport::reqStartUpload, m_uploader, &FileUploader::startUpload);
    connect(m_uploader, &FileUploader::progressUpdated, this, &RestTransport::uploadProgress);
    connect(m_uploader, &FileUploader::uploadSucceeded, this, &RestTransport::uploadSucceeded);
    connect(m_uploader, &FileUploader::uploadFailed, this, &RestTransport::uploadFailed);

    m_uploadThread.start();
}

RestTransport::~RestTransport()
{
    m_uploadThread.quit();
    m_uploadThread.wait();
}

void RestTransport::send(const QString& method,
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
    QNetworkReply* reply = m_nam->sendCustomRequest(req, method.toUtf8(), payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        reply->deleteLater();
        const QJsonObject jsonObj = QJsonDocument::fromJson(reply->readAll()).object();

        if (reply->error() != QNetworkReply::NoError) {
            QString errMsg = jsonObj.contains("detail")
            ? jsonObj["detail"].toString()
            : reply->errorString();
            if (onError)
                onError(errMsg);
            else
                emit networkError(errMsg);
            return;
        }
        if (onSuccess)
            onSuccess(jsonObj);
    });
}

void RestTransport::enqueueUpload(UploadTask task)
{
    if (task.simpleUrl.startsWith('/'))
        task.simpleUrl.prepend(m_serverUrl);
    if (task.statusUrl.startsWith('/'))
        task.statusUrl.prepend(m_serverUrl);
    if (task.chunkUrl.startsWith('/'))
        task.chunkUrl.prepend(m_serverUrl);
    if (task.completeUrl.startsWith('/'))
        task.completeUrl.prepend(m_serverUrl);

    emit reqStartUpload(task);
}
