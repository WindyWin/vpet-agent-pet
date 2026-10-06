#include "platform/contracts/startup.h"
#include "i18n/contexts.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace pet {
static constexpr auto label = "io.github.windywin.agent-pet";
QString loginEntryPath(const QString &directory) {
    const auto base = directory.isEmpty() ? QDir::homePath() + "/Library/LaunchAgents" : directory;
    return base + '/' + label + ".plist";
}
bool loginStartEnabled(const QString &directory) { return QFile::exists(loginEntryPath(directory)); }
static QString xmlText(const QString &text) {
    return QString(text).replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
}
// A per-user launchd agent, loaded at the next login: it starts the pet once and does not restart it.
bool setLoginStart(bool enabled, const QString &executable, QString *error, const QString &directory) {
    const auto path = loginEntryPath(directory);
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!enabled) return !QFile::exists(path) || QFile::remove(path) || fail(Startup::tr("Cannot remove %1").arg(path));
    if (executable.contains("/AppTranslocation/"))
        return fail(Startup::tr("macOS is running Agent Pet from a temporary copy. Move Agent Pet to Applications and open it from there."));
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(Startup::tr("Cannot create %1").arg(QFileInfo(path).absolutePath()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    const auto bytes = (QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "  <key>Label</key><string>") + label + "</string>\n"
        "  <key>ProgramArguments</key><array><string>" + xmlText(executable) + "</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>LimitLoadToSessionType</key><string>Aqua</string>\n"
        "  <key>ProcessType</key><string>Interactive</string>\n"
        "</dict>\n</plist>\n").toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    return true;
}
}
