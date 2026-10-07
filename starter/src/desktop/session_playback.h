#pragma once
#include <QString>
namespace pet {
inline QString sessionAnimation(const QString &state) {
    if (state == "attention") return "needs_input";
    if (state == "exhausted") return "out_of_quota";
    if (state == "error") return "tool_error";
    if (state == "turn-finished") return "turn_finished";
    if (state == "waiting") return "idle";
    if (state == "inactive") return "sleeping";
    return state;
}
}
