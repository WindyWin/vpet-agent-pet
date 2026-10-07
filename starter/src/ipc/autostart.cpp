#include "autostart.h"
#include "settings/preferences.h"
#include "platform/headless.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace pet {
bool autostartPet(const QByteArray &event, const QString &kind, const QString &preferencesPath,
                  const QProcessEnvironment &environment, const QString &executable, const Launcher &launch) {
    if (kind != "session_start" || !launch) return false;
    if (!platform::desktopSessionAvailable(environment)) return false;
    PreferencesStore store(preferencesPath);
    if (!store.load().autostart || !store.error().isEmpty()) return false;
    return launch(executable, {"--autostarted", "--launch-event", QString::fromUtf8(event)});
}
static int loginCommand(const QStringList &args) {
    const auto operation = args.value(3);
    if (args.size() != 4 || (operation != "enable" && operation != "disable" && operation != "status")) {
        std::fprintf(stderr, "Usage: agent-pet autostart login enable|disable|status\n"); return 1;
    }
    QString error;
    if (operation != "status" && !setLoginStart(operation == "enable", petExecutable(), &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error)); return 1;
    }
    const QJsonObject report{{"start_at_login", loginStartEnabled()}, {"entry", loginEntryPath()}};
    const auto output = QJsonDocument(report).toJson();
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return 0;
}
int autostartCommand(const QStringList &args) {
    if (args.value(2) == "login") return loginCommand(args);
    const auto operation = args.value(2);
    QString error;
    auto fail = [&] { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; };
    const QString usage = "Usage: agent-pet autostart enable|disable|status [--when-idle keep|hide|quit]\n"
                          "       agent-pet autostart login enable|disable|status";
    if (operation != "enable" && operation != "disable" && operation != "status") { error = usage; return fail(); }
    bool setPolicy = false;
    IdlePolicy policy = IdlePolicy::Keep;
    for (int i = 3; i < args.size(); ++i) {
        if (args[i] == "--when-idle" && i + 1 < args.size() && !setPolicy && operation != "status") {
            if (!parseIdlePolicy(args[++i], policy)) { error = "Expected --when-idle keep, hide or quit"; return fail(); }
            setPolicy = true;
        } else { error = usage; return fail(); }
    }
    PreferencesStore store;
    auto preferences = store.load();
    if (!store.error().isEmpty()) { error = store.error() + "\n" + store.path(); return fail(); }
    bool changed = false;
    if (operation != "status") {
        auto updated = preferences;
        updated.autostart = operation == "enable";
        if (setPolicy) updated.whenIdle = policy;
        // Saving only on a change: disabling what was never enabled creates no file.
        changed = updated.autostart != preferences.autostart || updated.whenIdle != preferences.whenIdle;
        if (changed && !store.save(updated)) { error = "Cannot save preferences: " + store.error() + "\n" + store.path(); return fail(); }
        preferences = updated;
    }
    const QJsonObject report{{"autostart", preferences.autostart}, {"when_idle", idlePolicyName(preferences.whenIdle)},
                             {"preferences", store.path()}, {"changed", changed}};
    const auto output = QJsonDocument(report).toJson();
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return 0;
}
}
