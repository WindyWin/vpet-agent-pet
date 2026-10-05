#include "platform/native.h"
#include "commands.h"
#include "hosts/adapters/herdr.h"
#include "hosts/adapters/konsole.h"
#include "hosts/adapters/tmux.h"
#include "platform/desktop/kwin/kwin_desktop.h"
#include "platform/desktop/x11/x11_desktop.h"
#include "process.h"

namespace pet::platform {
std::unique_ptr<hosts::FocusService> createFocusService() {
    auto service = std::make_unique<hosts::FocusService>(hosts::Registry::builtin());
    const auto commands = std::make_shared<LinuxCommandRunner>();
    const auto processes = std::make_shared<LinuxProcesses>();
    service->addActivation(hosts::konsole::activation());
    service->addActivation(hosts::tmux::activation(commands, processes));
    service->addActivation(hosts::herdr::activation(commands, processes));
    // Backends are chosen by what the session offers, not by distribution: X11/XWayland
    // first, then KWin's scripting API for native Wayland windows on Plasma 6.
    service->addBackend(std::make_unique<x11::X11Desktop>());
    service->addBackend(std::make_unique<kwin::KWinDesktop>());
    return service;
}
}
