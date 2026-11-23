#include "fileuploader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkReply>

namespace {
constexpr qint64 CHUNK_THRESHOLD_BYTES = 2 * 1024 * 1024;
constexpr qint64 CHUNK_SIZE_BYTES = 1024 * 1024;
}

FileUploader::FileUploader(QObject* parent)
    : QObject(parent)
{
}

void FileUploader::init()
{
    m_nam = new QNetworkAccessManager(this);
}

void FileUploader::sendJson(const QString& method, const QString& urlStr, const QJsonObject& body, SuccessCb onSuccess, ErrorCb onError, const QUrlQuery& query)
{
    QUrl url(urlStr);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray payload = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_nam->sendCustomRequest(req, method.toUtf8(), payload);
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QJsonObject jsonObj = QJsonDocument::fromJson(reply->readAll()).object();

        if (reply->error() != QNetworkReply::NoError) {
            QString errMsg = jsonObj.contains("detail") ? jsonObj["detail"].toString() : reply->errorString();
            if (onError)
                onError(errMsg);
            return;
        }
        if (onSuccess)
            onSuccess(jsonObj);
    });
}

void FileUploader::startUpload(const UploadTask& task)
{
    QFileInfo info(task.localPath);
    if (!info.exists() || !info.isFile()) {
        emit uploadFailed(task.localId, "Файл не найден: " + task.localPath);
        return;
    }

    if (info.size() <= CHUNK_THRESHOLD_BYTES || task.chunkUrl.isEmpty()) {
        uploadSimple(task);
        return;
    }

    QFile file(task.localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit uploadFailed(task.localId, "Не удалось открыть файл");
        return;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        emit uploadFailed(task.localId, "Ошибка вычисления SHA-256");
        return;
    }
    const QString sha256Hex = QString::fromLatin1(hash.result().toHex());

    QUrlQuery q = task.queryParams;
    q.addQueryItem("upload_id", sha256Hex);

    sendJson("GET", task.statusUrl, { }, [this, task, sha256Hex](const QJsonObject& res) {
        const qint64 offset = res["received_bytes"].toVariant().toLongLong();
        uploadNextChunk(task, sha256Hex, offset);
    }, [this, task](const QString& err) {
                 emit uploadFailed(task.localId, err);
             }, q);
}

void FileUploader::uploadSimple(const UploadTask& task)
{
    auto* file = new QFile(task.localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        emit uploadFailed(task.localId, "Не удалось открыть файл");
        return;
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    file->setParent(multiPart);

    QHttpPart filePart;
    QMimeDatabase dbMime;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, dbMime.mimeTypeForFile(task.localPath).name());
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"file\"; filename=\"%1\"").arg(QFileInfo(task.localPath).fileName()));
    filePart.setBodyDevice(file);
    multiPart->append(filePart);

    QUrl url(task.simpleUrl);
    if (!task.queryParams.isEmpty()) {
        url.setQuery(task.queryParams);
    }

    QNetworkReply* reply = m_nam->post(QNetworkRequest(url), multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::uploadProgress, this, [this, id = task.localId](qint64 sent, qint64 total) {
        if (total > 0) {
            emit progressUpdated(id, static_cast<double>(sent) / total);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, id = task.localId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFailed(id, "Ошибка загрузки: " + reply->errorString());
            return;
        }
        emit uploadSucceeded(id, QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void FileUploader::uploadNextChunk(const UploadTask& task, const QString& sha256Hex, qint64 offset)
{
    QFile file(task.localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit uploadFailed(task.localId, "Ошибка чтения файла при передаче чанка");
        return;
    }

    const qint64 totalSize = file.size();
    if (offset >= totalSize) {
        finalizeChunked(task, sha256Hex);
        return;
    }

    file.seek(offset);
    const QByteArray chunk = file.read(CHUNK_SIZE_BYTES);
    file.close();

    QUrl url(task.chunkUrl);
    QUrlQuery q = task.queryParams;
    q.addQueryItem("upload_id", sha256Hex);
    q.addQueryItem("offset", QString::number(offset));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    QNetworkReply* reply = m_nam->put(req, chunk);
    connect(reply, &QNetworkReply::finished, this, [this, reply, task, sha256Hex, totalSize]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFailed(task.localId, "Сбой передачи чанка: " + reply->errorString());
            return;
        }

        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        const qint64 received = obj["received_bytes"].toVariant().toLongLong();
        emit progressUpdated(task.localId, static_cast<double>(received) / totalSize);

        if (received < totalSize) {
            uploadNextChunk(task, sha256Hex, received);
        } else {
            finalizeChunked(task, sha256Hex);
        }
    });
}

void FileUploader::finalizeChunked(const UploadTask& task, const QString& sha256Hex)
{
    QJsonObject body = task.extraBody;
    body["upload_id"] = sha256Hex;
    body["filename"] = QFileInfo(task.localPath).fileName();

    sendJson("POST", task.completeUrl, body, [this, id = task.localId](const QJsonObject& obj) {
        emit uploadSucceeded(id, obj);
    }, [this, id = task.localId](const QString& err) {
                 emit uploadFailed(id, err);
             });
}
