#include "monitor.h"
#include "session_playback.h"
#include <QApplication>
#include <QDateTime>

namespace pet {
Monitor::Monitor(PetWindow &window) : window_(window) {
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] { update(QDateTime::currentMSecsSinceEpoch()); });
    timer_.start();
    connect(&bubble_, &AlertBubble::nextRequested, this, &Monitor::next);
    connect(&bubble_, &AlertBubble::dismissRequested, this, &Monitor::dismiss);
    connect(&window_, &PetWindow::moved, this, [this] { if (bubble_.isVisible()) bubble_.place(window_.figure(), window_.screenAreas()); });
    connect(&window_, &PetWindow::notificationsChanged, this, &Monitor::refreshAlerts);
    connect(&window_, &PetWindow::quitRequested, this, &Monitor::stop);
}
bool Monitor::listen(QString &error) {
    auto receiver = std::make_unique<Receiver>();
    receiver->received = [this](const Event &event) { apply(event, QDateTime::currentMSecsSinceEpoch()); };
    if (!receiver->start(error)) return false;
    receiver_ = std::move(receiver);
    return true;
}
bool Monitor::apply(const Event &event, qint64 now) {
    if (!active_ || !sessions_.apply(event, now)) return false;
    observed_ = true; update(now);
    return true;
}
void Monitor::update(qint64 now) {
    if (!active_) return;
    sessions_.expire(now);
    refreshAlerts();
    if (!observed_ || window_.player().requestedState() == "closing") return;
    const auto state = sessions_.aggregate(now);
    const auto animation = sessionAnimation(state);
    if (state != lastAggregate_ || (!window_.player().isDragging() && state != "error" && state != "turn-finished" &&
                                    window_.player().requestedState() != animation)) {
        window_.player().select(animation, state == "attention" || state == "error");
        lastAggregate_ = state;
    }
}
void Monitor::refreshAlerts() {
    const auto pending = sessions_.pending();
    queue_.sync(pending);
    window_.setAttention(sessions_.unresolvedAttention());
    quint64 newest = heard_;
    for (const auto &alert : pending) newest = std::max(newest, alert.serial);
    const bool raised = newest > heard_;
    heard_ = newest;
    const auto *alert = queue_.current();
    if (!alert || window_.muted() || !active_) { bubble_.hide(); return; }
    if (raised && window_.sound()) QApplication::beep();
    bubble_.present(describe(*alert, pending), queue_.more());
    bubble_.place(window_.figure(), window_.screenAreas());
    if (!bubble_.isVisible()) bubble_.show();
}
void Monitor::next() { queue_.next(); refreshAlerts(); }
void Monitor::dismiss() { queue_.dismiss(sessions_); refreshAlerts(); }
void Monitor::stop() {
    active_ = false; timer_.stop(); receiver_.reset(); bubble_.hide();
}
}
