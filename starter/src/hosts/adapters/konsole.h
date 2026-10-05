#pragma once
#include "hosts/focus_service.h"

namespace pet::hosts::konsole {
constexpr auto id = "konsole";
// Target "<D-Bus service>|<window path>|<session path>", from KONSOLE_DBUS_*.
struct Target { QString service, window; int session = -1; };
bool decode(const QString &target, Target &out);
// The tab a process runs in; malformed when it does not run in Konsole.
QString targetOf(const QProcessEnvironment &environment);
Capture capture();
// Selects the tab over Konsole's D-Bus interface. A failed selection still raises
// the window. Defined with the native backends: it needs D-Bus.
std::unique_ptr<Activation> activation();
}
