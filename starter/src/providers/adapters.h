#pragma once
#include <QJsonObject>
#include <QStringList>
namespace pet {
// Empty result means unsupported or invalid input. Never forwards content fields.
// commandStatus is the Codex rollout status of a finished shell command, if known.
QJsonObject normalizeHook(const QString &provider, const QJsonObject &input, qint64 now, const QString &commandStatus = {});
// Bounded, read-only lookup of a Codex shell command's status ("completed", "failed").
QString codexCommandStatus(const QJsonObject &input);
QStringList hookEvents(const QString &provider);
}
