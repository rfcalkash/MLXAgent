#include "webfetcher.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

WebFetcher::WebFetcher(RestTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
}

void WebFetcher::fetch(const QString& urlStr, FetchCallback callback)
{
    QUrl url(urlStr);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
        callback(false, "Invalid URL: must start with http:// or https://");
        return;
    }

    // Временный простой GET; позже будет заменен на headless QWebEngineView
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, nam, callback]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            callback(false, "HTTP error: " + reply->errorString());
            return;
        }

        QString text = QString::fromUtf8(reply->readAll());
        // Грубая очистка HTML-тегов для экономии контекста модели
        text.remove(QRegularExpression("<script[^>]*>.*?</script>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
        text.remove(QRegularExpression("<style[^>]*>.*?</style>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
        text.replace(QRegularExpression("<[^>]+>"), " ");
        text = text.simplified();

        if (text.size() > 15000) {
            text = text.left(15000) + "\n...[truncated]";
        }
        callback(true, text);
    });
}
