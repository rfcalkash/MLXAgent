#pragma once

#include <QColor>
#include <QHash>
#include <QString>

class MathFormatter {
public:
    static QString toRichHtml(const QString& markdown,
        const QColor& textColor = Qt::white,
        double fontSizePt = 11.0);

private:
    static QString renderLatexToImgTag(const QString& latex,
        const QColor& color,
        double fontSizePt,
        bool isBlock);
};
