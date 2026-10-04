#pragma once
namespace pet {
// In-memory prototype defaults; persistence and monitor-layout recovery are M2.
struct Preferences {
    static constexpr int defaultSize = 240;
    static constexpr int recoveryMs = 15000;
};
}
