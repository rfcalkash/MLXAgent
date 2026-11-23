#include "appcontext.h"

AppContext::AppContext(QObject* parent)
    : QObject(parent)
    , m_transport(new RestTransport(this))
    , m_auth(new AuthController(m_transport, this))
    , m_models(new ModelController(m_transport, this))
    , m_chat(new ChatController(m_transport, this))
{
    connect(m_transport, &RestTransport::networkError, this, &AppContext::errorOccurred);
    connect(m_models, &ModelController::errorOccurred, this, &AppContext::errorOccurred);
    connect(m_chat, &ChatController::errorOccurred, this, &AppContext::errorOccurred);

    connect(m_transport, &RestTransport::serverUrlChanged, m_models, &ModelController::refresh);
    connect(m_transport, &RestTransport::serverUrlChanged, m_chat, &ChatController::fetchChats);

    connect(m_auth, &AuthController::userUuidChanged, this, [this]() {
        m_chat->setUserUuid(m_auth->userUuid());
    });

    connect(m_transport, &RestTransport::uploadProgress, m_chat, &ChatController::onUploadProgress);
    connect(m_transport, &RestTransport::uploadSucceeded, m_chat, &ChatController::onUploadSucceeded);
    connect(m_transport, &RestTransport::uploadFailed, m_chat, &ChatController::onUploadFailed);

    connect(m_chat, &ChatController::generationStarted, this, [this]() {
        m_models->setGenerating(true);
    });
    connect(m_chat, &ChatController::generationFinished, m_models, &ModelController::refresh);
}

void AppContext::initialize()
{
    m_models->refresh();
    if (m_auth->isLoggedIn()) {
        m_chat->setUserUuid(m_auth->userUuid());
    }
}
