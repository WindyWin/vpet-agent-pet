#pragma once
#include <functional>

namespace pet::restart {
// Some changes (the chosen pet, installed or removed packs, plugin packs) are read only at startup. The running
// pet installs a handler that relaunches it; `request` then applies such a change without the user restarting by hand.
// Without a handler (tests, --no-persist, --smoke-test) nothing restarts and `available` is false, so callers keep
// telling the user to restart.
void setHandler(std::function<void(int delayMs)> handler); // A negative delay cancels a pending restart.
bool available();
// Relaunches after `delayMs`. A later request restarts the countdown, so several quick changes restart once.
void request(int delayMs = 1500);
void cancel();
}
