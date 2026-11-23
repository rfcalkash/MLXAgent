#include "mathformatter.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgGenerator>
#include <QTextDocument>
#include <cmath>

#include "jkqtmathtext/jkqtmathtext.h"

QString MathFormatter::toRichHtml(const QString& markdown, const QColor& textColor, double fontSizePt)
{
    if (markdown.isEmpty())
        return { };

    QString shielded = markdown;
    QStringList placeholders;

    // 1. Экранируем блочные формулы $$ ... $$ и \[ ... \]
    static const QRegularExpression blockRegex(R"(\$\$(.+?)\$\$|\\\[(.+?)\\\])",
        QRegularExpression::DotMatchesEverythingOption);
    auto blockIt = blockRegex.globalMatch(shielded);
    int offset = 0;
    while (blockIt.hasNext()) {
        auto match = blockIt.next();
        QString inner = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        QString key = QString("MATHPLACEHOLDER%1END").arg(placeholders.size());
        placeholders.append(renderLatexToImgTag(inner.trimmed(), textColor, fontSizePt * 1.15, true));

        shielded.replace(match.capturedStart() + offset, match.capturedLength(), key);
        offset += key.length() - match.capturedLength();
    }

    // 2. Экранируем строчные формулы $ ... $ и \( ... \)
    static const QRegularExpression inlineRegex(R"((?<!\\)\$([^\$\n]+?)(?<!\\)\$|\\\((.+?)\\\))");
    auto inlineIt = inlineRegex.globalMatch(shielded);
    offset = 0;
    while (inlineIt.hasNext()) {
        auto match = inlineIt.next();
        QString inner = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        QString key = QString("MATHPLACEHOLDER%1END").arg(placeholders.size());
        placeholders.append(renderLatexToImgTag(inner.trimmed(), textColor, fontSizePt, false));

        shielded.replace(match.capturedStart() + offset, match.capturedLength(), key);
        offset += key.length() - match.capturedLength();
    }

    // 3. Конвертируем Markdown -> HTML встроенным движком Qt (md4c)
    QTextDocument doc;
    doc.setMarkdown(shielded);
    QString html = doc.toHtml();

    // 4. Возвращаем готовые SVG-картинки вместо заглушек
    for (int i = 0; i < placeholders.size(); ++i) {
        QString key = QString("MATHPLACEHOLDER%1END").arg(i);
        html.replace(key, placeholders[i]);
    }

    return html;
}

QString MathFormatter::renderLatexToImgTag(const QString& latex,
    const QColor& color,
    double fontSizePt,
    bool isBlock)
{
    // Ключ кэша учитывает формулу, цвет и размер
    const QString cacheKey = QString("%1|%2|%3|%4")
                                 .arg(latex, color.name())
                                 .arg(fontSizePt)
                                 .arg(isBlock);

    static QHash<QString, QString> svgCache;
    if (auto it = svgCache.constFind(cacheKey); it != svgCache.constEnd()) {
        return it.value();
    }

    static const QString serifFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    static const QString sansFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    JKQTMathText mathText;
    mathText.useAnyUnicodeForTextOnly(serifFont, sansFont);
    mathText.setFontSize(fontSizePt);
    mathText.setFontColor(color);

    if (!mathText.parse(latex)) {
        // Если модель сгенерировала битый LaTeX — безопасно отдаем как моноширинный текст
        return QString("<code>%1</code>").arg(latex.toHtmlEscaped());
    }

    // Измеряем точные границы формулы на фиктивном QSvgGenerator
    QBuffer measureBuf;
    measureBuf.open(QIODevice::WriteOnly);
    QSvgGenerator measureGen;
    measureGen.setOutputDevice(&measureBuf);
    QPainter measurePainter(&measureGen);

    double width = 0, ascent = 0, descent = 0, strikeoutPos = 0;
    mathText.getSizeDetail(measurePainter, width, ascent, descent, strikeoutPos);
    measurePainter.end();

    const int w = std::max(1, static_cast<int>(std::ceil(width + 2.0)));
    const int h = std::max(1, static_cast<int>(std::ceil(ascent + descent + 2.0)));

    // Рендерим чистовой векторный SVG в память
    QByteArray svgData;
    QBuffer outBuf(&svgData);
    outBuf.open(QIODevice::WriteOnly);

    QSvgGenerator generator;
    generator.setOutputDevice(&outBuf);
    generator.setSize(QSize(w, h));
    generator.setViewBox(QRect(0, 0, w, h));

    QPainter painter(&generator);
    mathText.draw(painter, QPointF(1.0, ascent + 1.0));
    painter.end();

    const QString base64 = QString::fromLatin1(svgData.toBase64());
    const QString imgTag = isBlock
        ? QString("<div align=\"center\" style=\"margin: 6px 0;\"><img src=\"data:image/svg+xml;base64,%1\" width=\"%2\" height=\"%3\"/></div>")
              .arg(base64)
              .arg(w)
              .arg(h)
        : QString("<img src=\"data:image/svg+xml;base64,%1\" width=\"%2\" height=\"%3\" align=\"middle\"/>")
              .arg(base64)
              .arg(w)
              .arg(h);

    svgCache.insert(cacheKey, imgTag);
    return imgTag;
}
