#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include "PropertyMacros.h"
#include "resttransport.h"

class ModelController : public QObject {
    Q_OBJECT

    RO_REF_PROPERTY_DEF(QString, serverStatus, "OFFLINE")
    RO_REF_PROPERTY(QString, activeModel)
    RO_REF_PROPERTY(QStringList, capabilities)
    RO_VAL_PROPERTY_DEF(bool, textOnlyMode, false)
    RO_REF_PROPERTY(QVariantList, models)
    RO_VAL_PROPERTY_DEF(int, activeModelIndex, -1)

    Q_PROPERTY(bool isBusy READ isBusy NOTIFY serverStatusChanged)

public:
    explicit ModelController(RestTransport* transport, QObject* parent = nullptr);

    bool isBusy() const;
    void setGenerating(bool generating);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void downloadModel(const QString& repoId);
    Q_INVOKABLE void deleteModel(const QString& modelId);
    Q_INVOKABLE void loadModel(const QString& modelId, bool textOnly = false);
    Q_INVOKABLE void unloadModel();

signals:
    void errorOccurred(const QString& message);

private slots:
    void pollTask();
    void updateActiveModelIndex();

private:
    void fetchState();
    void fetchModels();
    void startTaskPolling(const QString& taskId);

    RestTransport* m_transport;
    QTimer* m_pollTimer;
    QString m_currentTaskId;
};
