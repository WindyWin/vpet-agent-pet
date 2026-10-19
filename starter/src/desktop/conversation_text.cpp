#include "conversation_text.h"
#include "i18n/contexts.h"
#include "providers/registry.h"
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>

namespace pet::conversation {
using namespace interactions;

QString providerName(const QString &provider) {
    if (const auto *known = findProvider(provider)) return known->name;
    return provider.isEmpty() ? provider : provider.left(1).toUpper() + provider.mid(1);
}
QString shortId(const ConversationKey &key) { return key.sessionId.right(4); }
QString heading(const Conversation &c) {
    return QString("%1 · %2 · %3").arg(c.title.isEmpty() ? shortId(c.key) : c.title, providerName(c.key.provider), shortId(c.key));
}
QString identityTooltip(const Conversation &c) {
    return ::Conversations::tr("%1\nProvider: %2\nSession: %3").arg(c.title, providerName(c.key.provider), c.key.sessionId);
}
const InputRequest *firstOpenRequest(const Conversation &c) {
    for (const auto &id : c.requestOrder) {
        const auto it = c.requests.constFind(id);
        if (it != c.requests.constEnd() && (it->state == RequestState::Pending || it->state == RequestState::Unknown)) return &*it;
    }
    return nullptr;
}
QString status(const Conversation &c) {
    if (c.link == Conversation::Link::Ended) return ::Conversations::tr("Session ended");
    if (c.link == Conversation::Link::Disconnected) return ::Conversations::tr("Disconnected");
    const QString connected = ::Conversations::tr("Connected");
    if (c.needsAttention()) return connected + " · " + ::Conversations::tr("Waiting for your answer");
    if (!c.ready) return connected + " · " + ::Conversations::tr("Working…");
    return connected + " · " + ::Conversations::tr("Ready for a follow-up");
}
static const QTextDocument::MarkdownFeatures markdownFlags(QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);

QString firstLine(const QString &text) {
    for (const auto &line : text.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QTextDocument doc;
        doc.setMarkdown(line, markdownFlags);
        // Images become U+FFFC in the plain text; a line of nothing but marks (a code fence) is skipped.
        const QString plain = doc.toPlainText().remove(QChar::ObjectReplacementCharacter).simplified();
        if (!plain.isEmpty()) return plain;
    }
    return {};
}
QString markdownHtml(const QString &markdown) {
    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setMarkdown(markdown, markdownFlags);
    // Nothing is fetched or drawn from an address the agent names: an image shows as its address.
    QVector<QPair<int, int>> images;
    for (auto block = doc.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (it.fragment().isValid() && it.fragment().charFormat().isImageFormat())
                images.append({it.fragment().position(), it.fragment().length()});
    for (int i = images.size() - 1; i >= 0; --i) {
        QTextCursor cursor(&doc);
        cursor.setPosition(images[i].first);
        cursor.setPosition(images[i].first + images[i].second, QTextCursor::KeepAnchor);
        const QString address = cursor.charFormat().toImageFormat().name();
        cursor.insertText(address.left(200), QTextCharFormat());
    }
    return doc.toHtml();
}
bool listed(const Conversation &c, qint64 now, qint64 idleMs) {
    if (c.link != Conversation::Link::Connected) return false;
    if (c.needsAttention() || c.unreadCount() > 0 || !c.ready || c.protectedFromEviction()) return true;
    return now - c.lastActivity <= idleMs;
}
QString reasonText(const QString &reason) {
    namespace r = interactions::reason;
    if (reason == r::disconnected) return ::Conversations::tr("The agent is disconnected.");
    if (reason == r::ended) return ::Conversations::tr("The session has ended.");
    if (reason == r::staleRevision) return ::Conversations::tr("The question changed. Review it and answer again.");
    if (reason == r::alreadyAnswered) return ::Conversations::tr("This question was already answered.");
    if (reason == r::alreadySubmitting) return ::Conversations::tr("An answer is already being sent.");
    if (reason == r::requestClosed || reason == r::unknownRequest) return ::Conversations::tr("This question is closed.");
    if (reason == r::notReady) return ::Conversations::tr("The agent is busy. Try again when it is ready.");
    if (reason == r::awaitingAnswer) return ::Conversations::tr("Answer the open question first.");
    if (reason == r::deliveryUnconfirmed || reason == r::timedOut)
        return ::Conversations::tr("Delivery not confirmed. Check the agent before sending again.");
    if (reason == r::tooLarge) return ::Conversations::tr("That is too long to send.");
    if (reason == r::invalidOption) return ::Conversations::tr("That option is no longer available.");
    if (reason == r::customNotAllowed) return ::Conversations::tr("This question does not take a typed answer.");
    if (reason == r::unsupported) return ::Conversations::tr("This agent cannot do that.");
    if (reason == "invalid_answer") return ::Conversations::tr("The agent did not accept that answer. Edit it and try again.");
    return ::Conversations::tr("Could not send (%1).").arg(reason);
}
}
