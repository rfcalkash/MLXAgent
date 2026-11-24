#include "ChatModel.h"
#include <QJsonObject>

ChatModel::ChatModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChatModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant ChatModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return { };

    const auto& msg = m_items.at(index.row());
    switch (role) {
    case RoleRole:
        return msg.role;
    case ContentRole:
        return msg.content;
    case ThinkRole:
        return msg.think;
    case WarningRole:
        return msg.warning;
    case AttachmentsRole:
        return msg.attachments;
    case ToolCallsRole:
        return msg.toolCalls.toVariantList();
    default:
        return { };
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { RoleRole, "role" },
        { ContentRole, "content" },
        { ThinkRole, "think" },
        { WarningRole, "warning" },
        { AttachmentsRole, "attachments" },
        { ToolCallsRole, "toolCalls" }
    };
    return roles;
}

void ChatModel::appendMessage(const ChatMessage& msg)
{
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(msg);
    endInsertRows();
}

void ChatModel::updateLastAssistantMessage(const QString& think,
    const QString& content,
    const QString& warning,
    const QJsonArray& toolCalls)
{
    if (m_items.isEmpty() || m_items.last().role != "assistant")
        return;

    const int lastRow = m_items.size() - 1;
    auto& msg = m_items[lastRow];

    msg.think = think;
    msg.content = content;
    if (!warning.isEmpty()) {
        msg.warning = warning;
    }
    if (!toolCalls.isEmpty()) {
        msg.toolCalls = toolCalls;
    }

    const QModelIndex idx = index(lastRow);
    emit dataChanged(idx, idx, { ContentRole, ThinkRole, WarningRole, ToolCallsRole });
}

void ChatModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

QJsonArray ChatModel::toApiMessages() const
{
    QJsonArray arr;
    for (const auto& msg : m_items) {
        if (msg.content.trimmed().isEmpty() && msg.attachments.isEmpty() && msg.toolCalls.isEmpty() && msg.toolCallId.isEmpty())
            continue;

        QJsonArray attArr;
        for (const auto& a : msg.attachments) {
            const auto map = a.toMap();
            attArr.append(QJsonObject {
                { "type", map["type"].toString() },
                { "file_id", map["file_id"].toString() } });
        }

        QJsonObject obj {
            { "role", msg.role },
            { "content", msg.content }
        };
        if (!attArr.isEmpty()) {
            obj["attachments"] = attArr;
        }
        if (!msg.toolCalls.isEmpty()) {
            obj["tool_calls"] = msg.toolCalls;
        }
        if (!msg.toolCallId.isEmpty()) {
            obj["tool_call_id"] = msg.toolCallId;
        }
        arr.append(obj);
    }
    return arr;
}
