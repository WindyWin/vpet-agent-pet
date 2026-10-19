#pragma once
#include "interactions/service.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

namespace pet {
// The composer: Enter adds a line; Ctrl+Enter (Cmd+Enter on macOS) sends. Qt does not deliver keys while an
// input method is composing, so confirming a candidate never sends.
class ComposerEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
signals:
    void sendRequested();
protected:
    void keyPressEvent(QKeyEvent *) override;
};

// The expanded conversation: one agent's messages, its open question and a composer, for exactly one
// conversation at a time. It reads everything from the interaction service and submits through it, so a
// draft, a request or a delivery result is the service's, never a second copy.
class ConversationPanel : public QWidget {
    Q_OBJECT
public:
    explicit ConversationPanel(interactions::Service &service, QWidget *parent = nullptr);
    ~ConversationPanel() override;
    // Shows this conversation and gives the panel keyboard focus: only an explicit user action calls it.
    void open(const interactions::ConversationKey &key);
    const interactions::ConversationKey &current() const { return current_; }
    void place(const QRect &pet, const QVector<QRect> &screens);

    // What the panel shows, for tests and accessibility checks.
    QString heading() const { return heading_->text(); }
    QString status() const { return status_->text(); }
    QString feedback() const { return feedback_->text(); }
    QString question() const { return questionText_; }
    QString composerText() const { return edit_->toPlainText(); }
    QString placeholder() const { return edit_->placeholderText(); }
    int messageCount() const { return messages_.size(); }
    int optionCount() const { return options_.size(); }
    bool customOption() const { return custom_ != nullptr; }
    bool requestShown() const { return !card_->isHidden(); }
    bool sendEnabled() const { return send_->isEnabled(); }
    bool composerEnabled() const { return edit_->isEnabled(); }
    bool foldable(int message) const;
    bool folded(int message) const;
    void toggleFold(int message);
    int requestChoices() const { return selector_->isHidden() ? 1 : selector_->count(); }
    void chooseRequest(int index) { selector_->setCurrentIndex(index); }
    void chooseOption(int index);
    void chooseCustom();
    void typeText(const QString &text) { edit_->setPlainText(text); }
    void send();
    void markVisibleRead();
    void collapse();
signals:
    void collapsed();
    void openRequested(const interactions::ConversationKey &key); // Bring the agent's own terminal or editor forward.
protected:
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void changeEvent(QEvent *) override;
private:
    struct MessageView {
        QWidget *box = nullptr;
        QLabel *text = nullptr;
        QPushButton *fold = nullptr;
        QString id;
        bool agent = false, foldable = false;
    };
    void retranslate();
    void refresh();
    void rebuildBody(const interactions::Conversation &c);
    void showRequest(const interactions::Conversation &c);
    void updateComposer(const interactions::Conversation &c);
    void applyFold(MessageView &view, bool folded);
    void onResult(const interactions::CommandResult &result);
    QString draftSlot() const;
    const interactions::InputRequest *selectedRequest(const interactions::Conversation &c) const;
    bool answeringWithText() const;

    interactions::Service &service_;
    int observer_ = 0;
    interactions::ConversationKey current_;
    // A command is answered for the conversation it went to, whichever one the panel shows when the result arrives.
    struct Flight { interactions::ConversationKey key; QString slot; };
    QHash<QString, Flight> flights_;  // Command ID → where it was sent and which draft it came from.
    QString sending_;                 // The command in flight for the conversation on screen, if any.
    QString shownSlot_;   // Which draft the composer currently holds, so a refresh never overwrites typing.
    QString bodySignature_, requestSignature_, activeRequest_, questionText_;
    QHash<QString, QString> chosen_; // Request ID → option ID picked but not sent (never transmitted by itself).
    QSet<QString> expanded_;         // Conversation + message IDs opened by the user.
    bool loading_ = false, stickToEnd_ = true;
    QLabel *heading_, *status_, *banner_, *question_, *feedback_, *composerLabel_;
    QPushButton *open_, *collapse_, *send_;
    QScrollArea *scroll_;
    QWidget *body_, *card_, *optionBox_;
    QVBoxLayout *messageLayout_, *optionLayout_;
    QWidget *answered_ = nullptr;
    QVBoxLayout *answeredLayout_;
    QComboBox *selector_;
    ComposerEdit *edit_;
    QButtonGroup group_;
    QVector<MessageView> messages_;
    QVector<QRadioButton *> options_;
    QRadioButton *custom_ = nullptr;
};
}
