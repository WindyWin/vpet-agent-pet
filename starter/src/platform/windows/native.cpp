#include "platform/native.h"
#include "platform/contracts/desktop.h"
#include "platform/contracts/native_window.h"
#include "platform/desktop/window_match.h"
#include "process.h"
#include <QGuiApplication>
#include <windows.h>

namespace pet::platform {
namespace {
constexpr auto windowsBackend = "windows";
// Top-level application windows, as the taskbar shows them: visible, unowned, not tool windows.
// Explorer's windows are left out: it is an ancestor of anything started from the Start menu,
// and never the agent's terminal.
QVector<WindowInfo> windows() {
    QVector<WindowInfo> listed;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
            return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        wchar_t title[512];
        const int length = GetWindowTextW(window, title, 512);
        reinterpret_cast<QVector<WindowInfo> *>(data)->append({QString::number(quintptr(window)), qint64(pid), QString::fromWCharArray(title, qMax(length, 0))});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&listed));
    QVector<qint64> pids;
    for (const auto &window : listed) if (!pids.contains(window.pid)) pids.append(window.pid);
    const auto names = processNames(pids);
    QVector<WindowInfo> result;
    for (const auto &window : listed)
        if (names.value(pids.indexOf(window.pid)).compare("explorer.exe", Qt::CaseInsensitive) != 0) result.append(window);
    return result;
}
QString preferred(const WindowRequest &request) { return request.window.backend == windowsBackend ? request.window.id : QString(); }
HWND handle(const QString &id) { return reinterpret_cast<HWND>(quintptr(id.toULongLong())); }

// Raises the agent's terminal window by its process ancestry. Windows lets the foreground
// application, which the pet is right after the user clicks it, give focus to another window.
class WindowsDesktop : public DesktopBackend {
public:
    QString id() const override { return windowsBackend; }
    Outcome activate(const WindowRequest &request) override {
        if (preferred(request).isEmpty() && request.pids.isEmpty()) return Outcome::MissingTarget;
        const auto id = matchWindow(preferred(request), request.pids, request.project, windows());
        if (id.isEmpty()) return Outcome::TargetNotFound;
        const auto window = handle(id);
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        if (!SetForegroundWindow(window)) return Outcome::Failed;
        return GetForegroundWindow() == window ? Outcome::Confirmed : Outcome::Requested;
    }
    ActiveState active(const WindowRequest &request) override {
        const auto foreground = GetForegroundWindow();
        if (!foreground) return ActiveState::Inactive;
        return handle(matchWindow(preferred(request), request.pids, request.project, windows())) == foreground
            ? ActiveState::Active : ActiveState::Inactive;
    }
};
}
std::unique_ptr<hosts::FocusService> createFocusService() {
    auto service = std::make_unique<hosts::FocusService>(hosts::Registry::builtin());
    // tmux and herdr do not run natively on Windows; terminal windows are raised directly.
    service->addBackend(std::make_unique<WindowsDesktop>());
    return service;
}
// The input desktop is "Default" while the user works; locking switches to Winlogon's secure
// desktop, which a user process cannot open.
std::function<bool()> createScreenLockQuery(QObject *) {
    return [] {
        const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        if (!desktop) return true;
        wchar_t name[64] = {};
        const bool named = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), nullptr);
        CloseDesktop(desktop);
        return named && _wcsicmp(name, L"Default") != 0;
    };
}
// A system move (startSystemMove) runs a modal loop that can swallow the release before Qt sees it.
// Only the native platform has a real pointer; other Qt platforms (offscreen tests) keep Qt's state.
std::optional<bool> nativeLeftButtonDown() {
    if (QGuiApplication::platformName() != QLatin1String("windows")) return std::nullopt;
    const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    return (GetAsyncKeyState(button) & 0x8000) != 0;
}
std::optional<QPoint> nativeWindowOrigin(quintptr) { return std::nullopt; }
}
