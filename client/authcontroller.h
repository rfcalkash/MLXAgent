#pragma once

#include <QObject>
#include "PropertyMacros.h"
#include "resttransport.h"

class AuthController : public QObject {
    Q_OBJECT

    RO_REF_PROPERTY(QString, userUuid)
    RO_REF_PROPERTY(QString, userLogin)
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY userUuidChanged)

public:
    explicit AuthController(RestTransport* transport, QObject* parent = nullptr);

    bool isLoggedIn() const { return !m_userUuid.isEmpty(); }

    Q_INVOKABLE void registerUser(const QString& login, const QString& password);
    Q_INVOKABLE void loginUser(const QString& login, const QString& password);
    Q_INVOKABLE void logout();

private:
    void saveSession();
    void loadSession();

    RestTransport* m_transport;
};
