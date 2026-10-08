#include "runtime.h"
#include <algorithm>
#include <utility>

namespace pet::behavior {
namespace {
int rank(Policy policy) { return int(policy); }
// Classes that cut what is showing rather than letting it finish its exit.
bool cuts(Policy policy) {
    return policy == Policy::Shutdown || policy == Policy::Urgent || policy == Policy::Startup || policy == Policy::Surprise ||
           policy == Policy::Reminder;
}
bool same(const Intent &a, const Intent &b) { return a.source == b.source && a.key == b.key; }
bool session(const Intent &intent) { return intent.policy == Policy::Urgent || intent.policy == Policy::Activity; }
bool discretionary(Policy policy) { return policy == Policy::Surprise || policy == Policy::Celebration || policy == Policy::Ambient; }
}

Runtime::Runtime(std::function<qint64()> clock) : clock_(std::move(clock)) {}

QString Runtime::activity() const { return activity_ ? activity_->intent.cue : QString(); }
bool Runtime::urgent() const { return activity_ && activity_->intent.policy == Policy::Urgent; }
bool Runtime::idleLike() const { return !activity_ || activity_->intent.cue == "idle" || activity_->intent.cue == "waiting"; }

std::optional<quint64> Runtime::current() const {
    if (showing_) return showing_->instance;
    if (rest_ && idleLike()) return rest_->instance;
    if (activity_) return activity_->instance;
    return std::nullopt;
}
QString Runtime::currentCue() const {
    if (showing_) return showing_->intent.cue;
    if (rest_ && idleLike()) return rest_->intent.cue;
    return activity();
}

Runtime::Gate Runtime::gate(const Intent &intent) const {
    const auto &c = context_;
    switch (intent.policy) {
    case Policy::Shutdown:
    case Policy::Urgent:
    case Policy::Activity:
    case Policy::Startup:
        return Gate::Now;
    case Policy::Surprise:
        return !c.visible || c.handled || urgent() ? Gate::Never : Gate::Now;
    case Policy::Reminder:
        return c.present && c.visible && !c.muted && !c.speaking && c.attention == 0 && !c.handled && !c.moving && !urgent()
            ? Gate::Now : Gate::Wait;
    case Policy::Celebration:
        if (!c.visible || urgent()) return Gate::Never;
        return c.handled ? Gate::Wait : Gate::Now;
    case Policy::Ambient:
        return !c.visible || c.handled || urgent() || !idleLike() ? Gate::Never : Gate::Now;
    }
    return Gate::Never;
}
bool Runtime::outranks(const Intent &intent) const { return !showing_ || rank(intent.policy) < rank(showing_->intent.policy); }

bool Runtime::admits(const Intent &intent) const {
    if (closing_) return false;
    if (session(intent) || intent.policy == Policy::Shutdown) return true;
    if (intent.lifetime == Lifetime::Persistent) return !showing_ && gate(intent) == Gate::Now;
    return gate(intent) == Gate::Now && outranks(intent);
}

void Runtime::tell(const Intent &intent, Outcome what) { if (outcome) outcome(intent, what); }
Runtime::Entry Runtime::make(const Intent &intent) {
    Entry entry;
    entry.intent = intent;
    entry.instance = nextInstance_++;
    entry.sequence = nextSequence_++;
    return entry;
}

void Runtime::request(Entry &entry, bool interrupt) {
    entry.requestedAt = clock_();
    presented_ = entry.instance;
    // A copy: presentation may report back at once, which can end the entry it was handed.
    const Request request{entry.instance, entry.intent.cue, entry.intent.state, interrupt};
    if (present) present(request);
}

void Runtime::admit(Entry entry) {
    if (showing_) {
        const auto old = std::move(*showing_);
        showing_.reset();
        tell(old.intent, Outcome::Interrupted);
    }
    showing_ = std::move(entry);
    tell(showing_->intent, Outcome::Admitted);
    if (showing_) request(*showing_, cuts(showing_->intent.policy));
}

void Runtime::end(Outcome what) {
    const auto old = std::move(*showing_);
    showing_.reset();
    tell(old.intent, what);
    if (old.intent.policy == Policy::Shutdown) { finish(); return; }
    resolve(true);
}

void Runtime::finish() {
    if (finished_) return;
    finished_ = true;
    if (finished) finished();
}

void Runtime::expire() {
    if (waiting_.isEmpty()) return;
    const auto now = clock_();
    for (int i = int(waiting_.size()) - 1; i >= 0; --i)
        if (waiting_[i].intent.expiresAt <= now) tell(waiting_.takeAt(i).intent, Outcome::Expired);
}

void Runtime::resolve(bool returning) {
    if (closing_) return; // Shutdown presents itself and nothing comes after it.
    expire(); // Whatever calls for a resolve, past its deadline is too late.
    // The first waiting one-shot that may run now, by rank and then by submission order.
    std::stable_sort(waiting_.begin(), waiting_.end(), [](const Entry &a, const Entry &b) {
        return rank(a.intent.policy) != rank(b.intent.policy) ? rank(a.intent.policy) < rank(b.intent.policy)
                                                              : a.sequence < b.sequence;
    });
    for (int i = 0; i < waiting_.size(); ++i) {
        if (!outranks(waiting_[i].intent)) break;
        if (gate(waiting_[i].intent) != Gate::Now) continue;
        admit(waiting_.takeAt(i));
        return;
    }
    if (showing_ || context_.handled) return; // Handling holds presentation until the user lets go.
    Entry *target = rest_ && idleLike() ? &*rest_ : activity_ ? &*activity_ : nullptr;
    if (!target || target->instance == presented_) return;
    if (target == &*activity_ && activity_->intent.lifetime == Lifetime::Moment && momentShown_) return;
    if (target == &*activity_ && activity_->intent.lifetime == Lifetime::Moment) momentShown_ = true;
    // Coming back to the activity after a reaction or a hold never cuts anything.
    request(*target, !returning && target->requestedAt < 0 && cuts(target->intent.policy));
}

Submission Runtime::submit(const Intent &intent) {
    if (closing_) return showing_ && same(showing_->intent, intent) ? Submission::Duplicate : Submission::Rejected;
    if (intent.policy == Policy::Shutdown) {
        closing_ = true;
        if (showing_) {
            const auto old = std::move(*showing_);
            showing_.reset();
            tell(old.intent, Outcome::Interrupted);
        }
        for (const auto &waiting : std::exchange(waiting_, {})) tell(waiting.intent, Outcome::Dropped);
        if (rest_) {
            const auto old = std::move(*rest_);
            rest_.reset();
            tell(old.intent, Outcome::Interrupted);
        }
        showing_ = make(intent);
        tell(intent, Outcome::Admitted);
        // No one would see the closing animation.
        if (!context_.visible) { showing_.reset(); tell(intent, Outcome::Completed); finish(); return Submission::Admitted; }
        request(*showing_, true);
        return Submission::Admitted;
    }
    if (session(intent)) {
        if (activity_ && same(activity_->intent, intent) && activity_->intent.cue == intent.cue && activity_->intent.policy == intent.policy)
            return Submission::Duplicate;
        const bool changed = !activity_ || activity_->intent.cue != intent.cue;
        activity_ = make(intent);
        momentShown_ = false;
        if (changed) // The turn a waiting celebration was for is old news.
            for (int i = int(waiting_.size()) - 1; i >= 0; --i)
                if (waiting_[i].intent.policy == Policy::Celebration) tell(waiting_.takeAt(i).intent, Outcome::Dropped);
        // Any session activity ends the pet's arrival, as it always has; urgent activity ends every reaction.
        if (showing_ && showing_->intent.policy == Policy::Startup) {
            const auto old = std::move(*showing_);
            showing_.reset();
            tell(old.intent, Outcome::Interrupted);
        }
        if (intent.policy == Policy::Urgent) {
            if (showing_ && rank(showing_->intent.policy) > rank(Policy::Urgent)) {
                const auto old = std::move(*showing_);
                showing_.reset();
                tell(old.intent, Outcome::Interrupted);
            }
            for (int i = int(waiting_.size()) - 1; i >= 0; --i)
                if (discretionary(waiting_[i].intent.policy)) tell(waiting_.takeAt(i).intent, Outcome::Dropped);
        }
        if (rest_ && !idleLike()) {
            const auto old = std::move(*rest_);
            rest_.reset();
            tell(old.intent, Outcome::Interrupted);
        }
        resolve();
        return Submission::Admitted;
    }
    if (intent.lifetime == Lifetime::Persistent) { // A rest.
        if (rest_ && same(rest_->intent, intent) && rest_->intent.cue == intent.cue) return Submission::Duplicate;
        if (showing_ || gate(intent) != Gate::Now) return Submission::Rejected;
        if (rest_) {
            const auto old = std::move(*rest_);
            rest_.reset();
            tell(old.intent, Outcome::Interrupted);
        }
        rest_ = make(intent);
        tell(intent, Outcome::Admitted);
        resolve();
        return Submission::Admitted;
    }
    if ((showing_ && same(showing_->intent, intent))
        || std::any_of(waiting_.begin(), waiting_.end(), [&](const Entry &entry) { return same(entry.intent, intent); }))
        return Submission::Duplicate;
    const auto gated = gate(intent);
    if (gated == Gate::Never) return Submission::Rejected;
    if (gated == Gate::Now && outranks(intent)) { admit(make(intent)); return Submission::Admitted; }
    if (intent.expiresAt > clock_() && waiting_.size() < maxDeferred) { waiting_.append(make(intent)); return Submission::Deferred; }
    return Submission::Rejected;
}

void Runtime::withdraw(const QString &source, const QString &key) {
    const auto matches = [&](const Intent &intent) { return intent.source == source && intent.key == key; };
    if (rest_ && matches(rest_->intent)) {
        const auto old = std::move(*rest_);
        rest_.reset();
        tell(old.intent, Outcome::Interrupted);
        resolve(true);
        return;
    }
    for (int i = 0; i < waiting_.size(); ++i)
        if (matches(waiting_[i].intent)) { tell(waiting_.takeAt(i).intent, Outcome::Dropped); return; }
}

void Runtime::setContext(const Context &context) {
    const bool released = context_.handled && !context.handled;
    context_ = context;
    resolve(released);
}

void Runtime::report(quint64 instance, Feedback feedback) {
    if (showing_ && showing_->instance == instance) {
        if (feedback == Feedback::Started) {
            if (showing_->started) return;
            showing_->started = true;
            // A celebration that begins stands in for the finished turn it celebrates.
            if (showing_->intent.policy == Policy::Celebration && activity_ && activity_->intent.lifetime == Lifetime::Moment)
                momentShown_ = true;
            tell(showing_->intent, Outcome::Started);
            return;
        }
        end(feedback == Feedback::Completed ? Outcome::Completed
            : feedback == Feedback::Interrupted ? Outcome::Interrupted : Outcome::Unavailable);
        return;
    }
    if (feedback == Feedback::Started || instance != presented_) return;
    if (rest_ && rest_->instance == instance) {
        // Something else took the pet out of its rest: the rest is over.
        const auto old = std::move(*rest_);
        rest_.reset();
        tell(old.intent, Outcome::Interrupted);
        presented_ = 0; // Shown again on the next tick.
        return;
    }
    // Presentation left the activity on its own; the next tick shows it again. A moment already played.
    if (activity_ && activity_->instance == instance) presented_ = 0;
}

void Runtime::tick() {
    const auto now = clock_();
    expire();
    if (showing_ && showing_->requestedAt >= 0
        && now - showing_->requestedAt >= (showing_->intent.policy == Policy::Shutdown ? shutdownTimeoutMs : oneShotTimeoutMs)) {
        end(Outcome::TimedOut);
        return;
    }
    resolve(true);
}
}
