#pragma once
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

namespace pet {
struct Event {
    QString provider, session, id, kind, tool, parent, project, activity;
    qint64 timestamp = 0;
    QString reason; // attention only: "approval" or "input"; empty when unknown
    static bool parse(const QByteArray &data, Event &event, QString &error);
};
struct Session {
    QString provider, id, parent, project, state = "idle", resume = "idle";
    QMap<QString, QString> tools;
    qint64 timestamp = 0, seen = 0, reactionUntil = 0;
};
struct Alert {
    QString session, kind, project, provider, id, reason;
    qint64 created = 0;
    int count = 1;
    quint64 serial = 0; // Increases whenever this alert is created or aggregated.
};
class Sessions {
public:
    static constexpr int maxSessions = 256, maxTools = 128, maxAlerts = 64, maxEvents = 4096;
    static constexpr qint64 expiryMs = 30 * 60 * 1000;
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
