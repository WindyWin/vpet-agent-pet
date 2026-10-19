#include "conversation_bubbles.h"
#include "alert_bubble.h"
#include "conversation_text.h"
#include "i18n/contexts.h"
#include "desktop/overlay.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

namespace pet {
using namespace interactions;

ConversationPreview::ConversationPreview(QWidget *parent) : QWidget(parent) {
    // Unmanaged and unfocusable like the alert toast: a preview never steals the keyboard.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                   Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    overlay::prepare(this, true, false);
    setCursor(Qt::PointingHandCursor);
    setFixedWidth(340);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 10, 12, 10); layout->setSpacing(4);
    auto *top = new QHBoxLayout; top->setSpacing(8);
    title_ = new QLabel(this); title_->setStyleSheet("color:#453324; font-weight:600;");
    name_ = new QLabel(this); name_->setStyleSheet("color:#6b5643;");
    title_->setTextFormat(Qt::PlainText); name_->setTextFormat(Qt::PlainText);
    more_ = new QPushButton(this);
    more_->setStyleSheet("QPushButton{color:#8a4b12; background:#f3e3c3; border:0; border-radius:8px; padding:1px 7px; font-weight:600;}"
                         "QPushButton:hover{background:#ecd3a5;}");
    top->addWidget(title_); top->addWidget(name_, 1); top->addWidget(more_);
    auto *bottom = new QHBoxLayout; bottom->setSpacing(8);
    body_ = new QLabel(this); body_->setTextFormat(Qt::PlainText); body_->setStyleSheet("color:#453324;");
    body_->setWordWrap(true);
    view_ = new QPushButton(this); hide_ = new QPushButton("×", this);
    for (auto *button : {view_, hide_})
        button->setStyleSheet("QPushButton{color:#8a4b12; font-weight:600; border:0; padding:0 3px;}"
                              "QPushButton:hover{text-decoration:underline;}");
    hide_->setStyleSheet(hide_->styleSheet() + "QPushButton{font-size:15px;}");
    for (auto *button : {more_, view_, hide_}) { button->setFlat(true); button->setCursor(Qt::PointingHandCursor); }
    bottom->addWidget(body_, 1); bottom->addWidget(view_, 0, Qt::AlignTop); bottom->addWidget(hide_, 0, Qt::AlignTop);
    layout->addLayout(top); layout->addLayout(bottom);
    retranslate();
    connect(more_, &QPushButton::clicked, this, &ConversationPreview::listRequested);
    connect(view_, &QPushButton::clicked, this, &ConversationPreview::viewRequested);
    connect(hide_, &QPushButton::clicked, this, &ConversationPreview::hideRequested);
}
void ConversationPreview::retranslate() {
    setAccessibleName(::Conversations::tr("Agent message"));
    view_->setText(::Conversations::tr("View"));
    view_->setAccessibleName(::Conversations::tr("View the conversation")); view_->setToolTip(::Conversations::tr("Open the conversation"));
    hide_->setAccessibleName(::Conversations::tr("Hide this preview")); hide_->setToolTip(::Conversations::tr("Hide this preview (it stays unread)"));
    more_->setAccessibleName(::Conversations::tr("Show conversations")); more_->setToolTip(::Conversations::tr("Show conversations"));
}
void ConversationPreview::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) { retranslate(); adjustSize(); }
    QWidget::changeEvent(event);
}
void ConversationPreview::present(const Content &content) {
    title_->setText(content.title);
    name_->setText(fontMetrics().elidedText(content.name, Qt::ElideMiddle, 120));
    fullBody_ = content.body;
    // The real first line, shortened to two lines of width, never rewritten.
    const int width = 340 - 26 - 12 - 90;
    body_->setText(fontMetrics().elidedText(content.body, Qt::ElideRight, width * 2));
    body_->setVisible(!content.body.isEmpty());
    setToolTip(content.tooltip);
    more_->setText(QString("+%1").arg(content.more)); more_->setVisible(content.more > 0);
    accent_ = content.attention ? conversation::pendingColor() : conversation::unreadColor();
    setAccessibleDescription(content.title + ": " + content.name + (content.body.isEmpty() ? QString() : ". " + content.body));
    adjustSize();
    update();
}
void ConversationPreview::place(const QRect &pet, const QVector<QRect> &screens, const QVector<QRect> &avoid) {
    QRect screen = screens.value(0, pet);
    for (const auto &area : screens) if (area.contains(pet.center())) { screen = area; break; }
    overlay::place(this, AlertBubble::placement(pet, size(), screen, avoid));
}
void ConversationPreview::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) emit viewRequested();
}
void ConversationPreview::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF body = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor("#453324"), 1.2)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(body, 12, 12);
    painter.setPen(Qt::NoPen); painter.setBrush(accent_);
    painter.drawEllipse(QPointF(14, 20), 5, 5);
}

static QIcon dot(const QColor &color) {
    QPixmap pixmap(12, 12); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(color); painter.drawEllipse(1, 1, 10, 10);
    return QIcon(pixmap);
}
ConversationList::ConversationList(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(2);
    header_ = new QLabel(this); header_->setStyleSheet("color:#453324; font-weight:600; padding:0 4px 4px 4px;");
    empty_ = new QLabel(this);
    empty_->setStyleSheet("color:#7a6450; padding:4px;"); empty_->setWordWrap(true); empty_->setFixedWidth(260);
    rows_ = new QVBoxLayout; rows_->setSpacing(1);
    layout->addWidget(header_); layout->addWidget(empty_); layout->addLayout(rows_);
    retranslate();
}
void ConversationList::retranslate() {
    setAccessibleName(::Conversations::tr("Agent conversations"));
    empty_->setText(::Conversations::tr("No conversations yet. An agent with interactions enabled appears here."));
}
void ConversationList::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) { retranslate(); presented_ = false; }
    QWidget::changeEvent(event);
}
void ConversationList::present(const Service &service) {
    struct Row { ConversationKey key; QString text, tip; QColor color; };
    QVector<Row> list;
    QString signature;
    for (const auto &key : service.keys()) {
        const auto *c = service.find(key);
        if (!c) continue;
        QString what;
        QColor color = conversation::neutralColor();
        if (c->link == Conversation::Link::Ended) what = ::Conversations::tr("Session ended");
        else if (c->link == Conversation::Link::Disconnected) what = ::Conversations::tr("Disconnected");
        else if (c->needsAttention()) { what = ::Conversations::tr("Needs your answer"); color = conversation::pendingColor(); }
        else if (c->unreadCount() > 0) { what = c->unreadCount() == 1 ? ::Conversations::tr("1 new message") : ::Conversations::tr("%1 new messages").arg(c->unreadCount()); color = conversation::unreadColor(); }
        else what = c->ready ? ::Conversations::tr("Ready") : ::Conversations::tr("Working…");
        QString detail;
        if (const auto *request = conversation::firstOpenRequest(*c)) detail = conversation::firstLine(request->question);
        else for (int i = c->messages.size() - 1; i >= 0 && detail.isEmpty(); --i) detail = conversation::firstLine(c->messages[i].text);
        const QString name = c->title.isEmpty() ? conversation::shortId(key) : c->title;
        list.append({key, name + " — " + what + (detail.isEmpty() ? QString() : "\n" + detail),
                     conversation::identityTooltip(*c), color});
        signature += key.id() + list.last().text + '\n';
    }
    if (presented_ && signature == shown_) return;
    shown_ = signature; presented_ = true;
    qDeleteAll(buttons_); buttons_.clear();
    header_->setText(list.size() == 1 ? ::Conversations::tr("1 conversation") : ::Conversations::tr("%1 conversations").arg(list.size()));
    empty_->setVisible(list.isEmpty());
    for (const auto &row : list.mid(0, maxRows)) {
        auto *button = new QPushButton(dot(row.color), row.text, this);
        button->setFlat(true); button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(row.tip);
        button->setAccessibleName(row.text.simplified());
        button->setStyleSheet("QPushButton{color:#453324; text-align:left; border:0; border-radius:8px; padding:4px 8px;}"
                              "QPushButton:hover, QPushButton:focus{background:#f3e3c3;}");
        const auto key = row.key;
        connect(button, &QPushButton::clicked, this, [this, key] { hide(); emit conversationRequested(key); });
        rows_->addWidget(button); buttons_.append(button);
    }
    adjustSize();
}
void ConversationList::place(const QRect &pet, const QVector<QRect> &screens, const QVector<QRect> &avoid) {
    QRect screen = screens.value(0, pet);
    for (const auto &area : screens) if (area.contains(pet.center())) { screen = area; break; }
    move(AlertBubble::placement(pet, size(), screen, avoid));
}
void ConversationList::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#453324"), 1.2)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
}
}
