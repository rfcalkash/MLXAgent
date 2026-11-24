#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "PropertyMacros.h"
#include "webfetcher.h"

class ToolController : public QObject {
    Q_OBJECT

    RW_VAL_PROPERTY_DEF(bool, agentEnabled, setAgentEnabled, true)
    RW_REF_PROPERTY(QString, workspaceRoot, setWorkspaceRoot)
    RW_REF_PROPERTY(QString, cmakePath, setCmakePath)
    RW_REF_PROPERTY(QString, qmllintPath, setQmllintPath)

    // Матрица разрешений: ключи -> "allow" | "ask" | "deny"
    // file_create, file_read, file_update, file_delete, dir_create, dir_read, dir_update, dir_delete, web_fetch, cmake_build, qmllint_check
    RO_REF_PROPERTY(QVariantMap, permissions)

    // Очередь запросов, ожидающих подтверждения пользователя в UI
    RO_REF_PROPERTY(QVariantList, pendingApprovals)

    Q_PROPERTY(bool cmakeValid READ cmakeValid NOTIFY cmakePathChanged)
    Q_PROPERTY(bool qmllintValid READ qmllintValid NOTIFY qmllintPathChanged)

public:
    explicit ToolController(WebFetcher* fetcher, QObject* parent = nullptr);

    bool cmakeValid() const;
    bool qmllintValid() const;

    QJsonArray enabledToolSchemas() const;

    Q_INVOKABLE void setWorkspaceUrl(const QUrl& folderUrl);
    Q_INVOKABLE void setCmakeUrl(const QUrl& fileUrl);
    Q_INVOKABLE void setQmllintUrl(const QUrl& fileUrl);
    Q_INVOKABLE void setPermission(const QString& toolName, const QString& mode);

    // Запуск пачки вызовов от ассистента
    void executeToolCalls(const QString& chatId, const QJsonArray& toolCalls);

    // Действия пользователя по плашке подтверждения (Ask)
    Q_INVOKABLE void approveCall(const QString& callId);
    Q_INVOKABLE void rejectCall(const QString& callId);

signals:
    void toolBatchCompleted(const QString& chatId, const QJsonArray& toolResultMessages);

private:
    struct PendingCall {
        QString chatId;
        QString callId;
        QString name;
        QJsonObject args;
    };

    void loadSettings();
    void saveSettings();
    void detectDefaultBinaries();

    void processNextCall();
    void runApprovedCall(const PendingCall& call);
    void finishCurrentCall(const QString& outputText);

    // Песочница путей
    bool resolveSandboxedPath(const QString& relPath, QString& outAbsPath, QString& outError) const;

    // Реализации инструментов
    QString execDateTime() const;
    QString execFileCreate(const QJsonObject& args);
    QString execFileRead(const QJsonObject& args) const;
    QString execFileUpdate(const QJsonObject& args);
    QString execFileDelete(const QJsonObject& args);
    QString execDirCreate(const QJsonObject& args);
    QString execDirRead(const QJsonObject& args) const;
    QString execDirUpdate(const QJsonObject& args);
    QString execDirDelete(const QJsonObject& args);
    void execWebFetch(const QJsonObject& args);
    void execCmakeBuild(const QJsonObject& args);
    void execQmllint(const QJsonObject& args);

    WebFetcher* m_fetcher;
    QString m_activeBatchChatId;
    QList<PendingCall> m_queue;
    QJsonArray m_batchResults;
    bool m_waitingUserApproval = false;
};
