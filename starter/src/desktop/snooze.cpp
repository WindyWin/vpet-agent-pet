#include "snooze.h"

namespace pet {
qint64 Snooze::tomorrowMs(const QDateTime &local) {
    const QTime morning(tomorrowHour, 0);
    const auto day = local.time() < morning ? local.date() : local.date().addDays(1);
    return qMax<qint64>(1, local.msecsTo(QDateTime(day, morning)));
}
bool Nudges::held(const QString &key, qint64 now) const {
    const auto it = entries_.constFind(key);
    return it != entries_.constEnd() && (it->showing || now < it->notBefore);
}
void Nudges::shown(const QString &key) {
    auto &entry = entries_[key];
    ++entry.asks; entry.showing = true;
}
void Nudges::later(const QString &key, qint64 now) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return;
    // Putting it off, or losing its bubble to something else, is not the user ignoring it.
    if (it->showing && it->asks > 0) --it->asks;
    it->showing = false; it->notBefore = now + laterMs;
}
bool Nudges::ignored(const QString &key, qint64 now) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    it->showing = false; it->notBefore = now + laterMs;
    return it->asks >= maxAsks;
}
void Nudges::settle(const QString &showing, qint64 now) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (it->showing && it.key() != showing) { it->showing = false; it->notBefore = now + laterMs; if (it->asks > 0) --it->asks; }
}
}
