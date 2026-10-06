#include "alert_bubble.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

namespace pet {
AlertBubble::AlertBubble(QWidget *parent) : QWidget(parent) {
    // Unmanaged like a tooltip, so window managers neither re-place it nor list it.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                   Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setCursor(Qt::PointingHandCursor);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(26, 6, 8, 6); layout->setSpacing(8);
    title_ = new QLabel(this); title_->setStyleSheet("color:#453324; font-weight:600;");
    name_ = new QLabel(this); name_->setStyleSheet("color:#6b5643;");
    title_->setTextFormat(Qt::PlainText); name_->setTextFormat(Qt::PlainText);
    layout->addWidget(title_); layout->addWidget(name_);
    more_ = new QPushButton(this); open_ = new QPushButton(this); dismiss_ = new QPushButton("×", this);
    more_->setStyleSheet("QPushButton{color:#8a4b12; background:#f3e3c3; border:0; border-radius:8px; padding:1px 7px; font-weight:600;}"
                         "QPushButton:hover{background:#ecd3a5;}");
    for (auto *button : {open_, dismiss_})
        button->setStyleSheet("QPushButton{color:#8a4b12; font-weight:600; border:0; padding:0 3px;}"
                              "QPushButton:hover{text-decoration:underline;}");
    dismiss_->setStyleSheet(dismiss_->styleSheet() + "QPushButton{font-size:15px;}");
    for (auto *button : {more_, open_, dismiss_}) { button->setFlat(true); button->setCursor(Qt::PointingHandCursor); }
    retranslate();
    layout->addWidget(more_); layout->addWidget(open_); layout->addWidget(dismiss_);
    connect(more_, &QPushButton::clicked, this, &AlertBubble::listRequested);
    connect(open_, &QPushButton::clicked, this, &AlertBubble::focusRequested);
    connect(dismiss_, &QPushButton::clicked, this, &AlertBubble::dismissRequested);
}
void AlertBubble::retranslate() {
    setAccessibleName(tr("Agent Pet alert"));
    open_->setText(tr("Open"));
    more_->setAccessibleName(tr("Show running sessions")); more_->setToolTip(tr("Show running sessions"));
    open_->setAccessibleName(tr("Open the agent's terminal or editor")); open_->setToolTip(tr("Bring the agent's terminal or editor forward"));
    dismiss_->setAccessibleName(tr("Dismiss alert")); dismiss_->setToolTip(tr("Dismiss"));
}
void AlertBubble::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) { retranslate(); adjustSize(); }
    QWidget::changeEvent(event);
}
void AlertBubble::present(const AlertText &text, const QString &kind, int more) {
    title_->setText(text.title);
    name_->setText(fontMetrics().elidedText(text.name, Qt::ElideMiddle, 180));
    label_ = text.label;
    setToolTip(text.label + "\n" + text.tooltip);
    more_->setText(QString("+%1").arg(more)); more_->setVisible(more > 0);
    accent_ = kind == "attention" ? QColor("#d9480f") : kind == "error" ? QColor("#c92a2a") : QColor("#2b8a3e");
    setAccessibleDescription(text.title + ": " + text.label);
    adjustSize();
    update();
}
QPoint AlertBubble::placement(const QRect &pet, QSize bubble, const QRect &screen) {
    constexpr int gap = 8;
    const int top = pet.top() + pet.height() / 5, middle = pet.center().x() - bubble.width() / 2;
    const QPoint candidates[] = {{pet.right() + 1 + gap, top}, {pet.left() - gap - bubble.width(), top},
                                 {middle, pet.top() - gap - bubble.height()}, {middle, pet.bottom() + 1 + gap}};
    QPoint position = candidates[0];
    for (const auto &candidate : candidates)
        if (screen.contains(QRect(candidate, bubble))) { position = candidate; break; }
    return {qBound(screen.left(), position.x(), qMax(screen.left(), screen.right() - bubble.width() + 1)),
            qBound(screen.top(), position.y(), qMax(screen.top(), screen.bottom() - bubble.height() + 1))};
}
void AlertBubble::place(const QRect &pet, const QVector<QRect> &screens) {
    QRect screen = screens.value(0, pet);
    for (const auto &area : screens) if (area.contains(pet.center())) { screen = area; break; }
    move(placement(pet, size(), screen));
}
void AlertBubble::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) emit focusRequested();
}
void AlertBubble::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF body = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor("#453324"), 1.2)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(body, body.height() / 2, body.height() / 2);
    painter.setPen(Qt::NoPen); painter.setBrush(accent_);
    painter.drawEllipse(QPointF(14, body.center().y()), 5, 5);
}
NoteBubble::NoteBubble(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                   Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAccessibleName(tr("Agent Pet note"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 8);
    label_ = new QLabel(this);
    label_->setTextFormat(Qt::PlainText); label_->setWordWrap(true);
    label_->setStyleSheet("color:#453324; font-weight:600;");
    label_->setMaximumWidth(260);
    layout->addWidget(label_);
    hide_.setSingleShot(true);
    connect(&hide_, &QTimer::timeout, this, &QWidget::hide);
}
void NoteBubble::say(const QString &text, const QRect &pet, const QVector<QRect> &screens, const QString &details, int ms) {
    details_ = details; pet_ = pet; screens_ = screens;
    setCursor(details.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    setToolTip(details.isEmpty() ? QString() : tr("Click for more"));
    present(text, ms);
}
void NoteBubble::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) setAccessibleName(tr("Agent Pet note"));
    QWidget::changeEvent(event);
}
void NoteBubble::present(const QString &text, int ms) {
    label_->setText(text);
    setAccessibleDescription(text);
    adjustSize();
    QRect screen = screens_.value(0, pet_);
    for (const auto &area : screens_) if (area.contains(pet_.center())) { screen = area; break; }
    move(AlertBubble::placement(pet_, size(), screen));
    show(); raise(); hide_.start(ms);
}
void NoteBubble::mouseReleaseEvent(QMouseEvent *) {
    if (details_.isEmpty()) { hide(); emit clicked(); return; }
    const auto details = details_; details_.clear();
    setCursor(Qt::ArrowCursor); setToolTip({});
    present(details, 15000);
}
void NoteBubble::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF body = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor("#453324"), 1.2)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(body, 14, 14);
}
}
