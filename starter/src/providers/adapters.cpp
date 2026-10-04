#include "adapters.h"
#include "sessions/state.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
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
// Codex asks questions through a tool; the call blocks until the user answers.
static bool codexQuestion(const QJsonObject &in) { return in.value("tool_name").toString() == "request_user_input"; }
static QString codexKind(const QJsonObject &in, const QString &commandStatus) {
    const auto name = in.value("hook_event_name").toString();
    if (name == "Interrupt") return "interrupt";
    if (name == "PreToolUse" && codexQuestion(in)) return "attention";
    if (name == "PostToolUse") {
        if (commandStatus == "failed") return "error";
        const auto response = in.value("tool_response").toObject();
        // Only structured failure signals; never infer errors from output text.
        if (response.value("isError").toBool() ||
            (response.value("exit_code").isDouble() && response.value("exit_code").toDouble() != 0)) return "error";
    }
    return commonKind(name);
}
// Codex runs every shell command through one "Bash" tool, so the tool name alone
// cannot tell exploring from editing. Classify the command line in memory only:
// it is reading when every pipeline/sequence segment starts with a read-only
// program. Nothing from the command is forwarded.
static bool readOnlyCommand(const QString &command) {
    static const QSet<QString> programs{"cat", "head", "tail", "less", "more", "nl", "wc", "ls", "tree", "find", "fd",
        "rg", "grep", "egrep", "fgrep", "ag", "ack", "file", "stat", "du", "pwd", "which", "realpath", "readlink",
        "basename", "dirname", "jq", "sort", "uniq", "cut", "column", "diff", "cmp", "echo", "printf", "true"};
    static const QSet<QString> gitReads{"status", "log", "show", "diff", "blame", "grep", "ls-files", "rev-parse"};
    static const QRegularExpression separators(R"(\|\||&&|[|;\n])");
    static const QRegularExpression space(R"(\s+)");
    auto line = command;
    line.remove(QStringLiteral("2>/dev/null")).remove(QStringLiteral("2>&1"));
    if (line.size() > 4096 || line.contains('>') || line.contains("$(") || line.contains('`')) return false;
    const auto segments = line.split(separators, Qt::SkipEmptyParts);
    if (segments.isEmpty()) return false;
    for (const auto &segment : segments) {
        const auto words = segment.trimmed().split(space, Qt::SkipEmptyParts);
        if (words.isEmpty()) continue;
        const auto program = words[0].section('/', -1);
        if (program == "cd") continue;
        if (program == "sed" && words.size() > 1 && words[1] == "-n" && !segment.contains(" -i")) continue;
        if (program == "git" && words.size() > 1 && gitReads.contains(words[1])) continue;
        if (!programs.contains(program) || (program == "find" && (segment.contains("-delete") || segment.contains("-exec")))) return false;
    }
    return true;
}
static QString activity(const QString &provider, const QJsonObject &in) {
    static const QSet<QString> reading{"Read", "Grep", "Glob", "WebFetch", "WebSearch", "read_file", "list_dir", "view_image"};
    const auto tool = in.value("tool_name").toString();
    if (reading.contains(tool)) return "reading";
    if (provider == "codex" && tool == "Bash" && readOnlyCommand(in.value("tool_input").toObject().value("command").toString()))
        return "reading";
    return "working";
}
QString codexCommandStatus(const QJsonObject &in) {
    // The PostToolUse payload carries only command output, without the exit code.
    // Codex records each finished command, with its status, in the session rollout
    // before calling the hook; read only the tail of that file for this call.
    if (in.value("hook_event_name").toString() != "PostToolUse" || in.value("tool_name").toString() != "Bash") return {};
    const auto path = in.value("transcript_path").toString(), tool = in.value("tool_use_id").toString();
    if (tool.isEmpty() || !path.endsWith(".jsonl") || !QFileInfo(path).isFile()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    constexpr qint64 tail = 512 * 1024;
    if (file.size() > tail) file.seek(file.size() - tail);
    const auto lines = file.read(tail).split('\n');
    const auto needle = QJsonDocument(QJsonObject{{"id", tool}}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (!it->contains(needle) || !it->contains("\"item_completed\"")) continue;
        const auto item = QJsonDocument::fromJson(*it).object().value("payload").toObject().value("item").toObject();
        if (item.value("id").toString() == tool && item.value("type").toString() == "CommandExecution")
            return item.value("status").toString();
    }
    return {};
}
QJsonObject normalizeHook(const QString &provider, const QJsonObject &in, qint64 now, const QString &commandStatus) {
    const auto name = in.value("hook_event_name").toString();
    if (!hookEvents(provider).contains(name)) return {};
    const auto kind = provider == "claude" ? claudeKind(in) : codexKind(in, commandStatus);
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
        else if (type == "idle_prompt" || type == "elicitation_dialog" || (provider == "codex" && codexQuestion(in))) out["reason"] = "input";
    }
    if (kind == "tool_start") out["activity"] = activity(provider, in);
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
