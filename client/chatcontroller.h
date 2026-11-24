#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <functional>

#include "ChatModel.h"
#include "PropertyMacros.h"
#include "resttransport.h"

class ChatController : public QObject {
    Q_OBJECT

    RO_REF_PROPERTY(QVariantList, chats)
    RO_REF_PROPERTY(QString, activeChatId)

    Q_PROPERTY(QString activeChatTitle READ activeChatTitle NOTIFY activeChatTitleChanged)

    Q_PROPERTY(QVariantList activeDraftAttachments READ activeDraftAttachments NOTIFY activeDraftAttachmentsChanged)
    Q_PROPERTY(ChatModel* model READ model CONSTANT)

public:
    using ToolsProvider = std::function<QJsonArray()>;

    explicit ChatController(RestTransport* transport, QObject* parent = nullptr);

    ChatModel* model() const { return m_chatModel; }
    QVariantList activeDraftAttachments() const;
    void setUserUuid(const QString& userUuid);
    void setToolsProvider(ToolsProvider provider);

    Q_INVOKABLE void fetchChats();
    Q_INVOKABLE void createNewChat();
    Q_INVOKABLE void selectChat(const QString& chatId);
    Q_INVOKABLE void deleteChat(const QString& chatId);

    Q_INVOKABLE void attachFile(const QUrl& fileUrl);
    Q_INVOKABLE void retryAttachment(const QString& localId);
    Q_INVOKABLE void removeDraftAttachment(const QString& localId);
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE static QString formatContent(const QString& content, const QColor& textColor = Qt::white, double fontSizePt = 11.0);

    QString activeChatTitle() const;

public slots:
    void onUploadProgress(const QString& localId, double progress);
    void onUploadSucceeded(const QString& localId, const QJsonObject& response);
    void onUploadFailed(const QString& localId, const QString& error);
    void onToolBatchCompleted(const QString& chatId, const QJsonArray& toolResultMessages);

signals:
    void activeDraftAttachmentsChanged();
    void errorOccurred(const QString& message);
    void generationStarted();
    void generationFinished();
    void toolCallsRequested(const QString& chatId, const QJsonArray& toolCalls);

    void activeChatTitleChanged();

private slots:
    void pollGenerationTask();

private:
    void sendChatPayload(const QString& targetChatId);
    int findDraftIndex(const QString& localId) const;
    QString buildFileUrl(const QString& chatId, const QString& fileId) const;

    RestTransport* m_transport;
    ChatModel* m_chatModel;
    QTimer* m_pollTimer;
    QString m_userUuid;
    QString m_currentTaskId;
    ToolsProvider m_toolsProvider;
    int m_toolTurnDepth = 0;

    QVariantList m_allDrafts;
};
