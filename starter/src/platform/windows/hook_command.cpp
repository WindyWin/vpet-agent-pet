#include "platform/contracts/hook_command.h"
#include "executables.h"
#include "i18n/contexts.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <windows.h>

namespace pet {
static constexpr auto registration = "agent-pet-v1";
static QJsonArray hookArguments(const QString &provider) { return {"hook", "--provider", provider, "--registration", registration}; }
// Characters cmd.exe treats specially in an unquoted program token.
static const QRegularExpression &cmdSpecial() {
    static const QRegularExpression pattern(R"([\s"&|<>^%!(),;=])");
    return pattern;
}
// Claude Code spawns exec-form handlers (`args`, Claude Code 2.1.139 and newer) directly, with no
// shell, so nothing needs quoting. Codex runs `cmd.exe /d /c "<command>"`, where a quoted program
// never starts, so the program is unquoted: its 8.3 short path when the path has spaces.
QJsonObject hookHandler(const QString &executable, const QString &provider, QString &error) {
    const auto program = QDir::toNativeSeparators(executable);
    if (provider == "claude") return {{"type", "command"}, {"command", program}, {"args", hookArguments(provider)}};
    auto unquoted = program;
    if (unquoted.contains(cmdSpecial())) {
        wchar_t shortPath[MAX_PATH];
        const auto length = GetShortPathNameW(reinterpret_cast<const wchar_t *>(program.utf16()), shortPath, MAX_PATH);
        unquoted = length && length < MAX_PATH ? QString::fromWCharArray(shortPath, int(length)) : QString();
    }
    if (unquoted.isEmpty() || unquoted.contains(cmdSpecial())) {
        error = Integrations::tr("Codex cannot run a program from this folder. Install Agent Pet in a folder without spaces or symbols.");
        return {};
    }
    return {{"type", "command"}, {"command", unquoted + " hook --provider " + provider + " --registration " + registration}};
}
bool ownedHookHandler(const QJsonObject &handler, const QString &provider) {
    if (handler.value("type").toString() != "command" || !handler.value("command").isString()) return false;
    if (handler.contains("args")) return handler.value("args").toArray() == hookArguments(provider) && !handler.value("command").toString().isEmpty();
    static const QRegularExpression command(R"(^[^\s"&|<>^%!(),;=]+ hook --provider (claude|codex) --registration agent-pet-v1$)");
    const auto match = command.match(handler.value("command").toString());
    return match.hasMatch() && match.captured(1) == provider;
}
QString hookExecutable(const QString &application) { return platform::consoleExecutable(application); }
}
