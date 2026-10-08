#pragma once
#include "sessions/state.h"
namespace pet {
// Updater checkpoints for platforms with verifiable process identities. Unknown or exited agent processes are never restored.
bool saveSessions(const QString &path, const Sessions &sessions);
bool loadSessions(const QString &path, Sessions &sessions, qint64 now);
}
