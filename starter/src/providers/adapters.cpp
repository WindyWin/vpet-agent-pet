#include "adapters.h"
#include "sessions/state.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace pet {
QStringList hookEvents(const QString &provider) {
    QStringList events{"SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PermissionRequest", "Stop", "SessionEnd", "SubagentStart", "SubagentStop"};
    if (provider == "claude") events << "PostToolUseFailure" << "Notification" << "StopFailure";
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
    if (name == "StopFailure") return "turn_failed";
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
bool destructiveCommand(const QString &command) {
    static const QRegularExpression sql(R"(\b(drop\s+(table|database|schema)|truncate\s+table)\b)",
                                        QRegularExpression::CaseInsensitiveOption);
    if (sql.match(command).hasMatch()) return true;
    // Each simple command on its own, with quotes dropped: a false alarm only startles the pet.
    static const QRegularExpression separators(R"(&&|\|\||[;|&\n`()]|\$\()");
    static const QSet<QString> wrappers{"sudo", "doas", "command", "exec", "nohup", "time", "env", "xargs"};
    for (const auto &part : command.split(separators, Qt::SkipEmptyParts)) {
        auto words = QString(part).remove('"').remove('\'').simplified().split(' ', Qt::SkipEmptyParts);
        while (!words.isEmpty() && (wrappers.contains(words.first()) || (words.first().contains('=') && !words.first().startsWith('-'))))
            words.removeFirst();
        if (words.isEmpty()) continue;
        const auto program = words.takeFirst().section('/', -1);
        // A shell running a script, such as Codex's ["bash", "-lc", "..."]: check the script.
        if (QStringList{"bash", "sh", "zsh", "dash"}.contains(program)) {
            while (!words.isEmpty() && words.first().startsWith('-')) words.removeFirst();
            if (destructiveCommand(words.join(' '))) return true;
            continue;
        }
        auto flag = [&words](QChar letter, const QString &longName) {
            for (const auto &word : words)
                if (word == longName || (word.startsWith('-') && !word.startsWith("--") && word.contains(letter))) return true;
            return false;
        };
        if (program == "rm" && (flag('r', "--recursive") || flag('R', "--recursive")) && flag('f', "--force")) return true;
        if (program.startsWith("mkfs")) return true;
        if (program == "dd" && std::any_of(words.begin(), words.end(), [](const QString &w) { return w.startsWith("of=/dev/"); }))
            return true;
        if (program != "git") continue;
        // The subcommand is the first word that is not an option or the value of -C or -c.
        QString subcommand;
        for (int i = 0; i < words.size() && subcommand.isEmpty(); ++i) {
            if (words[i] == "-C" || words[i] == "-c") ++i;
            else if (!words[i].startsWith('-')) subcommand = words[i];
        }
        const auto forced = flag('f', "--force") ||
            std::any_of(words.begin(), words.end(), [](const QString &w) { return w.startsWith("--force") || w.startsWith('+'); });
        if ((subcommand == "push" && forced) || (subcommand == "reset" && words.contains("--hard")) ||
            (subcommand == "clean" && flag('f', "--force"))) return true;
    }
    return false;
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
        // A child stop must never announce completion, or failure, of the parent turn.
        if (name == "Stop" || name == "StopFailure") out["kind"] = "session_end";
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
    if (out["kind"] == "turn_failed") {
        // Only the quota categories are named; the provider's error type itself stays here.
        const auto type = in.value("error_type").toString();
        if (type == "rate_limit") out["reason"] = "limit";
        else if (type == "billing_error") out["reason"] = "billing";
    }
    if (kind == "tool_start") {
        const QSet<QString> reading{"Read", "Grep", "Glob", "WebFetch", "WebSearch", "read_file", "list_dir"};
        out["activity"] = reading.contains(in.value("tool_name").toString()) ? "reading" : "working";
        // A shell tool's command line, as one string or an argument list, is checked here and dropped.
        const auto command = in.value("tool_input").toObject().value("command");
        QString line = command.toString();
        if (command.isArray()) for (const auto &part : command.toArray()) line += part.toString() + ' ';
        if (destructiveCommand(line)) out["risky"] = true;
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
