#include "adapters.h"
#include "sessions/state.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>

namespace pet {
static QJsonObject envelope(const QString &provider, const QString &session, const QString &kind, qint64 now) {
    if (session.isEmpty() || kind.isEmpty()) return {};
    return {{"version", 1}, {"provider", provider}, {"session_id", session}, {"kind", kind},
            {"timestamp_ms", now}, {"event_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
}
static QString toolEventId(const QString &type, const QString &tool) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonArray{type, tool}).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}
// The bundled plugin forwards only this small metadata contract, never message/tool content.
QJsonObject normalizeOpenCode(const QJsonObject &in, qint64 now) {
    const auto type = in["type"].toString();
    QString kind;
    if (type == "session.created" || type == "session.updated") kind = "session_start";
    else if (type == "chat.message") kind = "prompt";
    else if (type == "tool.execute.before") kind = "tool_start";
    else if (type == "tool.execute.after") kind = "tool_end";
    else if (type == "tool.error") kind = "error";
    else if (type == "permission.asked" || type == "question.asked") kind = "attention";
    else if (type == "session.idle") kind = "turn_finished";
    else if (type == "session.error") kind = in["aborted"].toBool() ? "interrupt" : "turn_failed";
    else if (type == "session.deleted") kind = "session_end";
    auto out = envelope("opencode", in["sessionID"].toString(), kind, now);
    if (out.isEmpty()) return {};
    if (kind == "tool_start" || kind == "tool_end" || kind == "error") {
        const auto tool = in["callID"].toString();
        if (tool.isEmpty()) return {};
        out["tool_id"] = tool;
        out["event_id"] = toolEventId(type, tool);
        if (kind == "tool_start") out["activity"] = toolActivity(in["tool"].toString());
    }
    if (kind == "attention") {
        out["reason"] = type == "question.asked" ? "input" : "approval";
        if (!in["callID"].toString().isEmpty()) out["tool_id"] = in["callID"];
    }
    if (kind == "turn_finished" && !in["parentID"].toString().isEmpty()) out["kind"] = "session_end";
    if (!in["eventID"].toString().isEmpty()) out["event_id"] = in["eventID"];
    if (!in["parentID"].toString().isEmpty()) out["parent_id"] = in["parentID"];
    if (in["directory"].isString()) out["project_path"] = in["directory"];
    return out;
}
QJsonObject normalizeAntigravity(const QJsonObject &in, qint64 now) {
    const auto name = in["hook_event_name"].toString(); // Supplied by the registered --event argument.
    QString kind;
    if (name == "SessionStart") kind = "session_start";
    // Later model calls are continuations, not new user turns. In particular they must
    // not clear overlapping/background tools or restart the observed turn timer.
    else if (name == "PreInvocation" && in["invocationNum"].isDouble() && in["invocationNum"].toDouble() == 0) kind = "prompt";
    else if (name == "PreToolUse") kind = "tool_start";
    else if (name == "PostToolUse") kind = in["error"].toString().isEmpty() ? "tool_end" : "error";
    else if (name == "Stop") kind = in["terminationReason"] == "error" || !in["error"].toString().isEmpty() ? "turn_failed" : "turn_finished";
    auto out = envelope("antigravity", in["conversationId"].toString(), kind, now);
    if (out.isEmpty()) return {};
    const auto paths = in["workspacePaths"].toArray();
    if (!paths.isEmpty() && paths.first().isString()) out["project_path"] = paths.first();
    if (kind == "tool_start" || kind == "tool_end" || kind == "error") {
        const auto step = in["stepIdx"];
        if (!step.isDouble() || step.toDouble() < 0 || step.toDouble() > 9007199254740991.0 ||
            std::floor(step.toDouble()) != step.toDouble()) return {};
        const auto id = QString::number(step.toDouble(), 'f', 0);
        out["tool_id"] = id;
        out["event_id"] = toolEventId(name, id);
        if (kind == "tool_start") {
            const auto call = in["toolCall"].toObject();
            out["activity"] = toolActivity(call["name"].toString());
            const auto line = call["args"].toObject()["CommandLine"].toString();
            if (destructiveCommand(line)) out["risky"] = true;
            if (out["activity"].toString() == "executing" && !line.trimmed().isEmpty()) out["activity"] = commandActivity(line);
        }
    }
    if (kind == "turn_finished" && in["fullyIdle"].isBool() && !in["fullyIdle"].toBool()) out["waiting"] = true;
    return out;
}
}
