#pragma once
#include "state.h"

namespace pet {
// Display text for one pending alert. The label never contains prompt text or tool data.
struct AlertText {
    QString title;   // "Needs approval", "Needs input", "Tool error", "Turn finished"
    QString label;   // "abc-web · Claude Code · b72c"
    QString tooltip; // Full project path, or an explicit "unavailable" note.
    QString name;    // "abc-web": the short form shown in the bubble itself.
};
QString providerName(const QString &provider);
// Shortest prefix (at least 4 characters) not shared with another session of the same provider.
QString shortSessionId(const QString &provider, const QString &id, const QVector<Alert> &context);
// Basename of the project path, with its parent directory appended when another
// alert uses the same basename for a different path.
QString projectName(const QString &path, const QVector<Alert> &context);
AlertText describe(const Alert &alert, const QVector<Alert> &context);

// One line of the running-sessions list. Subagent sessions fold into their parent.
struct SessionRow {
    QString key;     // Sessions::records() key
    QString name;    // "abc-web"
    QString detail;  // "Claude Code · b72c · Konsole"
    QString status;  // "Needs approval", "Working", "Idle · 3 min"
    QString state;   // Session state, for the status colour
    QString tooltip; // Project path
    int children = 0;
};
// Sorted: waiting on the user, then errors, then active, then idle and stopped.
QVector<SessionRow> sessionRows(const Sessions &sessions, qint64 now);
QString alertTitle(const Alert &alert); // "Needs approval", "Turn finished", ...

// Presentation cursor over Sessions::pending(). It holds no alert data, so a
// restarted monitor starts empty and resolved alerts disappear on the next sync.
class AlertQueue {
public:
    // Keeps the shown alert unless it disappeared or a higher-priority alert arrived.
    void sync(const QVector<Alert> &pending);
    bool empty() const { return pending_.isEmpty(); }
    const Alert *current() const;
    int more() const { return empty() ? 0 : pending_.size() - 1; }
    void next();
    // Hides the current alert. Session attention remains until the session resolves it.
    void dismiss(Sessions &sessions);
    static int rank(const QString &kind) { return kind == "attention" ? 0 : kind == "error" ? 1 : 2; }
private:
    int index() const;
    QVector<Alert> pending_;
    QString session_, kind_;
    quint64 serial_ = 0;
};
}
