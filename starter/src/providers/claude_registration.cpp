#include "claude_bridge.h"
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>

namespace pet::claude {

// Hooks that run for the whole session are asynchronous so they never delay the terminal: the waiter (SessionStart and
// Stop) is the one that may wake an idle session, and Claude Code still enforces a timeout on those, so it is a day.
constexpr int waiterTimeoutSeconds = 24 * 60 * 60;

QStringList interactionEvents() { return {"SessionStart", "UserPromptSubmit", "PreToolUse", "Stop", "SessionEnd"}; }

QJsonObject interactionHandler(const QString &executable, const QString &event, int waitSeconds) {
    auto escaped = executable;
    escaped.replace("'", "'\\''");
    QString command = "'" + escaped + "' claude-interact --event " + event;
    if (event == "PreToolUse") command += " --wait " + QString::number(waitSeconds);
    command += QString(" --registration ") + interactionRegistration;
    QJsonObject handler{{"type", "command"}, {"command", command}};
    if (event == "SessionStart" || event == "Stop") { handler["asyncRewake"] = true; handler["timeout"] = waiterTimeoutSeconds; }
    else if (event == "PreToolUse") handler["timeout"] = waitSeconds + 10; // Claude Code kills the hook if it hangs.
    else handler["async"] = true;
    return handler;
}

bool ownedInteractionHandler(const QJsonObject &handler) {
    // The entire command grammar, so no other hook that merely mentions our names is ever claimed.
    static const QRegularExpression command(
        R"(^'(?:[^']|'\\'')*' claude-interact --event (?:SessionStart|UserPromptSubmit|Stop|SessionEnd|PreToolUse --wait [0-9]{1,3}) --registration agent-pet-interact-v1$)");
    return handler.value("type").toString() == "command" && command.match(handler.value("command").toString()).hasMatch();
}

bool mergeInteractions(const QJsonObject &input, const QString &executable, bool enable, int waitSeconds,
                       QJsonObject &output, int &owned, QString &error) {
    owned = 0; output = input;
    if (enable && (!QDir::isAbsolutePath(executable) || executable.contains(QChar('\n')) || executable.contains(QChar('\r')) ||
                   executable.contains(QChar(0)))) {
        error = "Executable must be an absolute path without control characters"; return false;
    }
    if (enable && (waitSeconds < 1 || waitSeconds > maxQuestionWaitSeconds)) {
        error = QString("The question wait must be between 1 and %1 seconds").arg(maxQuestionWaitSeconds); return false;
    }
    if (input.contains("hooks") && !input.value("hooks").isObject()) { error = "hooks must be an object; configuration unchanged"; return false; }
    auto hooks = input.value("hooks").toObject();
    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
        if (!it.value().isArray()) { error = "Hook event must contain an array; configuration unchanged"; return false; }
        QJsonArray groups;
        for (const auto &value : it.value().toArray()) {
            if (!value.isObject() || !value.toObject().value("hooks").isArray()) { error = "Invalid hook group; configuration unchanged"; return false; }
            auto group = value.toObject();
            if (group.contains("matcher") && !group.value("matcher").isString()) { error = "Invalid hook matcher; configuration unchanged"; return false; }
            QJsonArray handlers; bool removed = false;
            for (const auto &handler : group.value("hooks").toArray()) {
                if (!handler.isObject()) { error = "Invalid hook handler; configuration unchanged"; return false; }
                if (ownedInteractionHandler(handler.toObject())) { ++owned; removed = true; }
                else handlers.append(handler);
            }
            if (!removed) groups.append(group);
            else if (!handlers.isEmpty()) { group["hooks"] = handlers; groups.append(group); }
        }
        it.value() = groups;
    }
    // Only arrays emptied by removing our entries go; pre-existing empty arrays stay.
    for (const auto &name : hooks.keys())
        if (hooks.value(name).toArray().isEmpty() && !input.value("hooks").toObject().value(name).toArray().isEmpty()) hooks.remove(name);
    if (enable) {
        for (const auto &event : interactionEvents()) {
            auto groups = hooks.value(event).toArray();
            QJsonObject group{{"hooks", QJsonArray{interactionHandler(executable, event, waitSeconds)}}};
            if (event == "PreToolUse") group["matcher"] = "AskUserQuestion";
            groups.append(group);
            hooks[event] = groups;
        }
    }
    if (!hooks.isEmpty() || input.contains("hooks")) output["hooks"] = hooks;
    return true;
}

}
