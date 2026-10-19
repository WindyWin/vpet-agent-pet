#pragma once
// Shared by the hook runtime and the session host; not part of the bridge's public surface.
#include <QString>
#include <QStringList>

namespace pet::claude::detail {

// $CLAUDE_CONFIG_DIR/sessions, otherwise ~/.claude/sessions: the per-process files Claude Code keeps for `claude agents`.
QString sessionsDirectory();

// The part of a session file the bridge reads (never its socket or key): identity and readiness.
struct SessionFile {
    qint64 pid = 0, startedAt = 0;
    QString sessionId, status; // status: idle, busy or waiting
    // Changes per Claude process (a resumed session is a new conversation), constant across host restarts.
    QString epoch() const { return QString::number(pid) + "." + QString::number(startedAt); }
};
bool readSessionFile(qint64 pid, SessionFile &file);

// The hook ↔ host relay endpoint of one session, in the private per-user directory.
QString relayEndpoint(const QString &sessionId);
// The pet's interaction stream: $AGENT_PET_INTERACTION_ENDPOINT, else the application's endpoint.
QString petEndpoint();

// `claude-interact host …`: runs until its Claude process ends, the session ends, or the pet stays away.
int runHost(const QStringList &args);

}
