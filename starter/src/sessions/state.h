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
    QString reason; // attention only: "approval" or "input"; empty when unknown
    // Where the agent runs, captured by the hook so the pet can bring it forward:
    // the v1 wire fields, converted to a hosts::HostContext when applied.
    QString host, hostPids, hostWindow, hostTarget;
    // tool_start only: the hook saw a destructive shell command. A flag, never the command.
    bool risky = false;
    // Host fields are checked against the registry's hosts (the built-in ones by default).
    static bool parse(const QByteArray &data, Event &event, QString &error,
                      const hosts::Registry &hosts = hosts::Registry::builtin());
};
struct Session {
    QString provider, id, parent, project, state = "idle", resume = "idle";
    QString reason; // Reason of the current attention request, if any.
    QSet<QString> attentionTools; // Tools awaiting the user's answer; the first to finish resolves the attention.
    bool interrupted = false; // Set by an interrupt; tool callbacks of that turn are stale until the next prompt.
    hosts::HostContext host; // Null until an event identifies the host; kept until another one does.
    QMap<QString, QString> tools;
    qint64 timestamp = 0, seen = 0, reactionUntil = 0, activityUntil = 0;
    qint64 turnStarted = 0; // Timestamp of the prompt that started the current turn; 0 when unknown.
    qint64 lastTurnMs = 0; // How long the last finished turn ran, from its prompt; 0 when unknown.
};
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
