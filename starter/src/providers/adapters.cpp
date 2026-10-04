#include "adapters.h"
#include "sessions/state.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

namespace pet {
QStringList hookEvents(const QString &provider) {
    QStringList events{"SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PermissionRequest", "Stop", "SessionEnd", "SubagentStart", "SubagentStop"};
    if (provider == "claude") events << "PostToolUseFailure" << "Notification";
    else if (provider == "codex") events << "Interrupt";
    else return {};
    return events;
}
static QString commonKind(const QString &name) {
    if (name == "SessionStart") return "session_start";
    if (name == "UserPromptSubmit" || name == "SubagentStart") return "prompt";
    if (name == "PreToolUse") return "tool_start";
    if (name == "PostToolUse") return "tool_end";
    if (name == "PermissionRequest") return "attention";
    if (name == "Stop") return "turn_finished";
    if (name == "SessionEnd" || name == "SubagentStop") return "session_end";
    return {};
}
static QString claudeKind(const QJsonObject &in) {
    const auto name = in.value("hook_event_name").toString();
    if (name == "PostToolUseFailure") return in.value("is_interrupt").toBool() ? "interrupt" : "error";
    if (name == "Notification") {
        const auto type = in.value("notification_type").toString();
        if (type == "permission_prompt" || type == "idle_prompt" || type == "elicitation_dialog") return "attention";
        return {};
    }
    return commonKind(name);
}
static QString codexKind(const QJsonObject &in) {
    const auto name = in.value("hook_event_name").toString();
    if (name == "Interrupt") return "interrupt";
    if (name == "PostToolUse") {
        const auto response = in.value("tool_response").toObject();
        // Only structured failure signals; never infer errors from output text.
        if (response.value("isError").toBool() ||
            (response.value("exit_code").isDouble() && response.value("exit_code").toDouble() != 0)) return "error";
    }
    return commonKind(name);
}
QJsonObject normalizeHook(const QString &provider, const QJsonObject &in, qint64 now) {
    const auto name = in.value("hook_event_name").toString();
    if (!hookEvents(provider).contains(name)) return {};
    const auto kind = provider == "claude" ? claudeKind(in) : codexKind(in);
    auto session = in.value("session_id").toString();
    if (kind.isEmpty() || session.isEmpty()) return {};
    const auto agent = in.value("agent_id").toString();
    if (name.startsWith("Subagent") && agent.isEmpty()) return {};
    QJsonObject out{{"version", 1}, {"provider", provider}, {"session_id", session}, {"kind", kind}, {"timestamp_ms", now}};
    if (!agent.isEmpty()) {
        out["parent_id"] = session;
        out["session_id"] = agent;
        // A child stop must never announce completion of the parent turn.
        if (name == "Stop") out["kind"] = "session_end";
    }
    const auto tool = in.value("tool_use_id").toString();
    if (kind == "tool_start" || kind == "tool_end" || name == "PostToolUseFailure" || (name == "PostToolUse" && kind == "error")) {
        if (tool.isEmpty()) return {}; // No invented identity for overlapping tools.
        out["tool_id"] = tool;
    } else if (kind == "attention" && !tool.isEmpty()) out["tool_id"] = tool;
    if (kind == "attention") {
        // Only documented signals: permission requests need approval; idle and
        // elicitation prompts wait for input. Anything else stays unspecified.
        const auto type = in.value("notification_type").toString();
        if (name == "PermissionRequest" || type == "permission_prompt") out["reason"] = "approval";
        else if (type == "idle_prompt" || type == "elicitation_dialog") out["reason"] = "input";
    }
    if (kind == "tool_start") {
        const QSet<QString> reading{"Read", "Grep", "Glob", "WebFetch", "WebSearch", "read_file", "list_dir"};
        out["activity"] = reading.contains(in.value("tool_name").toString()) ? "reading" : "working";
    }
    if (in.value("cwd").isString()) out["project_path"] = in.value("cwd");
    // Tool identities are stable across delivery retries. Lifecycle events lack a
    // unique occurrence ID: use a delivery UUID rather than suppress later turns.
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!tool.isEmpty() && (name == "PreToolUse" || name == "PostToolUse" || name == "PostToolUseFailure")) {
        const auto identity = QJsonDocument(QJsonObject{{"event", name}, {"tool", tool}}).toJson(QJsonDocument::Compact);
        id = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    }
    out["event_id"] = id;
    Event checked; QString error;
    if (!Event::parse(QJsonDocument(out).toJson(QJsonDocument::Compact), checked, error)) return {};
    return out;
}
}
