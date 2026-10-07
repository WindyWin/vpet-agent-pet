#include "integrations.h"
#include "i18n/contexts.h"
#include "adapters.h"
#include "platform/contracts/hook_command.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <cstdio>

namespace pet {
bool mergeIntegration(const QJsonObject &input, const QString &provider, const QString &executable,
                      bool enable, QJsonObject &output, int &owned, QString &error) {
    owned = 0; output = input;
    const auto events = hookEvents(provider);
    if (events.isEmpty()) { error = Integrations::tr("Expected provider claude or codex"); return false; }
    if (input.contains("hooks") && !input.value("hooks").isObject()) { error = Integrations::tr("hooks must be an object; configuration unchanged"); return false; }
    auto hooks = input.value("hooks").toObject();
    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
        if (!it.value().isArray()) { error = Integrations::tr("Hook event must contain an array; configuration unchanged"); return false; }
        QJsonArray groups;
        for (const auto &value : it.value().toArray()) {
            if (!value.isObject() || !value.toObject().value("hooks").isArray()) { error = Integrations::tr("Invalid hook group; configuration unchanged"); return false; }
            auto group = value.toObject();
            if (group.contains("matcher") && !group.value("matcher").isString()) { error = Integrations::tr("Invalid hook matcher; configuration unchanged"); return false; }
            QJsonArray handlers; bool removed = false;
            for (const auto &handler : group.value("hooks").toArray()) {
                if (!handler.isObject()) { error = Integrations::tr("Invalid hook handler; configuration unchanged"); return false; }
                const auto object = handler.toObject();
                if (!object.value("type").isString() || object.value("type").toString().isEmpty() ||
                    (object.value("type").toString() == "command" && !object.value("command").isString())) {
                    error = Integrations::tr("Invalid hook handler fields; configuration unchanged"); return false;
                }
                if (ownedHookHandler(object, provider)) { ++owned; removed = true; }
                else handlers.append(handler);
            }
            if (!removed) groups.append(group);
            else if (!handlers.isEmpty()) { group["hooks"] = handlers; groups.append(group); }
        }
        it.value() = groups;
    }
    // Remove only arrays emptied by removing our entries. Preserve preexisting empty arrays.
    for (const auto &name : hooks.keys())
        if (hooks.value(name).toArray().isEmpty() && !input.value("hooks").toObject().value(name).toArray().isEmpty()) hooks.remove(name);
    if (enable) {
        if (!QDir::isAbsolutePath(executable) || executable.contains(QChar('\n')) || executable.contains(QChar('\r')) || executable.contains(QChar(0))) {
            error = Integrations::tr("Executable must be an absolute path without control characters"); return false;
        }
        auto handler = hookHandler(executable, provider, error);
        if (handler.isEmpty()) return false;
        handler["timeout"] = 1;
        for (const auto &name : events) {
            auto groups = hooks.value(name).toArray();
            groups.append(QJsonObject{{"hooks", QJsonArray{handler}}});
            hooks[name] = groups;
        }
    }
    if (!hooks.isEmpty() || input.contains("hooks")) output["hooks"] = hooks;
    return true;
}
QString integrationConfigPath(const QString &provider) {
    const auto root = qEnvironmentVariable(provider == "claude" ? "CLAUDE_CONFIG_DIR" : "CODEX_HOME",
                                           QDir::homePath() + (provider == "claude" ? "/.claude" : "/.codex"));
    return root + (provider == "claude" ? "/settings.json" : "/hooks.json");
}
bool runIntegration(const QString &operation, const QString &provider, QString path, const QString &application,
                    QJsonObject &report, QString &error) {
    auto fail = [] { return false; };
    const auto executable = hookExecutable(application);
    if (!QStringList{"preview", "inspect", "enable", "disable"}.contains(operation) || hookEvents(provider).isEmpty()) {
        error = "Usage: agent-pet integration preview|inspect|enable|disable --provider claude|codex [--config PATH] [--executable PATH]"; return fail();
    }
    if (operation == "enable" && (!QFileInfo(executable).isFile() || !QFileInfo(executable).isExecutable())) {
        error = Integrations::tr("Hook executable does not exist or is not executable"); return fail();
    }
    // macOS runs a downloaded app that was never moved from a temporary, read-only copy that later disappears.
    if (operation == "enable" && executable.contains("/AppTranslocation/")) {
        error = "macOS is running Agent Pet from a temporary copy. Move Agent Pet to Applications, open it from there, "
                "and enable the hooks again."; return fail();
    }
    if (path.isEmpty()) path = integrationConfigPath(provider);
    path = QFileInfo(path).absoluteFilePath();
    // Disabling with no configuration file creates neither the file nor its directory.
    const bool write = operation == "enable" || (operation == "disable" && QFileInfo::exists(path));
    if (QFileInfo(path).isSymLink()) { error = Integrations::tr("Refusing a symlink configuration; specify its real path"); return fail(); }
    if (write && !QDir().mkpath(QFileInfo(path).absolutePath())) { error = Integrations::tr("Cannot create configuration directory"); return fail(); }
    // Lock our writers; compare bytes again before atomic replacement to detect
    // edits by external clients that do not honor our lock.
    QLockFile lock(path + ".agent-pet.lock");
    if (write && !lock.tryLock(0)) { error = Integrations::tr("Configuration is busy"); return fail(); }
    const bool existed = QFileInfo::exists(path);
    QByteArray before; QJsonObject input;
    QFile file(path);
    if (existed) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) { error = Integrations::tr("Cannot read configuration or it exceeds 4 MiB"); return fail(); }
        before = file.readAll(); file.close();
        QJsonParseError parse; const auto doc = QJsonDocument::fromJson(before, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) { error = Integrations::tr("Malformed JSON configuration; unchanged"); return fail(); }
        input = doc.object();
    }
    QJsonObject output; int owned = 0;
    if (!mergeIntegration(input, provider, executable, operation != "disable", output, owned, error)) return fail();
    report = QJsonObject{{"provider", provider}, {"config", path}, {"owned_handlers", owned},
                       {"expected_handlers", hookEvents(provider).size()},
                       {"coverage", Integrations::tr("Local observed sessions only. Restart the client after setup; verify in /hooks. Silent sessions and remote/container hosts are not discovered.")}};
    if (provider == "codex") report["setup"] = Integrations::tr("Review and trust these definitions in Codex /hooks. features.hooks and managed policy can prevent execution. Agent Pet does not change trust or policy.");
    else report["setup"] = Integrations::tr("Inspect /hooks. disableAllHooks or managed policy can prevent execution. Agent Pet does not change policy.");
    if (input.value("disableAllHooks").toBool()) report["warning"] = Integrations::tr("disableAllHooks is set in this file");
    if (operation == "preview") report["proposed_config"] = output;
    if (operation == "inspect") {
        QJsonObject registered;
        for (const auto &name : input.value("hooks").toObject().keys()) {
            QJsonArray handlers;
            for (const auto &group : input.value("hooks").toObject().value(name).toArray())
                for (const auto &handler : group.toObject().value("hooks").toArray())
                    if (ownedHookHandler(handler.toObject(), provider)) handlers.append(handler);
            if (!handlers.isEmpty()) registered[name] = handlers;
        }
        report["entries"] = registered;
    }
    if (write && input != output) {
        QFile check(path);
        if (QFileInfo(path).isSymLink() || QFileInfo::exists(path) != existed ||
            (existed && (!check.open(QIODevice::ReadOnly) || check.readAll() != before))) { error = Integrations::tr("Configuration changed during setup; retry"); return fail(); }
        check.close();
        QSaveFile saved(path); saved.setDirectWriteFallback(false);
        if (!saved.open(QIODevice::WriteOnly)) { error = Integrations::tr("Cannot open configuration for atomic save"); return fail(); }
        saved.setPermissions(existed ? QFileInfo(path).permissions() : QFile::ReadOwner | QFile::WriteOwner);
        const auto data = QJsonDocument(output).toJson();
        if (saved.write(data) != data.size() || !saved.commit()) { error = Integrations::tr("Cannot save configuration"); return fail(); }
    }
    report["changed"] = write && input != output;
    return true;
}
int integrationCommand(const QStringList &args) {
    QString provider, path, executable = QCoreApplication::applicationFilePath(), error;
    auto fail = [&] { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; };
    for (int i = 3; i < args.size(); ++i) {
        const auto option = args[i];
        if (i + 1 >= args.size()) { error = Integrations::tr("Missing option value"); return fail(); }
        if (option == "--provider" && provider.isEmpty()) provider = args[++i];
        else if (option == "--config" && path.isEmpty()) path = args[++i];
        else if (option == "--executable") executable = args[++i];
        else { error = Integrations::tr("Unknown integration option"); return fail(); }
    }
    QJsonObject report;
    if (!runIntegration(args.value(2), provider, path, executable, report, error)) return fail();
    const auto result = QJsonDocument(report).toJson();
    std::fwrite(result.constData(), 1, result.size(), stdout);
    return 0;
}
}
