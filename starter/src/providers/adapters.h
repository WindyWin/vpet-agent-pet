#pragma once
#include <QJsonObject>
#include <QStringList>
namespace pet {
// Empty result means unsupported or invalid input. Never forwards content fields.
QJsonObject normalizeHook(const QString &provider, const QJsonObject &input, qint64 now);
QStringList hookEvents(const QString &provider);
// Whether a shell command line looks destructive: rm with recursive and force flags, a forced git
// push, git reset --hard, git clean with force, mkfs, dd onto a device, or SQL DROP or TRUNCATE.
// A heuristic for the pet's startled reaction only; the hook sends the answer, never the command.
bool destructiveCommand(const QString &command);
// The activity class of a shell command line, from the programs it runs: grep searches, cat reads,
// sed -i or a redirection to a file edits, curl browses, and anything else executes. With several
// commands the most active class wins (the activityClasses() order). Only the class leaves the hook.
QString commandActivity(const QString &command);
}
