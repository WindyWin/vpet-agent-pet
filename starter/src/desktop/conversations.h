#pragma once
#include "conversation_bubbles.h"
#include "conversation_panel.h"
#include "interactions/service.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

namespace pet {
class PetWindow;

// Presents the interaction service beside the pet: one collapsed preview at a time, one expanded panel, and
// the list of conversations. A question outranks a message; a new message never opens the panel or takes
// focus. It owns presentation state only (what was hidden, what faded): requests, drafts and read state
// stay in the service.
class ConversationHub : public QObject {
    Q_OBJECT
public:
    static constexpr int defaultFadeMs = 8000;
    ConversationHub(PetWindow &window, interactions::Service &service, QObject *parent = nullptr);
    ~ConversationHub() override;
    ConversationPanel &panel() { return panel_; }
    ConversationPreview &preview() { return preview_; }
    ConversationList &list() { return list_; }
    // Other bubbles the preview keeps clear of, and how a conversation's own terminal or editor is raised.
    std::function<QVector<QRect>()> avoid;
    std::function<bool(const interactions::ConversationKey &)> focusHost;
    // Asked when the agent's own window could not be raised: may the pet let the conversation go? The default
    // is a dialog; replaceable for tests.
    std::function<bool(const interactions::ConversationKey &)> confirmDetach;
    // Whether the pointer is on the preview, which holds its fade; replaceable for tests.
    std::function<bool()> hovering;
    // "Hide message previews": identity and state only, no text from the agent.
    void setHideExcerpts(bool hide) { hideExcerpts_ = hide; refresh(); }
    void setFadeMs(int ms) { fade_.setInterval(ms); }
    void open(const interactions::ConversationKey &key);
    void toggleList();
    // Sessions with a question or an unread message, for the tray.
    int attention() const;
    void refresh();
    void stop();
private:
    struct Pick {
        interactions::ConversationKey key;
        QString id; // The request or message this preview is about; also what "Hide" suppresses.
        QString text;
        bool attention = false;
    };
    bool pick(Pick &out, int &more) const;
    void openHost(const interactions::ConversationKey &key);
    bool askToDetach(const interactions::ConversationKey &key);
    void layout();
    void fadeFired();
    PetWindow &window_;
    interactions::Service &service_;
    int observer_ = 0;
    ConversationPanel panel_;
    ConversationPreview preview_;
    ConversationList list_;
    QSet<QString> hidden_, faded_; // Preview subjects the user hid, or that ran their course.
    QString shown_;                // Subject now previewed.
    QTimer fade_;
    bool hideExcerpts_ = false, stopped_ = false, refreshing_ = false;
};
}
