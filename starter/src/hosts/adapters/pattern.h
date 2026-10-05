#pragma once
#include <QRegularExpression>

namespace pet::hosts {
// True when the whole value matches; adapters validate every target field this way.
inline bool fullMatch(const char *pattern, const QString &value) {
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern)).match(value).hasMatch();
}
}
