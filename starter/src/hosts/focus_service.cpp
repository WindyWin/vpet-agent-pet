#include "focus_service.h"

namespace pet::hosts {
void FocusService::addActivation(std::unique_ptr<Activation> activation) {
    const auto id = activation->id();
    activations_[id] = std::move(activation);
}
void FocusService::addBackend(std::unique_ptr<platform::DesktopBackend> backend) { backends_.push_back(std::move(backend)); }
Activation *FocusService::find(const QString &id) const {
    const auto it = activations_.find(id);
    return it == activations_.end() ? nullptr : it->second.get();
}
// The most telling failure is reported when no backend raises a window.
static int severity(Outcome outcome) {
    switch (outcome) {
    case Outcome::Unsupported: return 1;
    case Outcome::MissingTarget: return 2;
    case Outcome::TargetNotFound: return 3;
    case Outcome::TimedOut: return 4;
    case Outcome::Failed: return 5;
    default: return 0;
    }
}
FocusResult FocusService::focus(HostContext host, const QString &project) {
    FocusResult result;
    if (host.isNull()) { result.activation = Outcome::MissingTarget; return result; }
    if (!registry_.find(host.adapter)) { result.selection = result.activation = Outcome::Unsupported; return result; }
    auto *activation = find(host.adapter);
    if (!activation) result.selection = Outcome::Unsupported;
    else if (!registry_.validTarget(host)) result.selection = Outcome::MissingTarget; // Nothing to select; still raise.
    else result.selection = activation->select(host);
    const bool selectionFailed = result.selection == Outcome::Failed || result.selection == Outcome::TimedOut ||
                                 result.selection == Outcome::Unsupported;
    if (activation && selectionFailed && !activation->raiseAfterFailedSelection()) return result;
    const auto windows = activation ? activation->windows(host) : QVector<HostContext>{host};
    for (const auto &window : windows) {
        if (window.adapter != host.adapter)
            if (auto *nested = find(window.adapter); nested && registry_.validTarget(window)) nested->select(window);
        for (const auto &backend : backends_) {
            const auto outcome = backend->activate({window.window, window.pids, project});
            if (platform::succeeded(outcome)) {
                result.activation = outcome; result.backend = backend->id();
                return result;
            }
            if (outcome == Outcome::Unsupported && !backend->requirement().isEmpty() &&
                !result.requirements.contains(backend->requirement()))
                result.requirements << backend->requirement();
            if (severity(outcome) > severity(result.activation)) result.activation = outcome;
        }
    }
    if (backends_.empty()) result.activation = Outcome::Unsupported;
    return result;
}
ActiveState FocusService::active(const HostContext &host, const QString &project) {
    if (host.isNull()) return ActiveState::Unknown;
    if (const auto *activation = find(host.adapter); activation && !activation->windowShowsSession()) return ActiveState::Unknown;
    for (const auto &backend : backends_) {
        const auto state = backend->active({host.window, host.pids, project});
        if (state != ActiveState::Unknown) return state;
    }
    return ActiveState::Unknown;
}
}
