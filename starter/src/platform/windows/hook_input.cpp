#include "platform/contracts/hook_input.h"
#include "process.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <windows.h>

namespace pet::platform {
qint64 parentProcessId() {
    const auto chain = processAncestors(QCoreApplication::applicationPid(), 2);
    return chain.value(1);
}
// Agents start hooks without a console window (Codex passes CREATE_NO_WINDOW), so the hook
// borrows the console of its nearest ancestor that has one, which is the agent's. A conhost
// console has its own window; under ConPTY (Windows Terminal, including consoles handed off
// to it) the console window is a hidden pseudo window owned by the terminal's window. Process
// ancestry cannot find either: conhost is no ancestor, and a handed-off shell's parent is
// Explorer. The first console found decides, so a terminal that started the agent's host
// (VS Code opened from a console) never lends its window.
WindowRef agentConsoleWindow(const QVector<qint64> &ancestors) {
    FreeConsole();
    for (const auto pid : ancestors) {
        if (!AttachConsole(DWORD(pid))) continue;
        const HWND console = GetConsoleWindow();
        FreeConsole();
        if (!console) continue; // A console without a window, like the agent's hook shell.
        const HWND window = GetAncestor(console, GA_ROOTOWNER);
        if (window && IsWindowVisible(window)) return {windowsBackend, QString::number(quintptr(window))};
        return {};
    }
    return {};
}
// Agents write the event to an anonymous pipe and close it. Pipes cannot be waited on, so the
// deadline is kept by peeking; a redirected file never blocks.
bool readHookInput(int inputLimit, QByteArray &data, QString &error) {
    data.clear();
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE) { error = "Cannot read event"; return false; }
    const auto type = GetFileType(input);
    if (type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) { error = "Cannot read event"; return false; }
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 150) {
        DWORD available = 0;
        if (type == FILE_TYPE_PIPE && !PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return true; // The writer closed: end of input.
            error = "Cannot read event"; return false;
        }
        if (type == FILE_TYPE_PIPE && !available) { Sleep(1); continue; }
        char buffer[8193];
        DWORD size = 0;
        if (!ReadFile(input, buffer, sizeof(buffer), &size, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return true;
            error = "Cannot read event"; return false;
        }
        if (size == 0) return true;
        data.append(buffer, int(size));
        if (data.size() > inputLimit) { error = "Input exceeds size limit"; return false; }
    }
    error = "Timed out reading event";
    return false;
}
}
