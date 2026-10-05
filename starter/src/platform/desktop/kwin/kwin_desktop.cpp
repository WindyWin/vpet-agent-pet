#include "kwin_desktop.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QTimer>

namespace pet::platform::kwin {
class FocusCallback : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.agentpet.FocusResult")
public:
    bool received = false, focused = false;
    QEventLoop loop;
public slots:
    void complete(bool success) { received = true; focused = success; loop.quit(); }
};
Outcome KWinDesktop::activate(const WindowRequest &request) {
    QJsonArray ancestors;
    for (const auto pid : request.pids) if (pid > 1) ancestors.append(double(pid));
    if (ancestors.isEmpty()) return Outcome::MissingTarget;
    auto bus = QDBusConnection::sessionBus();
    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
    scripting.setTimeout(1000);
    if (!scripting.isValid()) return Outcome::Unsupported;
    FocusCallback result;
    const QString path = "/AgentPetFocus";
    if (!bus.registerObject(path, &result, QDBusConnection::ExportAllSlots)) return Outcome::Failed;
    const auto json = [](const QJsonArray &value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); };
    QTemporaryFile file(QDir::tempPath() + "/agent-pet-focus-XXXXXX.js");
    auto outcome = Outcome::Failed;
    if (file.open()) {
        QByteArray script = "const pids = " + json(ancestors) + ";\nconst params = " +
            json({QFileInfo(request.project).fileName(), bus.baseService(), path}) + R"JS(;
let chosen = null;
const windows = workspace.windowList();
for (const pid of pids) {
    const matches = windows.filter(w => w.pid === pid && w.normalWindow);
    if (!matches.length) continue;
    const named = matches.filter(w => params[0] && w.caption.toLowerCase().includes(params[0].toLowerCase()));
    if (matches.length === 1) chosen = matches[0];
    else if (named.length === 1) chosen = named[0];
    break;
}
if (chosen) {
    chosen.minimized = false;
    if (chosen.desktops.length) workspace.currentDesktop = chosen.desktops[0];
    workspace.activeWindow = chosen;
}
callDBus(params[1], params[2], "org.agentpet.FocusResult", "complete",
         chosen !== null && workspace.activeWindow === chosen);
)JS";
        if (file.write(script) == script.size() && file.flush()) {
            const auto name = QFileInfo(file.fileName()).fileName();
            QDBusReply<int> loaded = scripting.call("loadScript", file.fileName(), name);
            if (loaded.isValid() && loaded.value() >= 0) {
                QDBusInterface runner("org.kde.KWin", QString("/Scripting/Script%1").arg(loaded.value()), "org.kde.kwin.Script", bus);
                runner.setTimeout(1000);
                if (runner.call("run").type() != QDBusMessage::ErrorMessage && !result.received) {
                    QTimer::singleShot(callbackTimeoutMs, &result.loop, &QEventLoop::quit);
                    result.loop.exec(QEventLoop::ExcludeUserInputEvents);
                }
                // The script reports one flag: no matching window and a refused activation look alike.
                outcome = !result.received ? Outcome::TimedOut : result.focused ? Outcome::Confirmed : Outcome::Failed;
                scripting.call("unloadScript", name);
            }
        }
    }
    bus.unregisterObject(path);
    return outcome;
}
}
#include "kwin_desktop.moc"
