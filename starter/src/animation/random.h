#pragma once
#include <QRandomGenerator>
#include <functional>

namespace pet {
// A uniform integer in [0, bound). Replaceable, so tests can script every draw.
using Random = std::function<int(int)>;
inline Random systemRandom() {
    return [](int bound) { return int(QRandomGenerator::global()->bounded(bound)); };
}
}
