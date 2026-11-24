#pragma once

#include <QObject>
#include <QQmlEngine>

#include "authcontroller.h"
#include "chatcontroller.h"
#include "modelcontroller.h"
#include "resttransport.h"
#include "toolcontroller.h"
#include "webfetcher.h"

class AppContext : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(RestTransport* transport READ transport CONSTANT)
    Q_PROPERTY(AuthController* auth READ auth CONSTANT)
    Q_PROPERTY(ModelController* models READ models CONSTANT)
    Q_PROPERTY(ChatController* chat READ chat CONSTANT)
    Q_PROPERTY(ToolController* tools READ tools CONSTANT)

public:
    explicit AppContext(QObject* parent = nullptr);

    RestTransport* transport() const { return m_transport; }
    AuthController* auth() const { return m_auth; }
    ModelController* models() const { return m_models; }
    ChatController* chat() const { return m_chat; }
    ToolController* tools() const { return m_tools; }

    Q_INVOKABLE void initialize();

signals:
    void errorOccurred(const QString& message);

private:
    RestTransport* m_transport;
    WebFetcher* m_fetcher;
    AuthController* m_auth;
    ModelController* m_models;
    ChatController* m_chat;
    ToolController* m_tools;
};