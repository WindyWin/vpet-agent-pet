#include "event_rules.h"
#include "sessions/event_name.h"
#include <algorithm>

namespace pet {
const QStringList &EventRules::agentEvents() {
    static const QStringList kinds{"session_start", "prompt", "attention", "error", "turn_finished", "turn_failed", "interrupt", "session_end"};
    return kinds;
}
bool EventRules::validTrigger(const QString &on) {
    if (on.startsWith("custom:")) return validEventName(on.mid(7));
    return agentEvents().contains(on);
}
void EventRules::add(const QVector<EventRule> &rules) { rules_ += rules; }
std::optional<EventReaction> EventRules::pick(const QString &trigger, qint64 now, const Random &random) const {
    if (restUntil_.value(trigger) > now) return std::nullopt;
    const auto recent = std::count_if(recent_.begin(), recent_.end(), [now](qint64 at) { return at > now - 60 * 1000; });
    if (recent >= maxPerMinute) return std::nullopt;
    int total = 0;
    for (const auto &rule : rules_) if (rule.on == trigger) total += rule.weight;
    if (!total) return std::nullopt;
    int roll = total == 1 ? 0 : random(total);
    for (const auto &rule : rules_) {
        if (rule.on != trigger) continue;
        if (roll < rule.weight) return EventReaction{rule.pack, trigger, rule.state, rule.say, rule.cooldownMs};
        roll -= rule.weight;
    }
    return std::nullopt;
}
void EventRules::commit(const EventReaction &reaction, qint64 now) {
    restUntil_[reaction.trigger] = now + reaction.cooldownMs;
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [now](qint64 at) { return at <= now - 60 * 1000; }), recent_.end());
    recent_.append(now);
}
}
