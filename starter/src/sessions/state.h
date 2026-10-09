#pragma once
#include "hosts/registry.h"
#include <QJsonObject>
#include <functional>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

namespace pet {
struct Event {
    QString provider, session, id, kind, tool, parent, project, activity;
    qint64 timestamp = 0;
    QString reason; // attention: "approval" or "input"; turn_failed: "limit" or "billing"; empty when unknown
    // Where the agent runs, captured by the hook so the pet can bring it forward:
    // the v1 wire fields, converted to a hosts::HostContext when applied.
    QString host, hostPids, hostWindow, hostTarget;
    // tool_start only: the hook saw a destructive shell command. A flag, never the command.
    bool risky = false;
    // turn_finished only: background work remains in flight.
    bool waiting = false;
    // custom only: what happened, such as "deploy_succeeded" (event_name.h). Names a reaction rule of a plugin pack (docs/plugins.md); never a session.
    QString name;
    // Host fields are checked against the registry's hosts (the built-in ones by default).
    static bool parse(const QByteArray &data, Event &event, QString &error,
                      const hosts::Registry &hosts = hosts::Registry::builtin());
};
struct Session {
    QString provider, id, parent, project, state = "idle", resume = "idle";
    QString reason; // Reason of the current attention request or quota stop, if any.
    QSet<QString> attentionTools; // Tools awaiting the user's answer; the first to finish resolves the attention.
    bool interrupted = false; // Set by an interrupt; tool callbacks of that turn are stale until the next prompt.
    hosts::HostContext host; // Null until an event identifies the host; kept until another one does.
    QMap<QString, QString> tools;
    qint64 timestamp = 0, seen = 0, reactionUntil = 0, activityUntil = 0;
    qint64 turnStarted = 0; // Timestamp of the prompt that started the current turn; 0 when unknown.
    qint64 lastTurnMs = 0; // How long the last finished turn ran, from its prompt; 0 when unknown.
};
// An accepted error or failed turn that counts as a failure (mood, recap) once applied to its session `s`.
// An error a waiting session ignored, a late callback of a tool its waiting finish cleared, does not.
inline bool failure(const Event &e, const Session *s) {
    return e.kind == "turn_failed" || (e.kind == "error" && !(s && s->state == "waiting"));
}
// Bubble order: requests, then quota stops, then errors, then finished turns.
inline int alertRank(const QString &kind) {
    return kind == "attention" ? 0 : kind == "exhausted" ? 1 : kind == "error" ? 2 : 3;
}
struct Alert {
    QString session, kind, project, provider, id, reason;
    qint64 created = 0;
    int count = 1;
    quint64 serial = 0; // Increases whenever this alert is created or aggregated.
    qint64 expires = 0; // Informational alerts fade on their own; 0 keeps the alert until resolved.
};
class Sessions {
public:
    static constexpr int maxSessions = 256, maxTools = 128, maxAlerts = 64, maxEvents = 4096;
    static constexpr qint64 expiryMs = 30 * 60 * 1000;
    // Finished turns and tool errors are reports, not requests: they fade so the
    // bubble does not pile up. Attention stays until the session resolves it.
    static constexpr qint64 finishedAlertMs = 6000, errorAlertMs = 10000;
    // Tool calls are often shorter than an animation phase; the last activity is
    // held this long after its tool ends so back-to-back tools read as one stretch.
    static constexpr qint64 activityHoldMs = 4000;
    QByteArray checkpoint() const;
    bool restore(const QByteArray &data, qint64 now, const std::function<bool(const Session &)> &running);
    // False for a custom event, which is a reaction and never session state.
    bool apply(const Event &event, qint64 now);
    void expire(qint64 now);
    QString aggregate(qint64 now) const;
    QVector<Alert> pending() const;
    void dismiss(const QString &session, const QString &kind);
    int unresolvedAttention() const; // Sessions waiting on the user, regardless of dismissal.
    const QMap<QString, Session> &records() const { return sessions_; }
private:
    QMap<QString, Session> sessions_;
    QMap<QString, qint64> ended_, events_;
    QVector<Alert> alerts_;
    quint64 serial_ = 0;
};
}
