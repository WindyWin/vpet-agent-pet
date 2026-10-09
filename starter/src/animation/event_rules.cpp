#include "event_rules.h"
#include "sessions/event_name.h"
#include "triggers.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace pet {
const QVector<Trigger> &triggers() {
    static const QVector<Trigger> parsed = [] {
        static const QHash<QString, Trigger::Class> classes{{"surprise", Trigger::Surprise}, {"celebration", Trigger::Celebration},
                                                             {"fidget", Trigger::Fidget}, {"reminder", Trigger::Reminder}};
        QVector<Trigger> list;
        const auto entries = QJsonDocument::fromJson(QByteArray(triggersJson)).object()["triggers"].toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            const auto trigger = it.value().toObject();
            QStringList cues;
            for (const auto &cue : trigger["cues"].toArray()) cues << cue.toString();
            list.append({it.key(), classes.value(trigger["class"].toString()), cues});
        }
        return list;
    }();
    return parsed;
}
const Trigger *findTrigger(const QString &name) {
    const auto &list = triggers();
    const auto found = std::lower_bound(list.begin(), list.end(), name,
                                        [](const Trigger &trigger, const QString &key) { return trigger.name < key; });
    return found != list.end() && found->name == name ? &*found : nullptr;
}
const QStringList &EventRules::agentEvents() {
    static const QStringList kinds{"session_start", "prompt", "attention", "error", "turn_finished", "turn_failed", "interrupt", "session_end"};
    return kinds;
}
bool EventRules::validTrigger(const QString &on) {
    if (on.startsWith("custom:")) return validEventName(on.mid(7));
    return agentEvents().contains(on) || findTrigger(on);
}
EventRules::EventRules(const QMap<QString, QVector<Reaction>> &pools) : pools_(pools) {
    for (const auto &trigger : triggers())
        for (const auto &cue : trigger.cues)
            if (!pools_.value(cue).isEmpty()) { rules_.append({{}, trigger.name, {}, {}, 1, 0, cue}); break; }
}
void EventRules::add(const QVector<EventRule> &rules) { rules_ += rules; }
// A rule whose cue the pet has no pool for has nothing to play; the loader refuses one, so this only guards a table
// built by hand.
bool EventRules::playable(const EventRule &rule) const {
    return !rule.state.isEmpty() || !rule.say.isEmpty() || !pools_.value(rule.cue).isEmpty();
}
bool EventRules::answers(const QString &trigger) const {
    return std::any_of(rules_.begin(), rules_.end(), [&](const EventRule &rule) { return rule.on == trigger && playable(rule); });
}
std::optional<EventReaction> EventRules::draw(const QString &trigger, const Random &random) const {
    int total = 0;
    for (const auto &rule : rules_) if (rule.on == trigger && playable(rule)) total += rule.weight;
    if (!total) return std::nullopt;
    int roll = total == 1 ? 0 : random(total);
    for (const auto &rule : rules_) {
        if (rule.on != trigger || !playable(rule)) continue;
        if (roll >= rule.weight) { roll -= rule.weight; continue; }
        const auto state = rule.cue.isEmpty() ? rule.state : drawReaction(pools_.value(rule.cue), random);
        return EventReaction{rule.pack, trigger, state, rule.say, rule.cooldownMs};
    }
    return std::nullopt;
}
std::optional<EventReaction> EventRules::pick(const QString &trigger, qint64 now, const Random &random) const {
    if (restUntil_.value(trigger) > now) return std::nullopt;
    const auto recent = std::count_if(recent_.begin(), recent_.end(), [now](qint64 at) { return at > now - 60 * 1000; });
    if (recent >= maxPerMinute) return std::nullopt;
    return draw(trigger, random);
}
void EventRules::commit(const EventReaction &reaction, qint64 now) {
    restUntil_[reaction.trigger] = now + reaction.cooldownMs;
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [now](qint64 at) { return at <= now - 60 * 1000; }), recent_.end());
    recent_.append(now);
}
}
