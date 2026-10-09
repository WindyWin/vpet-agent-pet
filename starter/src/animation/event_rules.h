#pragma once
#include "random.h"
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace pet {
// What a plugin pack's events.json says (docs/plugins.md): when something happens, play a state and/or say a remark.
// `on` is "custom:<name>" for a custom event (docs/events.md) or the kind of an agent event, such as "turn_finished".
struct EventRule {
    QString pack, on, state, say; // `state` names a state of the pet or the pack; either it or `say` is set.
    int weight = 1;
    qint64 cooldownMs = 0;
};
// A reaction chosen for an event.
struct EventReaction { QString pack, trigger, state, say; qint64 cooldownMs = 0; };

// The rules of every applied pack. Reactions, not session state: nothing here changes what the sessions show, and the
// behavior runtime decides whether a reaction may play at all. A noisy script cannot keep the pet reacting: a trigger
// that just reacted stays quiet for its rule's cooldown, and all triggers together are held to `maxPerMinute`.
// Time comes from the caller, so tests drive it by hand.
class EventRules {
public:
    static constexpr int maxRulesPerPack = 128;
    static constexpr int maxSayLength = 120;
    static constexpr qint64 defaultCooldownMs = 10 * 1000, minCooldownMs = 1000, maxCooldownMs = 60 * 60 * 1000;
    static constexpr int maxWeight = 1000;
    static constexpr int maxPerMinute = 12;
    // The agent events a rule may react to. Tool events are too frequent to be worth a reaction.
    static const QStringList &agentEvents();
    // Whether `on` is "custom:<name>" with a valid name, or one of agentEvents().
    static bool validTrigger(const QString &on);

    void add(const QVector<EventRule> &rules);
    bool isEmpty() const { return rules_.isEmpty(); }
    int size() const { return int(rules_.size()); }
    const QVector<EventRule> &rules() const { return rules_; }
    // The reaction for `trigger` at `now` (ms): one of its rules drawn by weight. Empty when no rule matches, or the
    // trigger or the pet as a whole has reacted too much lately. Changes nothing: call commit() once it plays.
    std::optional<EventReaction> pick(const QString &trigger, qint64 now, const Random &random = systemRandom()) const;
    // The reaction played: its trigger rests for the rule's cooldown and counts toward the per-minute limit.
    void commit(const EventReaction &reaction, qint64 now);
private:
    QVector<EventRule> rules_;
    QHash<QString, qint64> restUntil_;
    QVector<qint64> recent_; // When the last reactions played, oldest first.
};
}
