#include "platform/native.h"
#include "i18n/contexts.h"
#include "platform/contracts/native_window.h"
#include "platform/posix/commands.h"
#include "platform/unsupported/unsupported.h"
#include "hosts/adapters/herdr.h"
#include "hosts/adapters/tmux.h"
#include "process.h"
#include <QGuiApplication>
#include <CoreGraphics/CoreGraphics.h>

namespace pet::platform {
std::unique_ptr<hosts::FocusService> createFocusService() {
    auto service = std::make_unique<hosts::FocusService>(hosts::Registry::builtin());
    const auto commands = std::make_shared<PosixCommandRunner>();
    const auto processes = std::make_shared<MacProcesses>();
    // Multiplexer panes are selected with their own CLIs; raising the terminal window is not implemented yet.
    service->addActivation(hosts::tmux::activation(commands, processes));
    service->addActivation(hosts::herdr::activation(commands, processes));
    service->addLocator(hosts::herdr::locator(commands, hosts::herdr::clientSocket(QProcessEnvironment::systemEnvironment(), {})));
    service->addBackend(std::make_unique<unsupported::Desktop>("macos", Focus::tr("Agent Pet cannot bring windows forward on macOS yet.")));
    return service;
}
std::function<bool()> createScreenLockQuery(QObject *) {
    return [] {
        const CFDictionaryRef session = CGSessionCopyCurrentDictionary();
        if (!session) return false;
        const auto value = CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked"));
        const bool locked = value && CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
        CFRelease(session);
        return locked;
    };
}
// A system move (performWindowDrag) can consume the release before Qt sees it. Only the Cocoa
// platform has a real pointer; other Qt platforms (offscreen tests) keep Qt's synthesized state.
std::optional<bool> nativeLeftButtonDown() {
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) return std::nullopt;
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
}
std::optional<QPoint> nativeWindowOrigin(quintptr) { return std::nullopt; }
}
