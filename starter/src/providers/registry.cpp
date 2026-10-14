#include "registry.h"

namespace pet {
QJsonObject normalizeLegacyHook(const QString &, const QJsonObject &, qint64);
QJsonObject normalizeOpenCode(const QJsonObject &, qint64);
QJsonObject normalizeAntigravity(const QJsonObject &, qint64);
const QVector<Provider> &providers() {
    static const QStringList common{"SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PermissionRequest",
                                    "Stop", "SessionEnd", "SubagentStart", "SubagentStop"};
    static const QVector<Provider> entries{
        {"claude", "Claude Code", IntegrationFormat::HookGroups, common + QStringList{"PostToolUseFailure", "Notification", "StopFailure"},
         [](const QJsonObject &in, qint64 now) { return normalizeLegacyHook("claude", in, now); }},
        {"codex", "Codex", IntegrationFormat::HookGroups, common + QStringList{"Interrupt"},
         [](const QJsonObject &in, qint64 now) { return normalizeLegacyHook("codex", in, now); }},
        {"opencode", "OpenCode", IntegrationFormat::JavaScriptPlugin, {}, normalizeOpenCode},
        {"antigravity", "Antigravity", IntegrationFormat::NamedHooks,
         {"SessionStart", "PreInvocation", "PreToolUse", "PostToolUse", "Stop"}, normalizeAntigravity}
    };
    return entries;
}
const Provider *findProvider(const QString &id) {
    for (const auto &provider : providers()) if (provider.id == id) return &provider;
    return nullptr;
}
}
