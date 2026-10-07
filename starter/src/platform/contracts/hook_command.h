#pragma once
#include <QJsonObject>
#include <QString>
namespace pet {
// The command handler an agent runs for Agent Pet's hooks (without its timeout). Empty, with
// error set, when the provider's command grammar cannot express this executable.
QJsonObject hookHandler(const QString &executable, const QString &provider, QString &error);
// Whether a handler is exactly one hookHandler() emits, so removal never claims another's hook.
bool ownedHookHandler(const QJsonObject &handler, const QString &provider);
// The program hooks run, given the application: itself, or its console companion on Windows.
QString hookExecutable(const QString &application);
}
