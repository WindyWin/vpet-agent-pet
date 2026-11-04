#include "restart.h"

namespace pet::restart {
static std::function<void(int)> &handler() {
    static std::function<void(int)> value;
    return value;
}
void setHandler(std::function<void(int)> value) { handler() = std::move(value); }
bool available() { return static_cast<bool>(handler()); }
void request(int delayMs) {
    if (handler()) handler()(delayMs < 0 ? 0 : delayMs);
}
void cancel() {
    if (handler()) handler()(-1);
}
}
