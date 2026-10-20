#include "adapters.h"
#include "registry.h"
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
    const auto entry = findProvider(provider);
    return entry ? entry->events : QStringList{};
}
QJsonObject normalizeHook(const QString &provider, const QJsonObject &input, qint64 now) {
    const auto entry = findProvider(provider);
    if (!entry) return {};
    const auto out = entry->normalize(input, now);
    Event checked; QString error;
    if (!Event::parse(QJsonDocument(out).toJson(QJsonDocument::Compact), checked, error)) return {};
    return out;
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
// Characters with a meaning to the shell, masked while quoted so a pattern such as "a|b" or
// '$1 > 5' stays inside its command. The private-use stand-ins are restored for a nested script.
static const QString shellSpecials = QStringLiteral("&|;\n()`>\"'");
static QChar masked(QChar c) { const auto i = shellSpecials.indexOf(c); return i < 0 ? c : QChar(0xE000 + int(i)); }
static QString unmasked(QString text) {
    for (int i = 0; i < shellSpecials.size(); ++i) text.replace(QChar(0xE000 + i), shellSpecials[i]);
    return text;
}
// Drops quote delimiters and masks what they protect; $( ) and backticks inside double quotes still
// run, so they stay commands. Fd duplication (2>&1, &>) is not a separator. Unbalanced quotes fall
// back to dropping every quote character.
static QString dequote(const QString &line) {
    QString out, open; // open: the stack of ' " ( `
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i], top = open.isEmpty() ? QChar() : open.back();
        if (top == '\'') { if (c == '\'') open.chop(1); else out += masked(c); continue; }
        if (c == '\\' && i + 1 < line.size()) {
            if (line[++i] == '\n') out += ' '; else out += '\\' + QString(masked(line[i]));
            continue;
        }
        if (top == '"') {
            if (c == '"') open.chop(1);
            else if (c == '$' && i + 1 < line.size() && line[i + 1] == '(') { out += "$("; ++i; open += '('; }
            else if (c == '`') { out += c; open += c; }
            else out += masked(c);
        } else if (c == '\'' || c == '"') open += c;
        else if (c == '(') { out += c; open += c; }
        else if (c == ')') { out += c; if (top == '(') open.chop(1); }
        else if (c == '`') { out += c; if (top == '`') open.chop(1); else open += c; }
        else if (c == '&' && ((i > 0 && line[i - 1] == '>') || (i + 1 < line.size() && line[i + 1] == '>'))) out += masked(c);
        else out += c;
    }
    if (open.contains('\'') || open.contains('"')) return QString(line).remove('"').remove('\'');
    return out;
}
// The simple commands of a shell line, each a word list with the program (basename) first: wrappers
// such as sudo or timeout 5, VAR=x prefixes and leading keywords are skipped, and a shell running a
// script, such as Codex's ["bash", "-lc", "..."], contributes the script's commands.
static void simpleCommands(const QString &line, QList<QStringList> &out, int depth = 0) {
    static const QRegularExpression separators(R"(&&|\|\||[;|&\n`()]|\$\()");
    static const QSet<QString> wrappers{"sudo", "doas", "command", "exec", "nohup", "time", "env", "xargs", "timeout", "nice"};
    static const QSet<QString> valued{"-u", "-g", "-n", "-s", "-k", "-I", "-P", "-L", "-d", "-C"};
    static const QSet<QString> keywords{"if", "then", "else", "elif", "do", "while", "until", "!", "{", "}"};
    static const QSet<QString> blocks{"for", "select", "case", "done", "fi", "esac", "function"};
    if (depth > 4) return;
    for (const auto &part : dequote(line).split(separators, Qt::SkipEmptyParts)) {
        auto words = part.simplified().split(' ', Qt::SkipEmptyParts);
        while (!words.isEmpty()) {
            const auto first = words.first();
            if (keywords.contains(first) || (first.contains('=') && !first.startsWith('-'))) { words.removeFirst(); continue; }
            if (!wrappers.contains(first)) break;
            words.removeFirst();
            while (!words.isEmpty() && words.first().startsWith('-')) words.remove(0, valued.contains(words.first()) && words.size() > 1 ? 2 : 1);
            if (first == "timeout" && !words.isEmpty() && words.first().front().isDigit()) words.removeFirst();
        }
        if (words.isEmpty() || blocks.contains(words.first())) continue;
        words.first() = words.first().section('/', -1);
        if (QStringList{"bash", "sh", "zsh", "dash"}.contains(words.first())) {
            words.removeFirst();
            while (!words.isEmpty() && words.first().startsWith('-')) words.removeFirst();
            simpleCommands(unmasked(words.join(' ')), out, depth + 1);
        } else out += words;
    }
}
// The index of git's subcommand in its arguments: the first word that is not an option or the value of -C or -c.
static int gitSubcommand(const QStringList &args) {
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "-C" || args[i] == "-c") ++i;
        else if (!args[i].startsWith('-')) return i;
    }
    return -1;
}
bool destructiveCommand(const QString &command) {
    static const QRegularExpression sql(R"(\b(drop\s+(table|database|schema)|truncate\s+table)\b)",
                                        QRegularExpression::CaseInsensitiveOption);
    if (sql.match(command).hasMatch()) return true;
    // Each simple command on its own: a false alarm only startles the pet.
    QList<QStringList> commands; simpleCommands(command, commands);
    for (auto words : commands) {
        const auto program = words.takeFirst();
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
        const auto at = gitSubcommand(words);
        const auto subcommand = at < 0 ? QString() : words[at];
        const auto forced = flag('f', "--force") ||
            std::any_of(words.begin(), words.end(), [](const QString &w) { return w.startsWith("--force") || w.startsWith('+'); });
        if ((subcommand == "push" && forced) || (subcommand == "reset" && words.contains("--hard")) ||
            (subcommand == "clean" && flag('f', "--force"))) return true;
    }
    return false;
}
// The class of one simple command: empty when neutral (cd, echo...), else one of activityClasses().
static QString simpleActivity(QStringList words) {
    // An output redirection to a file edits it, whatever the program; /dev/null and 2>&1 do not.
    bool writes = false;
    for (int i = 0; i < words.size();) {
        if (words[i].startsWith('-') || !words[i].contains('>')) { ++i; continue; }
        auto target = words[i].section('>', -1); int span = 1;
        if (target.isEmpty() && i + 1 < words.size()) { target = words[i + 1]; span = 2; }
        if (!target.isEmpty() && !target.startsWith(masked('&')) && target != "/dev/null" && !target.startsWith("/dev/std")) writes = true;
        words.remove(i, span);
    }
    auto withWrites = [writes](const QString &activity) {
        if (!writes) return activity;
        return activity.isEmpty() || activityClasses().indexOf(activity) > activityClasses().indexOf("editing") ? QString("editing") : activity;
    };
    if (words.isEmpty()) return withWrites({});
    static const QSet<QString> neutral{"cd", "pushd", "popd", "echo", "printf", "export", "set", "unset", "true", "false",
                                       ":", "test", "[", "[[", "sleep", "read", "wait", "clear"};
    static const QSet<QString> searching{"grep", "egrep", "fgrep", "rg", "ag", "ack", "find", "fd", "fdfind", "locate", "ls", "tree"};
    static const QSet<QString> reading{"cat", "bat", "head", "tail", "less", "more", "wc", "nl", "sort", "uniq", "cut", "tr",
                                       "column", "paste", "diff", "cmp", "file", "stat", "jq", "yq", "xxd", "od", "hexdump",
                                       "strings", "readlink", "realpath", "basename", "dirname", "pwd", "which", "type"};
    static const QSet<QString> browsing{"curl", "wget", "http", "https", "xh", "gh"};
    static const QSet<QString> editing{"tee", "mv", "cp", "mkdir", "touch", "rm", "rmdir", "ln", "chmod", "chown", "truncate", "patch"};
    static const QSet<QString> delegating{"claude", "codex", "opencode", "gemini", "aider"};
    const auto program = words.takeFirst();
    // Leftovers such as the < of a process substitution are not programs; "." sources a script.
    if (neutral.contains(program) || (program != "." && !std::any_of(program.begin(), program.end(), [](QChar c) { return c.isLetterOrNumber(); })))
        return withWrites({});
    if (searching.contains(program)) return withWrites("searching");
    if (editing.contains(program)) return "editing";
    if (delegating.contains(program)) return withWrites("delegating");
    if (browsing.contains(program)) return withWrites("browsing");
    auto any = [&words](const QRegularExpression &re) {
        return std::any_of(words.begin(), words.end(), [&re](const QString &w) { return re.match(w).hasMatch(); });
    };
    static const QRegularExpression sedInPlace(R"(^(-[A-Za-z]*i|--in-place))"), perlInPlace(R"(^-[a-z0-9]*i)");
    if (program == "sed") return withWrites(any(sedInPlace) ? "editing" : "reading");
    if (program == "perl") return withWrites(any(perlInPlace) ? "editing" : "reading");
    if (program == "awk" || program == "gawk")
        return withWrites(std::any_of(words.begin(), words.end(), [](const QString &w) { return w.contains("inplace"); }) ? "editing" : "reading");
    if (reading.contains(program)) return withWrites("reading");
    if (program != "git") return "executing";
    const auto at = gitSubcommand(words);
    if (at < 0) return "executing";
    const auto sub = words[at];
    const auto rest = words.mid(at + 1);
    if (sub == "grep" || sub == "ls-files") return withWrites("searching");
    if (sub == "fetch" || sub == "ls-remote") return withWrites("browsing");
    if (sub == "branch") {
        const bool changes = std::any_of(rest.begin(), rest.end(), [](const QString &w) {
            return QStringList{"-d", "-D", "-m", "-M", "-c", "-C", "--delete", "--move", "--copy"}.contains(w);
        });
        return withWrites(changes ? "editing" : "reading");
    }
    if (sub == "stash") return withWrites(!rest.isEmpty() && (rest.first() == "list" || rest.first() == "show") ? "reading" : "editing");
    if (sub == "remote") return withWrites(rest.isEmpty() || rest.first() == "-v" ? "reading" : "executing");
    static const QSet<QString> gitReading{"diff", "log", "show", "status", "blame", "shortlog", "reflog", "describe", "rev-parse",
                                          "ls-tree", "cat-file"};
    static const QSet<QString> gitEditing{"add", "apply", "am", "commit", "mv", "rm", "restore", "checkout", "switch",
                                          "cherry-pick", "revert", "merge", "rebase"};
    if (gitReading.contains(sub)) return withWrites("reading");
    if (gitEditing.contains(sub)) return "editing";
    return "executing";
}
// Here-document bodies are data, not commands: drop the lines after <<WORD up to WORD.
static QString withoutHereDocuments(const QString &command) {
    static const QRegularExpression start(R"((?<!<)<<-?\s*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\1)");
    QStringList kept, ends;
    for (const auto &line : command.split('\n')) {
        if (!ends.isEmpty()) { if (line.trimmed() == ends.first()) ends.removeFirst(); continue; }
        kept += line;
        for (auto it = start.globalMatch(line); it.hasNext();) ends += it.next().captured(2);
    }
    return kept.join('\n');
}
QString commandActivity(const QString &command) {
    QList<QStringList> commands; simpleCommands(withoutHereDocuments(command), commands);
    qsizetype best = -1;
    for (const auto &words : commands) {
        const auto rank = activityClasses().indexOf(simpleActivity(words));
        if (rank >= 0 && (best < 0 || rank < best)) best = rank;
    }
    return best < 0 ? QString("executing") : activityClasses()[best];
}
QJsonObject normalizeLegacyHook(const QString &provider, const QJsonObject &in, qint64 now) {
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
    if (provider == "claude" && name == "Stop" && agent.isEmpty() &&
        !in.value("background_tasks").toArray().isEmpty()) out["waiting"] = true;
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
        // The tool name stays here; only its class travels (reading, searching, editing, executing, browsing,
        // delegating, or working for anything else, MCP tools included).
        out["activity"] = toolActivity(in.value("tool_name").toString());
        // A shell tool's command line, as one string or an argument list, is checked here and dropped.
        const auto command = in.value("tool_input").toObject().value("command");
        QString line = command.toString();
        if (command.isArray()) for (const auto &part : command.toArray()) line += part.toString() + ' ';
        if (destructiveCommand(line)) out["risky"] = true;
        // A shell tool is classed by what its command line does (grep searches, sed -i edits).
        if (out["activity"].toString() == "executing" && !line.trimmed().isEmpty()) out["activity"] = commandActivity(line);
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
    return out;
}
}
