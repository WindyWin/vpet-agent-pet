#include "conversations.h"
#include "conversation_text.h"
#include "i18n/contexts.h"
#include "pet_window.h"
#include <QDateTime>

namespace pet {
using namespace interactions;

ConversationHub::ConversationHub(PetWindow &window, Service &service, QObject *parent)
    : QObject(parent), window_(window), service_(service), panel_(service), preview_(), list_() {
    fade_.setSingleShot(true);
    fade_.setInterval(defaultFadeMs);
    connect(&fade_, &QTimer::timeout, this, &ConversationHub::fadeFired);
    observer_ = service_.subscribe([this](const Change &) { refresh(); });
    connect(&preview_, &ConversationPreview::viewRequested, this, [this] {
        Pick pick; int more = 0;
        if (this->pick(pick, more)) open(pick.key);
    });
    connect(&preview_, &ConversationPreview::hideRequested, this, [this] {
        // Hiding only puts this preview away: the question stays pending and the message stays unread.
        if (!shown_.isEmpty()) hidden_.insert(shown_);
        refresh();
    });
    connect(&preview_, &ConversationPreview::listRequested, this, &ConversationHub::toggleList);
    connect(&list_, &ConversationList::conversationRequested, this, &ConversationHub::open);
    connect(&panel_, &ConversationPanel::collapsed, this, [this] { window_.setWanderHold(false); refresh(); });
    connect(&panel_, &ConversationPanel::openRequested, this, [this](const ConversationKey &key) {
        if (focusHost) focusHost(key);
    });
    connect(&window_, &PetWindow::moved, this, &ConversationHub::layout);
    connect(&window_, &PetWindow::presenceChanged, this, &ConversationHub::refresh);
    connect(&window_, &PetWindow::conversationsRequested, this, &ConversationHub::toggleList);
    connect(&window_, &PetWindow::quitRequested, this, &ConversationHub::stop);
    window_.setConversationsAvailable(true);
}
ConversationHub::~ConversationHub() { service_.unsubscribe(observer_); }

void ConversationHub::stop() {
    stopped_ = true; fade_.stop();
    preview_.hide(); list_.hide(); panel_.hide(); window_.setWanderHold(false);
}

int ConversationHub::attention() const {
    int count = 0;
    for (const auto &key : service_.keys()) {
        const auto *c = service_.find(key);
        if (c && c->link != Conversation::Link::Ended && (c->needsAttention() || c->unreadCount() > 0)) ++count;
    }
    return count;
}

// The one subject worth a preview now: the most recent session with an unanswered question, else the most
// recent with a new message that has not faded or been hidden. The session the panel shows needs no preview.
bool ConversationHub::pick(Pick &out, int &more) const {
    bool found = false;
    more = 0;
    const bool panelOpen = panel_.isVisible();
    for (const auto &key : service_.keys()) {
        const auto *c = service_.find(key);
        if (!c || c->link == Conversation::Link::Ended) continue;
        Pick candidate{key, {}, {}, false};
        if (const auto *request = conversation::firstOpenRequest(*c); request && c->needsAttention()) {
            candidate.attention = true; candidate.id = key.id() + "|q|" + request->requestId + '|' + QString::number(request->revision);
            candidate.text = conversation::firstLine(request->question);
        } else {
            for (int i = c->messages.size() - 1; i >= 0; --i) {
                const auto &m = c->messages[i];
                if (m.read || m.role != Role::Agent) continue;
                candidate.id = key.id() + "|m|" + m.messageId; candidate.text = conversation::firstLine(m.text);
                break;
            }
            if (candidate.id.isEmpty()) continue;
        }
        if (panelOpen && key == panel_.current()) continue;
        // A subject that was hidden or faded still counts toward +N: it is unread or unanswered.
        const bool suppressed = hidden_.contains(candidate.id) || (!candidate.attention && faded_.contains(candidate.id));
        if (suppressed) { ++more; continue; }
        if (!found || (candidate.attention && !out.attention)) {
            if (found) ++more;
            out = candidate; found = true;
        } else ++more;
    }
    return found;
}

void ConversationHub::refresh() {
    if (stopped_ || refreshing_) return;
    refreshing_ = true;
    window_.setWanderHold(panel_.isVisible());
    if (list_.isVisible()) list_.present(service_);
    Pick pick; int more = 0;
    const bool found = this->pick(pick, more);
    // Alerts about the pet's own state, muting and a hidden pet govern ordinary messages; a question stays visible.
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const bool allowed = found && !window_.petHidden() && (pick.attention || !window_.quiet(now));
    if (!allowed) {
        preview_.hide(); fade_.stop(); shown_.clear();
        refreshing_ = false;
        return;
    }
    const auto *c = service_.find(pick.key);
    ConversationPreview::Content content;
    content.attention = pick.attention;
    content.title = pick.attention ? ::Conversations::tr("Needs your answer") : ::Conversations::tr("New message");
    content.name = c->title.isEmpty() ? conversation::shortId(pick.key) : c->title;
    content.body = hideExcerpts_ ? QString() : pick.text;
    content.more = more;
    content.tooltip = conversation::identityTooltip(*c);
    preview_.present(content);
    // The same subject delivered again changes nothing: no new timer, no restart.
    if (pick.id != shown_) {
        shown_ = pick.id;
        if (pick.attention) fade_.stop(); else fade_.start();
    }
    layout();
    if (!preview_.isVisible()) preview_.show();
    refreshing_ = false;
}

void ConversationHub::fadeFired() {
    if (hovering ? hovering() : preview_.underMouse()) { fade_.start(); return; } // Reading it: it waits.
    // It ran its course; the message stays unread in the list.
    if (!shown_.isEmpty()) faded_.insert(shown_);
    refresh();
}

void ConversationHub::layout() {
    if (stopped_) return;
    const auto figure = window_.figure();
    const auto screens = window_.screenAreas();
    QVector<QRect> taken = avoid ? avoid() : QVector<QRect>();
    if (preview_.isVisible()) preview_.place(figure, screens, taken);
    if (panel_.isVisible()) panel_.place(figure, screens);
    if (list_.isVisible()) list_.place(figure, screens, preview_.isVisible() ? QVector<QRect>{preview_.geometry()} : QVector<QRect>());
}

void ConversationHub::open(const ConversationKey &key) {
    if (stopped_) return;
    preview_.hide(); list_.hide(); fade_.stop(); shown_.clear();
    panel_.open(key);
    window_.setWanderHold(true);
    layout();
    refresh();
}

void ConversationHub::toggleList() {
    if (stopped_) return;
    if (list_.isVisible()) { list_.hide(); return; }
    list_.present(service_);
    list_.place(window_.figure(), window_.screenAreas(), preview_.isVisible() ? QVector<QRect>{preview_.geometry()} : QVector<QRect>());
    list_.show();
}
}
