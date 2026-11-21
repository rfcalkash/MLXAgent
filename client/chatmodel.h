#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QVector>

struct ChatMessage {
    QString role;
    QString content;
    QString think;
    QString warning;
};

class ChatModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        RoleRole = Qt::UserRole + 1,
        ContentRole,
        ThinkRole,
        WarningRole
    };

    explicit ChatModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendMessage(const ChatMessage& msg);
    void updateLastAssistantMessage(const QString& think,
        const QString& content,
        const QString& warning = { });
    void clear();
    QJsonArray toApiMessages() const;

private:
    QVector<ChatMessage> m_items;
};
