#include "wellness.h"
#include <algorithm>

namespace pet {
bool Wellness::validEyeMinutes(int minutes) {
    return std::find(std::begin(eyeChoices), std::end(eyeChoices), minutes) != std::end(eyeChoices);
}
bool Wellness::validWaterMinutes(int minutes) {
    return std::find(std::begin(waterChoices), std::end(waterChoices), minutes) != std::end(waterChoices);
}
bool Wellness::quietAt(const QDateTime &local) {
    const int hour = local.time().hour();
    return hour >= quietFrom || hour < quietUntil;
}
QString Wellness::note(const QString &reminder) {
    if (reminder == "eyes") return QString("Look at something far away for %1 seconds").arg(eyeRestSeconds);
    if (reminder == "water") return "Time for some water 💧";
    return {};
}
void Wellness::activity(qint64 now, bool user) {
    if (user) lastUser_ = std::max(lastUser_, now);
    else if (lastUser_ < 0 || now - lastUser_ >= breakMs) return; // Nobody has been there for a while.
    if (last_ >= 0 && now < last_) return; // Out of order: already counted.
    const qint64 gap = last_ < 0 ? breakMs : now - last_;
    if (gap >= breakMs) eyes_ = water_ = 0; // Back from a real break.
    else { const qint64 counted = std::min(gap, graceMs); eyes_ += counted; water_ += counted; }
    last_ = now;
}
// The part of the gap since the last activity that already counts.
qint64 Wellness::pending(qint64 now) const {
    if (last_ < 0 || now < last_) return 0;
    return std::min(now - last_, graceMs);
}
qint64 Wellness::counted(qint64 base, qint64 now) const {
    return last_ < 0 || now - last_ >= breakMs ? 0 : base + pending(now);
}
QString Wellness::due(qint64 now) const {
    if (eyeMinutes_ > 0 && eyesActiveMs(now) >= eyeMinutes_ * 60000LL) return "eyes";
    if (waterMinutes_ > 0 && waterActiveMs(now) >= waterMinutes_ * 60000LL) return "water";
    return {};
}
void Wellness::given(const QString &reminder, qint64 now) {
    // Starts over at `now`: the part of the current gap counted so far is taken back.
    if (reminder == "eyes") eyes_ = -pending(now);
    else if (reminder == "water") water_ = -pending(now);
}
}
