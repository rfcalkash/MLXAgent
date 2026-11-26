#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QStringList>
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
    RO_REF_PROPERTY(QStringList, unreadChatIds)

    Q_PROPERTY(QString activeChatTitle READ activeChatTitle NOTIFY activeChatTitleChanged)
    Q_PROPERTY(QVariantList activeDraftAttachments READ activeDraftAttachments NOTIFY activeDraftAttachmentsChanged)
    Q_PROPERTY(ChatModel* model READ model CONSTANT)
    Q_PROPERTY(bool isCurrentChatGenerating READ isCurrentChatGenerating NOTIFY activeTasksChanged)

public:
    using ToolsProvider = std::function<QJsonArray()>;

    explicit ChatController(RestTransport* transport, QObject* parent = nullptr);

    ChatModel* model() const { return m_chatModel; }
    QVariantList activeDraftAttachments() const;
    QString activeChatTitle() const;
    void setUserUuid(const QString& userUuid);
    void setToolsProvider(ToolsProvider provider);

    bool isCurrentChatGenerating() const;
    Q_INVOKABLE void stopCurrentChat();
    Q_INVOKABLE void forceStopServerGeneration();

    Q_INVOKABLE void fetchChats();
    Q_INVOKABLE void createNewChat();
    Q_INVOKABLE void selectChat(const QString& chatId);
    Q_INVOKABLE void deleteChat(const QString& chatId);

    Q_INVOKABLE void attachFile(const QUrl& fileUrl);
    Q_INVOKABLE void retryAttachment(const QString& localId);
    Q_INVOKABLE void removeDraftAttachment(const QString& localId);
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE static QString formatContent(const QString& content, const QColor& textColor = Qt::white, double fontSizePt = 11.0);

public slots:
    void onUploadProgress(const QString& localId, double progress);
    void onUploadSucceeded(const QString& localId, const QJsonObject& response);
    void onUploadFailed(const QString& localId, const QString& error);
    void onToolBatchCompleted(const QString& chatId, const QJsonArray& toolResultMessages);

signals:
    void activeDraftAttachmentsChanged();
    void activeChatTitleChanged();
    void errorOccurred(const QString& message);
    void generationStarted();
    void generationFinished();
    void activeTasksChanged();
    void toolCallsRequested(const QString& chatId, const QJsonArray& toolCalls);

private slots:
    void pollActiveTasks();

private:
    void sendChatPayload(const QString& targetChatId, const QJsonArray& msgArray);
    void markChatUnread(const QString& chatId);
    void markChatRead(const QString& chatId);
    int findDraftIndex(const QString& localId) const;
    QString buildFileUrl(const QString& chatId, const QString& fileId) const;

    RestTransport* m_transport;
    ChatModel* m_chatModel;
    QTimer* m_pollTimer;
    QString m_userUuid;
    ToolsProvider m_toolsProvider;

    // chatId -> taskId (фоновые и активные задачи генерации)
    QMap<QString, QString> m_activeTasks;
    // chatId -> глубина цепочки вызовов инструментов
    QMap<QString, int> m_toolTurnDepths;

    QVariantList m_allDrafts;
};
