#include "session_list.h"
#include "alert_bubble.h"
#include <QPainter>

namespace pet {
QColor SessionList::stateColor(const QString &state) {
    if (state == "attention") return QColor("#d9480f");
    if (state == "error") return QColor("#c92a2a");
    if (state == "working" || state == "reading" || state == "thinking") return QColor("#1971c2");
    if (state == "turn-finished") return QColor("#2b8a3e");
    return QColor("#adb5bd");
}
static QIcon dot(const QColor &color) {
    QPixmap pixmap(12, 12); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(color); painter.drawEllipse(1, 1, 10, 10);
    return QIcon(pixmap);
}
SessionList::SessionList(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAccessibleName("Running agent sessions");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(2);
    header_ = new QLabel(this); header_->setStyleSheet("color:#453324; font-weight:600; padding:0 4px 4px 4px;");
    empty_ = new QLabel("No sessions yet. A session appears after its next hook event.", this);
    empty_->setStyleSheet("color:#7a6450; padding:4px;"); empty_->setWordWrap(true); empty_->setFixedWidth(260);
    overflow_ = new QLabel(this); overflow_->setStyleSheet("color:#7a6450; padding:2px 4px;");
    rows_ = new QVBoxLayout; rows_->setSpacing(1);
    layout->addWidget(header_); layout->addWidget(empty_); layout->addLayout(rows_); layout->addWidget(overflow_);
}
void SessionList::present(const QVector<SessionRow> &rows) {
    QString signature;
    for (const auto &row : rows) signature += row.key + row.name + row.detail + row.status + row.state + row.tooltip + '\n';
    if (presented_ && signature == shown_) return; // Keep hover and keyboard focus stable.
    shown_ = signature; presented_ = true;
    qDeleteAll(buttons_); buttons_.clear();
    header_->setText(rows.size() == 1 ? "1 session" : QString("%1 sessions").arg(rows.size()));
    empty_->setVisible(rows.isEmpty());
    for (const auto &row : rows.mid(0, maxRows)) {
        auto *button = new QPushButton(dot(stateColor(row.state)), row.name + " — " + row.status + "\n" + row.detail, this);
        button->setFlat(true); button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(row.tooltip + "\nClick to bring its terminal or editor forward");
        button->setAccessibleName(row.name + ", " + row.status + ", " + row.detail);
        button->setStyleSheet("QPushButton{color:#453324; text-align:left; border:0; border-radius:8px; padding:4px 8px;}"
                              "QPushButton:hover, QPushButton:focus{background:#f3e3c3;}");
        const auto key = row.key;
        connect(button, &QPushButton::clicked, this, [this, key] { hide(); emit focusRequested(key); });
        rows_->addWidget(button); buttons_.append(button);
    }
    overflow_->setText(QString("and %1 more").arg(rows.size() - maxRows));
    overflow_->setVisible(rows.size() > maxRows);
    adjustSize();
}
void SessionList::place(const QRect &pet, const QVector<QRect> &screens) {
    QRect screen = screens.value(0, pet);
    for (const auto &area : screens) if (area.contains(pet.center())) { screen = area; break; }
    move(AlertBubble::placement(pet, size(), screen));
}
void SessionList::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#453324"), 1.2)); painter.setBrush(QColor("#fff7e3"));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
}
}
