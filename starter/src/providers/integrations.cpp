#include "integrations.h"
#include "i18n/contexts.h"
#include "adapters.h"
#include "registry.h"
#include "opencode_plugin.h"
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
static QByteArray pluginSource(const QString &executable, const char *body) {
    const auto literal = QJsonDocument(QJsonArray{executable}).toJson(QJsonDocument::Compact);
    return "const executable = " + literal.mid(1, literal.size() - 2) + ";\n" + body;
}
static QByteArray pluginSource(const QString &executable) {
    return pluginSource(executable, openCodePluginBody);
}
static bool ownedPlugin(const QByteArray &source) {
    const QByteArray prefix = "const executable = ";
    const int end = source.indexOf(";\n");
    if (!source.startsWith(prefix) || end < prefix.size()) return false;
    const auto values = QJsonDocument::fromJson("[" + source.mid(prefix.size(), end - prefix.size()) + "]").array();
    // Byte identity against every released template; user-modified files stay refused.
    // The legacy body lets enable migrate V1-era installs after an upgrade.
    return values.size() == 1 && values.first().isString()
        && (source == pluginSource(values.first().toString())
            || source == pluginSource(values.first().toString(), openCodePluginLegacyBody));
}
static bool ownedNamedHandler(QJsonObject handler, const QString &event) {
    const auto suffix = " --event " + event;
    auto command = handler["command"].toString();
    if (!command.endsWith(suffix)) return false;
    command.chop(suffix.size()); handler["command"] = command;
    return ownedHookHandler(handler, "antigravity");
}
static bool mergeNamedHooks(const QJsonObject &input, const QString &executable, bool enable,
                            QJsonObject &output, int &owned, QString &error) {
    const QString key = "agent-pet-v1";
    if (input.contains(key) && !input[key].isObject()) { error = "Invalid Agent Pet hook namespace; unchanged"; return false; }
    auto definition = input[key].toObject();
    for (const auto &event : hookEvents("antigravity")) {
        if (definition.contains(event) && !definition.value(event).isArray()) { error = "Invalid hook array; unchanged"; return false; }
        const bool tool = event == "PreToolUse" || event == "PostToolUse";
        QJsonArray kept;
        for (const auto &value : definition.value(event).toArray()) {
            if (!value.isObject()) { error = "Invalid hook handler; unchanged"; return false; }
            if (!tool) {
                if (ownedNamedHandler(value.toObject(), event)) ++owned;
                else kept.append(value);
                continue;
            }
            auto group = value.toObject();
            if (!group["hooks"].isArray()) { error = "Invalid tool hook group; unchanged"; return false; }
            QJsonArray handlers; bool removed = false;
            for (const auto &handler : group["hooks"].toArray()) {
                if (handler.isObject() && ownedNamedHandler(handler.toObject(), event)) { ++owned; removed = true; }
                else handlers.append(handler);
            }
            if (!removed) kept.append(group);
            else if (!handlers.isEmpty()) { group["hooks"] = handlers; kept.append(group); }
        }
        if (enable) {
            auto handler = hookHandler(executable, "antigravity", error);
            if (handler.isEmpty()) return false;
            handler["command"] = handler["command"].toString() + " --event " + event;
            handler["timeout"] = 1;
            kept.append(tool ? QJsonObject{{"hooks", QJsonArray{handler}}} : handler);
        }
        if (kept.isEmpty() && !definition.value(event).toArray().isEmpty()) definition.remove(event);
        else if (!kept.isEmpty()) definition[event] = kept;
    }
    output = input;
    if (definition.isEmpty() && owned > 0) output.remove(key);
    else if (!definition.isEmpty()) output[key] = definition;
    return true;
}
bool mergeIntegration(const QJsonObject &input, const QString &provider, const QString &executable,
                      bool enable, QJsonObject &output, int &owned, QString &error) {
    owned = 0; output = input;
    if (enable && (!QDir::isAbsolutePath(executable) || executable.contains(QChar('\n')) ||
                   executable.contains(QChar('\r')) || executable.contains(QChar(0)))) {
        error = Integrations::tr("Executable must be an absolute path without control characters"); return false;
    }
    const auto entry = findProvider(provider);
    if (entry && entry->format == IntegrationFormat::NamedHooks)
        return mergeNamedHooks(input, executable, enable, output, owned, error);
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
    if (provider == "opencode") return qEnvironmentVariable("OPENCODE_CONFIG_DIR",
        qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + "/.config") + "/opencode") + "/plugins/agent-pet.js";
    if (provider == "antigravity") return qEnvironmentVariable("ANTIGRAVITY_CONFIG_DIR", QDir::homePath() + "/.gemini/config") + "/hooks.json";
    if (!findProvider(provider)) return {};
    const auto root = qEnvironmentVariable(provider == "claude" ? "CLAUDE_CONFIG_DIR" : "CODEX_HOME",
                                           QDir::homePath() + (provider == "claude" ? "/.claude" : "/.codex"));
    return root + (provider == "claude" ? "/settings.json" : "/hooks.json");
}
bool runIntegration(const QString &operation, const QString &provider, QString path, const QString &application,
                    QJsonObject &report, QString &error) {
    auto fail = [] { return false; };
    const auto executable = hookExecutable(application);
    const auto entry = findProvider(provider);
    if (!QStringList{"preview", "inspect", "enable", "disable"}.contains(operation) || !entry) {
        error = "Usage: agent-pet integration preview|inspect|enable|disable --provider claude|codex|opencode|antigravity [--config PATH] [--executable PATH]"; return fail();
    }
    const bool plugin = entry->format == IntegrationFormat::JavaScriptPlugin;
    if (operation != "disable" && (!QDir::isAbsolutePath(executable) || executable.contains(QChar('\n')) ||
                                  executable.contains(QChar('\r')) || executable.contains(QChar(0)))) {
        error = Integrations::tr("Executable must be an absolute path without control characters"); return fail();
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
        if (plugin) {
            if (!ownedPlugin(before)) { error = "OpenCode plugin file is not an unmodified Agent Pet plugin; unchanged"; return fail(); }
        } else {
            QJsonParseError parse; const auto doc = QJsonDocument::fromJson(before, &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject()) { error = Integrations::tr("Malformed JSON configuration; unchanged"); return fail(); }
            input = doc.object();
        }
    }
    QJsonObject output; int owned = 0;
    if (plugin) owned = existed ? 1 : 0;
    else if (!mergeIntegration(input, provider, executable, operation != "disable", output, owned, error)) return fail();
    const auto after = plugin ? (operation == "disable" ? QByteArray() : pluginSource(executable)) : QJsonDocument(output).toJson();
    const bool changed = plugin ? before != after : input != output;
    report = QJsonObject{{"provider", provider}, {"config", path}, {"owned_handlers", owned},
                       {"expected_handlers", plugin ? 1 : hookEvents(provider).size()},
                       {"coverage", Integrations::tr("Local observed sessions only. Restart the client after setup; verify in /hooks. Silent sessions and remote/container hosts are not discovered.")}};
    if (provider == "codex") report["setup"] = Integrations::tr("Review and trust these definitions in Codex /hooks. features.hooks and managed policy can prevent execution. Agent Pet does not change trust or policy.");
    else report["setup"] = Integrations::tr("Inspect /hooks. disableAllHooks or managed policy can prevent execution. Agent Pet does not change policy.");
    if (provider == "opencode") report["setup"] = Integrations::tr("Restart OpenCode to load the Agent Pet plugin. Local plugin callbacks only; live compatibility is experimental.");
    if (provider == "antigravity") {
        report["setup"] = Integrations::tr("Restart Antigravity and inspect Customizations > Hooks (CLI: /hooks). Live compatibility is experimental.");
        if (input["agent-pet-v1"].toObject()["enabled"] == false) report["warning"] = Integrations::tr("Agent Pet hook namespace is disabled");
    }
    if (input.value("disableAllHooks").toBool()) report["warning"] = Integrations::tr("disableAllHooks is set in this file");
    if (operation == "preview") {
        if (plugin) report["proposed_plugin"] = QString::fromUtf8(after);
        else report["proposed_config"] = output;
    }
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
        if (provider == "antigravity") report["entries"] = input["agent-pet-v1"];
        if (plugin) report["entries"] = existed ? QJsonObject{{"plugin", path}} : QJsonObject{};
    }
    if (write && changed) {
        QFile check(path);
        if (QFileInfo(path).isSymLink() || QFileInfo::exists(path) != existed ||
            (existed && (!check.open(QIODevice::ReadOnly) || check.readAll() != before))) { error = Integrations::tr("Configuration changed during setup; retry"); return fail(); }
        check.close();
        if (plugin && operation == "disable") {
            if (!QFile::remove(path)) { error = "Cannot remove Agent Pet plugin"; return fail(); }
            report["changed"] = true;
            return true;
        }
        QSaveFile saved(path); saved.setDirectWriteFallback(false);
        if (!saved.open(QIODevice::WriteOnly)) { error = Integrations::tr("Cannot open configuration for atomic save"); return fail(); }
        saved.setPermissions(existed ? QFileInfo(path).permissions() : QFile::ReadOwner | QFile::WriteOwner);
        const auto data = after;
        if (saved.write(data) != data.size() || !saved.commit()) { error = Integrations::tr("Cannot save configuration"); return fail(); }
    }
    report["changed"] = write && changed;
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
