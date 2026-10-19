#pragma once
#include "interactions/model.h"
#include <QColor>
#include <QString>

// The words and colors of agent conversations, shared by the preview, the panel and the list so one session
// reads the same everywhere. Agent text is Markdown, shown through Qt's own parser with raw HTML, images and
// clickable links removed: it is never interpreted as anything but formatted text.
namespace pet::conversation {
QString providerName(const QString &provider);
QString shortId(const interactions::ConversationKey &key);
// "api-web · OpenCode · a12f": project, provider and short session ID.
QString heading(const interactions::Conversation &conversation);
QString identityTooltip(const interactions::Conversation &conversation);
// Connection and turn, in plain words: "Connected · Waiting for your answer".
QString status(const interactions::Conversation &conversation);
// The first nonempty line of a message as plain words (Markdown marks removed), not an invented summary.
QString firstLine(const QString &text);
// Markdown as rich text for a QLabel (Qt::RichText); an image is replaced by its address as text.
QString markdownHtml(const QString &markdown);
// Whether the conversation list shows it: not ended or disconnected, and either waiting on the user, working,
// holding a draft or an unread message, or active within `idleMs` (OpenCode keeps a closed session's data, so
// a quiet one is taken to be closed until it speaks again).
constexpr qint64 idleListMs = 30 * 60 * 1000;
bool listed(const interactions::Conversation &conversation, qint64 now, qint64 idleMs = idleListMs);
QString reasonText(const QString &reason);
// The question awaiting an answer first, else nothing.
const interactions::InputRequest *firstOpenRequest(const interactions::Conversation &conversation);
inline QColor pendingColor() { return QColor("#d9480f"); }
inline QColor unreadColor() { return QColor("#1971c2"); }
inline QColor failureColor() { return QColor("#c92a2a"); }
inline QColor neutralColor() { return QColor("#adb5bd"); }
}
