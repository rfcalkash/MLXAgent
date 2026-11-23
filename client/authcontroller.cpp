#include "authcontroller.h"
#include <QSettings>

AuthController::AuthController(RestTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    loadSession();
}

void AuthController::loadSession()
{
    QSettings s("MlxLocal", "Client");
    m_userUuid = s.value("userUuid").toString();
    m_userLogin = s.value("userLogin").toString();
}

void AuthController::saveSession()
{
    QSettings s("MlxLocal", "Client");
    s.setValue("userUuid", m_userUuid);
    s.setValue("userLogin", m_userLogin);
}

void AuthController::registerUser(const QString& login, const QString& password)
{
    m_transport->send("POST", "/users/register", { { "login", login }, { "password", password } }, [this](const QJsonObject& res) {
        m_userUuid = res["uuid"].toString();
        m_userLogin = res["login"].toString();
        saveSession();
        emit userUuidChanged();
        emit userLoginChanged();
    });
}

void AuthController::loginUser(const QString& login, const QString& password)
{
    m_transport->send("POST", "/users/login", { { "login", login }, { "password", password } }, [this](const QJsonObject& res) {
        m_userUuid = res["uuid"].toString();
        m_userLogin = res["login"].toString();
        saveSession();
        emit userUuidChanged();
        emit userLoginChanged();
    });
}

void AuthController::logout()
{
    m_userUuid.clear();
    m_userLogin.clear();
    saveSession();
    emit userUuidChanged();
    emit userLoginChanged();
}
