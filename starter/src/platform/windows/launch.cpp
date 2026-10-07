#include "platform/contracts/startup.h"
#include "executables.h"
#include <QCoreApplication>
#include <QDir>
#include <windows.h>

namespace pet {
// One argument as the C runtime and CommandLineToArgvW parse it back.
static QString quoted(const QString &argument) {
    if (!argument.isEmpty() && !argument.contains(QLatin1Char(' ')) && !argument.contains(QLatin1Char('\t'))
        && !argument.contains(QLatin1Char('"')))
        return argument;
    QString result = "\"";
    int backslashes = 0;
    for (const QChar c : argument) {
        if (c == '\\') { ++backslashes; continue; }
        result += QString(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
        result += c;
        backslashes = 0;
    }
    return result + QString(backslashes * 2, '\\') + '"';
}
// No handles are inherited and no console is attached. The pet leaves the agent's job when the
// job allows it, so closing the agent does not close the pet; it starts in the system directory,
// like POSIX "/", so it never holds a project directory open.
bool launchDetached(const QString &executable, const QStringList &arguments) {
    const auto program = QDir::toNativeSeparators(executable);
    auto line = quoted(program);
    for (const auto &argument : arguments) line += ' ' + quoted(argument);
    auto commandLine = line.toStdWString();
    wchar_t directory[MAX_PATH];
    const auto length = GetSystemDirectoryW(directory, MAX_PATH);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
    auto create = [&](DWORD extra) {
        return CreateProcessW(reinterpret_cast<const wchar_t *>(program.utf16()), commandLine.data(), nullptr, nullptr, FALSE,
                              flags | extra, nullptr, length && length < MAX_PATH ? directory : nullptr, &startup, &process);
    };
    if (!create(CREATE_BREAKAWAY_FROM_JOB) && (GetLastError() != ERROR_ACCESS_DENIED || !create(0))) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
QString petExecutable() { return platform::guiExecutable(QCoreApplication::applicationFilePath()); }
}
