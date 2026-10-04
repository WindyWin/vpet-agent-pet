#include "alert_bubble.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace pet {
AlertBubble::AlertBubble(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAccessibleName("Agent Pet alert");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10); layout->setSpacing(4);
    title_ = new QLabel(this); title_->setStyleSheet("color:#453324; font-weight:600;");
    label_ = new QLabel(this); label_->setStyleSheet("color:#453324;");
    label_->setTextFormat(Qt::PlainText); title_->setTextFormat(Qt::PlainText);
    layout->addWidget(title_); layout->addWidget(label_);
    auto *footer = new QHBoxLayout; footer->setSpacing(8);
    more_ = new QLabel(this); more_->setStyleSheet("color:#7a6450;");
    next_ = new QPushButton("&Next", this); dismiss_ = new QPushButton("&Dismiss", this);
    for (auto *button : {next_, dismiss_}) {
        button->setFlat(true); button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet("QPushButton{color:#8a4b12; font-weight:600; border:0; padding:0 2px;}"
                              "QPushButton:hover{text-decoration:underline;}");
    }
    next_->setAccessibleName("Next alert"); dismiss_->setAccessibleName("Dismiss alert");
    footer->addWidget(more_); footer->addStretch(); footer->addWidget(next_); footer->addWidget(dismiss_);
    layout->addLayout(footer);
    connect(next_, &QPushButton::clicked, this, &AlertBubble::nextRequested);
    connect(dismiss_, &QPushButton::clicked, this, &AlertBubble::dismissRequested);
}
void AlertBubble::present(const AlertText &text, int more) {
    title_->setText(text.title);
    label_->setText(text.label); label_->setToolTip(text.tooltip);
    more_->setText(more == 0 ? QString() : more == 1 ? "1 more alert" : QString("%1 more alerts").arg(more));
    next_->setVisible(more > 0);
    setAccessibleDescription(text.title + ": " + text.label);
    adjustSize();
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
void AlertBubble::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#453324"), 1.5)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
}
}
