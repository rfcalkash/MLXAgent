#pragma once

#include "resttransport.h"
#include <QObject>
#include <functional>

class WebFetcher : public QObject {
    Q_OBJECT

public:
    using FetchCallback = std::function<void(bool success, const QString& contentOrError)>;

    explicit WebFetcher(RestTransport* transport, QObject* parent = nullptr);
    virtual ~WebFetcher() = default;

    // Точка расширения: сейчас делает легкий HTTP GET, в будущем заменится на QWebEngineView
    virtual void fetch(const QString& url, FetchCallback callback);

private:
    RestTransport* m_transport;
};