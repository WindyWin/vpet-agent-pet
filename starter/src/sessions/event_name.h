#pragma once
#include <QString>

namespace pet {
// The name of a custom event (docs/events.md): [a-z0-9_-]{1,64}. Header-only, so the protocol parser and the plugin
// rules agree without depending on each other.
inline bool validEventName(const QString &name) {
    if (name.isEmpty() || name.size() > 64) return false;
    for (const QChar c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
}
