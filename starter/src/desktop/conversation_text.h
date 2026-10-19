#pragma once
#include "interactions/model.h"
#include <QColor>
#include <QString>

// The words and colors of agent conversations, shared by the preview, the panel and the list so one session
// reads the same everywhere. Plain text only: message content is never interpreted as markup.
namespace pet::conversation {
QString providerName(const QString &provider);
QString shortId(const interactions::ConversationKey &key);
// "api-web · OpenCode · a12f": project, provider and short session ID.
QString heading(const interactions::Conversation &conversation);
QString identityTooltip(const interactions::Conversation &conversation);
// Connection and turn, in plain words: "Connected · Waiting for your answer".
QString status(const interactions::Conversation &conversation);
// The first nonempty line of a message, not an invented summary.
QString firstLine(const QString &text);
QString reasonText(const QString &reason);
// The question awaiting an answer first, else nothing.
const interactions::InputRequest *firstOpenRequest(const interactions::Conversation &conversation);
inline QColor pendingColor() { return QColor("#d9480f"); }
inline QColor unreadColor() { return QColor("#1971c2"); }
inline QColor failureColor() { return QColor("#c92a2a"); }
inline QColor neutralColor() { return QColor("#adb5bd"); }
}
