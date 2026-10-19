#include "conversation_panel.h"
#include "alert_bubble.h"
#include "conversation_text.h"
#include "i18n/contexts.h"
#include "desktop/overlay.h"
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRadioButton>
#include <QScrollBar>
#include <QTimer>
#include <QUuid>

namespace pet {
using namespace interactions;

void ComposerEdit::keyPressEvent(QKeyEvent *event) {
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (enter && (event->modifiers() & Qt::ControlModifier)) { emit sendRequested(); return; }
    QPlainTextEdit::keyPressEvent(event);
}

static constexpr int foldedLines = 6;

ConversationPanel::ConversationPanel(Service &service, QWidget *parent) : QWidget(parent), service_(service) {
    // A bubble, not a window: no title bar or frame, drawn like the toast and the preview, and it takes the
    // keyboard because it holds the composer. Esc or the collapse button puts it away.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    overlay::prepare(this, true, true);
    resize(400, 480);
    setMinimumSize(320, 300);
    setObjectName("conversationPanel");
    setStyleSheet("#conversationPanel{background:transparent;} QLabel{color:#453324;}"
                  "QPushButton{color:#8a4b12; background:#f3e3c3; border:0; border-radius:8px; padding:4px 12px; font-weight:600; min-height:24px;}"
                  "QPushButton:hover{background:#ecd3a5;} QPushButton:disabled{color:#a8977f; background:#f6ecd8;}"
                  "QPushButton#send{color:#fff7e3; background:#8a4b12;} QPushButton#send:hover{background:#6f3b0d;}"
                  "QPushButton#send:disabled{color:#fff7e3; background:#c9b497;}"
                  "QPlainTextEdit, QComboBox{color:#453324; background:#fffdf6; border:1px solid #d8c4a0; border-radius:8px; padding:4px;}"
                  "QRadioButton{color:#453324; min-height:28px; spacing:8px;}"
                  // Drawn by hand: the platform style fills every indicator alike here, so a choice looked unmade.
                  "QRadioButton::indicator{width:14px; height:14px; border-radius:9px; border:2px solid #8a4b12; background:#fffdf6;}"
                  "QRadioButton::indicator:hover{background:#f3e3c3;}"
                  "QRadioButton::indicator:checked{background:qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
                  " stop:0 #8a4b12, stop:0.55 #8a4b12, stop:0.6 #fffdf6, stop:1 #fffdf6);}"
                  "QRadioButton::indicator:disabled{border-color:#c9b497; background:#f6ecd8;}"
                  "QRadioButton::indicator:checked:disabled{background:qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
                  " stop:0 #c9b497, stop:0.55 #c9b497, stop:0.6 #f6ecd8, stop:1 #f6ecd8);}"
                  "QRadioButton:checked{font-weight:600;}");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 16, 16, 16); root->setSpacing(8);

    auto *top = new QHBoxLayout;
    heading_ = new QLabel(this); heading_->setTextFormat(Qt::PlainText);
    heading_->setStyleSheet("font-weight:700; font-size:14px;");
    open_ = new QPushButton(this); collapse_ = new QPushButton("−", this);
    collapse_->setFixedWidth(36);
    top->addWidget(heading_, 1); top->addWidget(open_); top->addWidget(collapse_);
    root->addLayout(top);
    status_ = new QLabel(this); status_->setTextFormat(Qt::PlainText); status_->setStyleSheet("color:#6b5643;");
    banner_ = new QLabel(this); banner_->setWordWrap(true); banner_->setTextFormat(Qt::PlainText);
    banner_->setStyleSheet("color:#8a1f1f; background:#fde8e4; border-radius:8px; padding:6px;");
    banner_->hide();
    root->addWidget(status_); root->addWidget(banner_);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true); scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body_ = new QWidget; body_->setObjectName("body"); body_->setStyleSheet("#body{background:transparent;}");
    scroll_->setStyleSheet("QScrollArea{background:transparent;}");
    scroll_->viewport()->setAutoFillBackground(false); // The bubble is translucent: nothing may paint a square behind it.
    auto *bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 6, 0); bodyLayout->setSpacing(10);
    messageLayout_ = new QVBoxLayout; messageLayout_->setSpacing(10);
    bodyLayout->addLayout(messageLayout_);
    answered_ = new QWidget(body_);
    answeredLayout_ = new QVBoxLayout(answered_);
    answeredLayout_->setContentsMargins(0, 0, 0, 0); answeredLayout_->setSpacing(4);
    bodyLayout->addWidget(answered_);

    card_ = new QWidget(body_); card_->setObjectName("card");
    card_->setStyleSheet("#card{background:#fdecc8; border:1px solid #e0b255; border-radius:12px;}");
    auto *cardLayout = new QVBoxLayout(card_);
    cardLayout->setContentsMargins(12, 10, 12, 10); cardLayout->setSpacing(6);
    auto *cardTitle = new QLabel(card_); cardTitle->setObjectName("cardTitle");
    cardTitle->setStyleSheet("font-weight:700; color:#8a4b12;");
    selector_ = new QComboBox(card_);
    question_ = new QLabel(card_); question_->setWordWrap(true); question_->setTextFormat(Qt::RichText);
    question_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    question_->setOpenExternalLinks(false);
    connect(question_, &QLabel::linkHovered, question_, [this](const QString &url) { question_->setToolTip(url); });
    optionBox_ = new QWidget(card_);
    optionLayout_ = new QVBoxLayout(optionBox_);
    optionLayout_->setContentsMargins(0, 0, 0, 0); optionLayout_->setSpacing(0);
    cardLayout->addWidget(cardTitle); cardLayout->addWidget(selector_); cardLayout->addWidget(question_); cardLayout->addWidget(optionBox_);
    bodyLayout->addWidget(card_);
    bodyLayout->addStretch(1);
    card_->hide();
    scroll_->setWidget(body_);
    root->addWidget(scroll_, 1);

    composerLabel_ = new QLabel(this); composerLabel_->setStyleSheet("font-weight:600;");
    edit_ = new ComposerEdit(this);
    edit_->setFixedHeight(76); edit_->setTabChangesFocus(true);
    feedback_ = new QLabel(this); feedback_->setWordWrap(true); feedback_->setTextFormat(Qt::PlainText);
    feedback_->setAccessibleName(QString());
    send_ = new QPushButton(this); send_->setObjectName("send"); send_->setMinimumHeight(32);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(feedback_, 1); bottom->addWidget(send_);
    root->addWidget(composerLabel_); root->addWidget(edit_); root->addLayout(bottom);
    collapse_->setMinimumHeight(32); open_->setMinimumHeight(32);

    connect(collapse_, &QPushButton::clicked, this, &ConversationPanel::collapse);
    connect(open_, &QPushButton::clicked, this, [this] { emit openRequested(current_); });
    connect(send_, &QPushButton::clicked, this, &ConversationPanel::send);
    connect(edit_, &ComposerEdit::sendRequested, this, &ConversationPanel::send);
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_ || !current_.valid()) return;
        service_.setDraft(current_, draftSlot(), edit_->toPlainText());
        if (auto *c = service_.find(current_)) updateComposer(*c);
    });
    connect(selector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading_ || index < 0) return;
        activeRequest_ = selector_->itemData(index).toString();
        refresh();
    });
    connect(&group_, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *button, bool on) {
        if (loading_ || !on) return;
        if (button == custom_) chosen_.insert(activeRequest_, QString());
        else chosen_.insert(activeRequest_, button->property("optionId").toString());
        if (auto *c = service_.find(current_)) updateComposer(*c);
        if (button == custom_) edit_->setFocus();
    });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        stickToEnd_ = value >= scroll_->verticalScrollBar()->maximum() - 4;
        markVisibleRead();
    });
    observer_ = service_.subscribe([this](const Change &change) {
        if (change.kind == Change::Kind::CommandResult) {
            if (flights_.contains(change.result.commandId)) onResult(change.result);
            return;
        }
        if (current_.valid() && change.key == current_) refresh();
    });
    retranslate();
}
ConversationPanel::~ConversationPanel() { service_.unsubscribe(observer_); }

void ConversationPanel::retranslate() {
    setWindowTitle(::Conversations::tr("Agent conversation"));
    setAccessibleName(::Conversations::tr("Agent conversation"));
    open_->setText(::Conversations::tr("Open"));
    open_->setToolTip(::Conversations::tr("Bring the agent's terminal or editor forward"));
    collapse_->setToolTip(::Conversations::tr("Collapse (Esc)")); collapse_->setAccessibleName(::Conversations::tr("Collapse conversation"));
    send_->setToolTip(::Conversations::tr("Ctrl+Enter sends"));
    if (auto *title = card_->findChild<QLabel *>("cardTitle")) title->setText(::Conversations::tr("Needs your answer"));
    selector_->setAccessibleName(::Conversations::tr("Question to answer"));
    edit_->setAccessibleName(::Conversations::tr("Message to the agent"));
    if (current_.valid()) refresh();
}
void ConversationPanel::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) retranslate();
    QWidget::changeEvent(event);
}

QString ConversationPanel::draftSlot() const {
    const auto *c = service_.find(current_);
    const auto *request = c ? selectedRequest(*c) : nullptr;
    return request ? request->requestId : QString();
}
const InputRequest *ConversationPanel::selectedRequest(const Conversation &c) const {
    const auto it = c.requests.constFind(activeRequest_);
    if (it != c.requests.constEnd() && (it->state == RequestState::Pending || it->state == RequestState::Submitting || it->state == RequestState::Unknown))
        return &*it;
    return conversation::firstOpenRequest(c);
}
bool ConversationPanel::answeringWithText() const {
    const auto *c = service_.find(current_);
    const auto *request = c ? selectedRequest(*c) : nullptr;
    if (!request) return true; // A follow-up is always text.
    return request->options.isEmpty() || (custom_ && custom_->isChecked());
}

void ConversationPanel::open(const ConversationKey &key) {
    if (!(key == current_)) {
        // Another conversation: its own draft and selection, and none of this one's feedback.
        current_ = key; activeRequest_.clear(); requestSignature_.clear(); sending_.clear(); feedback_->clear();
        bodySignature_.clear(); shownSlot_.clear(); stickToEnd_ = true;
        // A command still in flight for this conversation keeps its composer busy: no second click can duplicate it.
        for (auto it = flights_.constBegin(); it != flights_.constEnd(); ++it)
            if (it->key == key && !it->unconfirmed) { sending_ = it.key(); feedback_->setText(::Conversations::tr("Sending…")); feedback_->setStyleSheet("color:#6b5643;"); }
    }
    refresh();
    show(); raise(); activateWindow();
}
void ConversationPanel::collapse() { hide(); emit collapsed(); }
void ConversationPanel::closeEvent(QCloseEvent *event) { event->ignore(); collapse(); }
void ConversationPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#453324"), 1.2)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 14, 14);
    const auto *c = service_.find(current_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(c && c->needsAttention() ? conversation::pendingColor() : conversation::unreadColor());
    painter.drawEllipse(QPointF(14, 28), 5, 5);
}
void ConversationPanel::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { collapse(); return; }
    QWidget::keyPressEvent(event);
}
void ConversationPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] { if (stickToEnd_) scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum()); markVisibleRead(); });
}
void ConversationPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &ConversationPanel::markVisibleRead);
}
void ConversationPanel::place(const QRect &pet, const QVector<QRect> &screens) {
    QRect screen = screens.value(0, pet);
    for (const auto &area : screens) if (area.contains(pet.center())) { screen = area; break; }
    // Never larger than the screen's usable area: the body scrolls instead.
    const QSize fit(qMin(width(), screen.width()), qMin(height(), screen.height()));
    if (fit != size()) resize(fit);
    overlay::place(this, AlertBubble::placement(pet, size(), screen));
}

// ---- Rendering ---------------------------------------------------------------------------------

void ConversationPanel::refresh() {
    const auto *c = service_.find(current_);
    loading_ = true;
    if (!c) {
        heading_->setText(::Conversations::tr("Conversation unavailable"));
        status_->setText(::Conversations::tr("This conversation is no longer available."));
        edit_->setEnabled(false); send_->setEnabled(false); card_->hide();
        loading_ = false;
        return;
    }
    heading_->setText(conversation::heading(*c));
    heading_->setToolTip(conversation::identityTooltip(*c));
    status_->setText(conversation::status(*c));
    status_->setAccessibleName(::Conversations::tr("Status: %1").arg(status_->text()));
    QStringList notes;
    if (c->historyGap) notes << ::Conversations::tr("Some earlier messages could not be loaded.");
    if (c->capacityExceeded) notes << ::Conversations::tr("Some messages could not be loaded.");
    if (c->unsupportedContent) notes << ::Conversations::tr("A question here is too large to show. Open the agent to answer it.");
    banner_->setText(notes.join('\n')); banner_->setVisible(!notes.isEmpty());
    rebuildBody(*c);
    showRequest(*c);
    updateComposer(*c);
    loading_ = false;
    QTimer::singleShot(0, this, [this] {
        if (stickToEnd_) scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
        markVisibleRead();
    });
}

void ConversationPanel::applyFold(MessageView &view, bool folded) {
    view.text->setMaximumHeight(folded ? view.text->fontMetrics().lineSpacing() * foldedLines + 2 : QWIDGETSIZE_MAX);
    view.fold->setText(folded ? ::Conversations::tr("Show more") : ::Conversations::tr("Show less"));
}
bool ConversationPanel::foldable(int message) const { return messages_.value(message).foldable; }
bool ConversationPanel::folded(int message) const {
    const auto view = messages_.value(message);
    return view.foldable && !expanded_.contains(current_.id() + view.id);
}
void ConversationPanel::toggleFold(int message) {
    if (message < 0 || message >= messages_.size() || !messages_[message].foldable) return;
    auto &view = messages_[message];
    const auto id = current_.id() + view.id;
    if (expanded_.contains(id)) expanded_.remove(id); else expanded_.insert(id);
    applyFold(view, !expanded_.contains(id));
}

void ConversationPanel::rebuildBody(const Conversation &c) {
    // Rebuilt only when something shown changed, so scrolling, selection and focus stay where they are.
    QString signature = c.key.id() + '\n';
    for (const auto &m : c.messages) signature += m.messageId + (m.role == Role::User ? 'u' : 'a') + QString::number(m.text.size()) + '\n';
    for (const auto &id : c.requestOrder) {
        const auto &r = c.requests.value(id);
        if (terminal(r.state) || r.state == RequestState::Unknown) signature += "r:" + id + requestStateName(r.state) + '\n';
    }
    signature += expanded_.isEmpty() ? "" : "e";
    if (signature == bodySignature_) return;
    bodySignature_ = signature;
    stickToEnd_ = stickToEnd_ || scroll_->verticalScrollBar()->value() >= scroll_->verticalScrollBar()->maximum() - 4;
    for (auto &view : messages_) view.box->deleteLater();
    messages_.clear();
    while (auto *item = answeredLayout_->takeAt(0)) { delete item->widget(); delete item; }
    for (const auto &m : c.messages) {
        MessageView view;
        view.id = m.messageId; view.agent = m.role == Role::Agent;
        view.box = new QWidget(body_);
        auto *layout = new QVBoxLayout(view.box);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
        auto *who = new QLabel(view.agent ? ::Conversations::tr("Agent") : ::Conversations::tr("You"), view.box);
        who->setStyleSheet(QString("font-weight:700; color:%1;").arg(view.agent ? "#6b5643" : "#8a4b12"));
        view.text = new QLabel(conversation::markdownHtml(m.text), view.box);
        view.text->setTextFormat(Qt::RichText); view.text->setWordWrap(true);
        view.text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        view.text->setOpenExternalLinks(false); // A link in agent text shows its address on hover and goes nowhere.
        connect(view.text, &QLabel::linkHovered, view.text, [label = view.text](const QString &url) { label->setToolTip(url); });
        view.text->setAccessibleName(who->text());
        layout->addWidget(who); layout->addWidget(view.text);
        view.fold = new QPushButton(view.box);
        view.fold->setFlat(true); view.fold->setStyleSheet("QPushButton{background:transparent; padding:0; text-align:left; min-height:20px;}");
        layout->addWidget(view.fold, 0, Qt::AlignLeft);
        const int index = messages_.size();
        const int lines = m.text.count('\n') + 1;
        view.foldable = lines > foldedLines || m.text.size() > foldedLines * 60;
        view.fold->setVisible(view.foldable);
        connect(view.fold, &QPushButton::clicked, this, [this, index] { toggleFold(index); });
        messageLayout_->addWidget(view.box);
        messages_.append(view);
        if (view.foldable) applyFold(messages_.last(), !expanded_.contains(c.key.id() + view.id));
    }
    for (const auto &id : c.requestOrder) {
        const auto &r = c.requests.value(id);
        if (!terminal(r.state)) continue;
        const QString label = r.state == RequestState::Answered ? ::Conversations::tr("Answered") : r.state == RequestState::Cancelled
            ? ::Conversations::tr("Cancelled") : r.state == RequestState::Replaced ? ::Conversations::tr("Replaced") : ::Conversations::tr("Expired");
        auto *line = new QLabel(QString("%1 · %2").arg(label, conversation::firstLine(r.question)), answered_);
        line->setTextFormat(Qt::PlainText); line->setWordWrap(true);
        line->setStyleSheet("color:#6b5643; background:#f3e9d2; border-radius:8px; padding:6px;");
        answeredLayout_->addWidget(line);
    }
}

void ConversationPanel::showRequest(const Conversation &c) {
    QVector<const InputRequest *> open;
    for (const auto &id : c.requestOrder) {
        const auto it = c.requests.constFind(id);
        if (it != c.requests.constEnd() && !terminal(it->state)) open.append(&*it);
    }
    if (open.isEmpty()) {
        for (auto *button : options_) { group_.removeButton(button); button->deleteLater(); }
        options_.clear();
        if (custom_) { group_.removeButton(custom_); custom_->deleteLater(); custom_ = nullptr; }
        card_->hide(); activeRequest_.clear(); requestSignature_.clear(); return;
    }
    const auto *shown = selectedRequest(c);
    if (!shown) shown = open.first(); // A request already being sent is still shown, read-only.
    activeRequest_ = shown->requestId;
    // Rebuilt only when the question changed, so a message arriving does not take keyboard focus off an option.
    QString signature = c.key.id() + '|' + shown->requestId + QString::number(shown->revision) + requestStateName(shown->state)
        + (c.link == Conversation::Link::Connected ? "c" : "d") + (shown->customAllowed ? "u" : "") + shown->question;
    for (const auto &option : shown->options) signature += '|' + option.id + option.label;
    for (const auto *r : open) signature += '#' + r->requestId + requestStateName(r->state);
    if (signature == requestSignature_ && !card_->isHidden()) return;
    requestSignature_ = signature;
    for (auto *button : options_) { group_.removeButton(button); button->deleteLater(); }
    options_.clear();
    if (custom_) { group_.removeButton(custom_); custom_->deleteLater(); custom_ = nullptr; }
    selector_->blockSignals(true);
    selector_->clear();
    for (int i = 0; i < open.size(); ++i) {
        selector_->addItem(::Conversations::tr("Question %1 of %2: %3").arg(i + 1).arg(open.size()).arg(conversation::firstLine(open[i]->question)), open[i]->requestId);
        if (open[i]->requestId == activeRequest_) selector_->setCurrentIndex(i);
    }
    selector_->blockSignals(false);
    selector_->setVisible(open.size() > 1);
    questionText_ = shown->question;
    question_->setText(conversation::markdownHtml(shown->question));
    const bool answerable = shown->state == RequestState::Pending && c.link == Conversation::Link::Connected;
    const QString picked = chosen_.value(activeRequest_);
    bool anyChecked = false;
    for (const auto &option : shown->options) {
        auto *button = new QRadioButton(option.label, optionBox_);
        button->setProperty("optionId", option.id);
        button->setEnabled(answerable);
        optionLayout_->addWidget(button); group_.addButton(button);
        options_.append(button);
        if (option.id == picked && !picked.isEmpty()) { button->setChecked(true); anyChecked = true; }
    }
    if (shown->customAllowed && !shown->options.isEmpty()) {
        custom_ = new QRadioButton(::Conversations::tr("Custom answer"), optionBox_);
        custom_->setEnabled(answerable);
        optionLayout_->addWidget(custom_); group_.addButton(custom_);
        if (!anyChecked && chosen_.contains(activeRequest_) && picked.isEmpty()) custom_->setChecked(true);
    }
    optionBox_->setVisible(!options_.isEmpty());
    card_->show();
}

void ConversationPanel::updateComposer(const Conversation &c) {
    const auto *request = selectedRequest(c);
    const QString slot = request ? request->requestId : QString();
    const QString full = c.key.id() + '\x1f' + slot;
    if (full != shownSlot_) { // A different draft: show that one, never carry text across.
        shownSlot_ = full;
        const bool was = loading_; loading_ = true;
        edit_->setPlainText(service_.draft(c.key, slot));
        loading_ = was;
    }
    const bool connected = c.link == Conversation::Link::Connected;
    const bool busy = !sending_.isEmpty();
    const QString text = edit_->toPlainText();
    if (request) {
        const bool pending = request->state == RequestState::Pending;
        const bool textMode = request->options.isEmpty() || (custom_ && custom_->isChecked());
        composerLabel_->setText(textMode && !request->options.isEmpty() ? ::Conversations::tr("Custom answer") : request->options.isEmpty() ? ::Conversations::tr("Answer this question") : QString());
        composerLabel_->setVisible(textMode);
        edit_->setVisible(textMode);
        edit_->setPlaceholderText(::Conversations::tr("Type a custom answer…"));
        edit_->setEnabled(connected && pending);
        send_->setText(::Conversations::tr("Send answer"));
        const bool ready = textMode ? !text.trimmed().isEmpty() : !chosen_.value(request->requestId).isEmpty();
        send_->setEnabled(connected && pending && !busy && ready);
    } else {
        composerLabel_->setVisible(false);
        edit_->setVisible(true);
        edit_->setPlaceholderText(::Conversations::tr("Follow up with this agent…"));
        // A draft can be written while the agent works; it is sent only on request, when the agent is ready.
        edit_->setEnabled(c.link != Conversation::Link::Ended && c.has(FollowUp));
        send_->setText(::Conversations::tr("Send"));
        send_->setEnabled(connected && c.ready && !busy && c.has(FollowUp) && !text.trimmed().isEmpty() && c.followUpUnknown.isEmpty());
    }
    send_->setAccessibleDescription(feedback_->text());
}

// ---- Sending -----------------------------------------------------------------------------------

void ConversationPanel::chooseOption(int index) {
    if (index >= 0 && index < options_.size()) options_[index]->setChecked(true);
}
void ConversationPanel::chooseCustom() {
    if (custom_) custom_->setChecked(true);
}
void ConversationPanel::send() {
    const auto *c = service_.find(current_);
    if (!c || !sending_.isEmpty() || !send_->isEnabled()) return;
    const auto *request = selectedRequest(*c);
    Command command;
    command.commandId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    command.conversation = current_;
    command.source = "desktop";
    if (request) {
        command.action = Action::AnswerRequest;
        command.requestId = request->requestId;
        command.requestRevision = request->revision;
        if (answeringWithText()) command.text = edit_->toPlainText().trimmed();
        else command.optionId = chosen_.value(request->requestId);
    } else {
        command.action = Action::SendFollowUp;
        command.text = edit_->toPlainText().trimmed();
    }
    sending_ = command.commandId;
    flights_.insert(command.commandId, {current_, request ? request->requestId : QString()});
    feedback_->setText(::Conversations::tr("Sending…"));
    feedback_->setStyleSheet("color:#6b5643;");
    send_->setEnabled(false);
    const auto result = service_.submit(command);
    if (result.outcome != Outcome::Pending && flights_.contains(command.commandId)) onResult(result);
    else if (auto *now = service_.find(current_)) updateComposer(*now);
}
void ConversationPanel::onResult(const CommandResult &result) {
    const bool settled = result.outcome != Outcome::Unknown;
    const auto flight = settled ? flights_.take(result.commandId) : flights_.value(result.commandId);
    if (!settled) flights_[result.commandId].unconfirmed = true;
    const bool visible = flight.key == current_;
    if (visible && sending_ == result.commandId) sending_.clear();
    const auto *c = service_.find(flight.key);
    if (result.outcome == Outcome::Accepted) {
        // "Sent" means the agent took it, not that the work it asked for succeeded.
        chosen_.remove(flight.slot);
        if (c) service_.setDraft(flight.key, flight.slot, {}); // What was sent is not kept as a draft.
        if (!visible) return;
        feedback_->setText(::Conversations::tr("Sent"));
        feedback_->setStyleSheet("color:#2b6a3a;");
        const bool was = loading_; loading_ = true; edit_->clear(); loading_ = was;
        if (c) { shownSlot_.clear(); refresh(); }
        return;
    }
    // The text and the choice stay: rejected or unconfirmed, nothing is lost and nothing is resent for the user.
    if (!visible) return;
    feedback_->setStyleSheet(QString("color:%1;").arg(conversation::failureColor().name()));
    if (result.outcome == Outcome::Unknown)
        feedback_->setText(::Conversations::tr("Delivery not confirmed. Check the agent before sending again."));
    else feedback_->setText(conversation::reasonText(result.reason));
    if (c) { refresh(); }
}

// ---- Reading -----------------------------------------------------------------------------------

void ConversationPanel::markVisibleRead() {
    if (!isVisible() || !current_.valid()) return;
    const auto *c = service_.find(current_);
    if (!c) return;
    const QRect viewport(QPoint(0, 0), scroll_->viewport()->size());
    for (const auto &view : messages_) {
        if (!view.agent || !view.box->isVisible()) continue;
        const QRect area(view.box->mapTo(scroll_->viewport(), QPoint(0, 0)), view.box->size());
        // Read once it is in view; folded text counts as the part that shows.
        if (viewport.intersects(area)) service_.markRead(current_, view.id);
    }
}
}
