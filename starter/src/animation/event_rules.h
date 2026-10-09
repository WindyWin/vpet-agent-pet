#pragma once
#include "catalog.h"
#include "random.h"
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace pet {
// One rule of the table: when something happens, play a state and/or say a remark (docs/plugins.md). `on` is
// "custom:<name>" for a custom event (docs/events.md), the kind of an agent event such as "turn_finished", or one of
// the pet's own triggers (src/animation/triggers.json) such as "danger". The pet's own rules have no pack.
struct EventRule {
    QString pack, on;
    QString state; // A state of the pet or the pack, or empty.
    QString say;   // A remark, or empty. A rule plays a state or a cue's pool, says a remark, or both.
    int weight = 1;
    qint64 cooldownMs = 0;
    QString cue; // Instead of a state: a reaction cue whose pool the pet draws from, as its own rules do.
};
// A reaction chosen for an event.
struct EventReaction { QString pack, trigger, state, say; qint64 cooldownMs = 0; };

// A moment the pet raises itself, and how the app plays what a rule chose for it.
struct Trigger {
    enum Class { Surprise, Celebration, Fidget, Reminder };
    QString name;
    Class kind = Surprise;
    QStringList cues; // The pet's own rule draws from the first of these it has a pool for.
    // Whether a pack's remark can come with it: fidgets are silent, and a reminder says its own note.
    bool speaks() const { return kind == Surprise || kind == Celebration; }
};
const QVector<Trigger> &triggers(); // The pet's own triggers, by name.
const Trigger *findTrigger(const QString &name); // Null for a name that is not one of them.

// The rules a pet plays from: its own, one per trigger that one of its pools answers, then those of every applied pack.
// The code that raises a trigger keeps its timing and class, and the table decides what plays: the catalog through the
// pet's own rules, and the packs through theirs. Reactions, not session state: nothing here changes what the sessions
// show, and the behavior runtime decides whether a reaction may play at all.
// Custom and agent events come from outside, so they are limited: a trigger that just reacted stays quiet for its
// rule's cooldown, and all of them together are held to `maxPerMinute`. The pet paces its own triggers. Time comes from
// the caller, so tests drive it by hand.
class EventRules {
public:
    static constexpr int maxRulesPerPack = 128;
    static constexpr int maxSayLength = 120;
    static constexpr qint64 defaultCooldownMs = 10 * 1000, minCooldownMs = 1000, maxCooldownMs = 60 * 60 * 1000;
    static constexpr int maxWeight = 1000;
    static constexpr int maxPerMinute = 12;
    // The agent events a rule may react to. Tool events are too frequent to be worth a reaction.
    static const QStringList &agentEvents();
    // Whether `on` is "custom:<name>" with a valid name, one of agentEvents(), or one of the pet's triggers.
    static bool validTrigger(const QString &on);

    EventRules() = default; // No pools, so none of the pet's own rules.
    // The pet's own rules for the pools of its catalog.
    explicit EventRules(const QMap<QString, QVector<Reaction>> &pools);
    void add(const QVector<EventRule> &rules); // After the rules already there.
    bool isEmpty() const { return rules_.isEmpty(); }
    int size() const { return int(rules_.size()); }
    const QVector<EventRule> &rules() const { return rules_; }
    // Some rule answers `trigger`.
    bool answers(const QString &trigger) const;
    // One of the rules for `trigger`, drawn by weight, with a state drawn from its cue's pool where it names one. With a
    // single rule and a single state, nothing is drawn. Empty when no rule answers. For the pet's own triggers.
    std::optional<EventReaction> draw(const QString &trigger, const Random &random = systemRandom()) const;
    // draw() for a custom or agent event at `now` (ms), and empty when the trigger or the pet as a whole has reacted too
    // much lately. Changes nothing: call commit() once it plays.
    std::optional<EventReaction> pick(const QString &trigger, qint64 now, const Random &random = systemRandom()) const;
    // The reaction played: its trigger rests for the rule's cooldown and counts toward the per-minute limit.
    void commit(const EventReaction &reaction, qint64 now);
private:
    bool playable(const EventRule &rule) const;
    QMap<QString, QVector<Reaction>> pools_;
    QVector<EventRule> rules_;
    QHash<QString, qint64> restUntil_;
    QVector<qint64> recent_; // When the last reactions played, oldest first.
};
}
