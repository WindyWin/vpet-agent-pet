#include "quota_panel.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace pet {
QuotaPanel::QuotaPanel(QWidget *parent) : QWidget(parent) {
    setObjectName("quotaPanel");
    setMinimumWidth(280);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 8, 4, 2); layout->setSpacing(4);
    auto *heading = new QHBoxLayout;
    title_ = new QLabel(this); title_->setStyleSheet("color:#453324; font-weight:600;");
    refresh_ = new QPushButton(this); refresh_->setFlat(true); refresh_->setCursor(Qt::PointingHandCursor);
    refresh_->setObjectName("quotaRefresh");
    refresh_->setStyleSheet("QPushButton{color:#7a6450; border:0; padding:2px 4px;} QPushButton:hover{color:#453324; background:#f3e3c3; border-radius:4px;}");
    heading->addWidget(title_); heading->addStretch(); heading->addWidget(refresh_);
    hint_ = new QLabel(this); hint_->setStyleSheet("color:#7a6450; font-size:11px;");
    hint_->setWordWrap(true);
    readings_ = new QVBoxLayout; readings_->setSpacing(6);
    layout->addLayout(heading); layout->addWidget(hint_); layout->addLayout(readings_);
    timer_.setInterval(60000);
    connect(&timer_, &QTimer::timeout, this, [this] { if (service_) service_->refresh(); render(); });
    connect(refresh_, &QPushButton::clicked, this, [this] { if (service_) service_->refresh(true); });
    hide();
}
void QuotaPanel::setService(quota::Service *service) {
    if (service_) disconnect(service_, nullptr, this, nullptr);
    service_ = service;
    if (service_) {
        connect(service_, &quota::Service::changed, this, [this] { if (isVisible()) render(); });
        connect(service_, &QObject::destroyed, this, [this] { timer_.stop(); hide(); emit sizeChanged(); });
    }
    setVisible(service != nullptr);
    render();
}
void QuotaPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (service_) { timer_.start(); service_->refresh(); render(); }
}
void QuotaPanel::hideEvent(QHideEvent *event) { timer_.stop(); QWidget::hideEvent(event); }
void QuotaPanel::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) render();
    QWidget::changeEvent(event);
}
namespace {
QString kindName(quota::Window::Kind kind) {
    return kind == quota::Window::Session ? QuotaPanel::tr("5 hours") : kind == quota::Window::Day ? QuotaPanel::tr("Daily")
         : kind == quota::Window::Week ? QuotaPanel::tr("Weekly") : QuotaPanel::tr("Monthly");
}
QString percent(double used) { return QLocale().toString(used, 'f', used > 0 && used < 1 ? 1 : 0); }
QString timeLeft(qint64 minutes) {
    return minutes >= 1440 ? QuotaPanel::tr("%1d %2h").arg(minutes / 1440).arg(minutes / 60 % 24)
         : minutes >= 60 ? QuotaPanel::tr("%1h %2m").arg(minutes / 60).arg(minutes % 60) : QuotaPanel::tr("%1m").arg(minutes);
}
}
RingGauge::RingGauge(QWidget *parent) : QWidget(parent) { setFixedSize(54, 54); }
void RingGauge::set(double used, const QString &text, const QString &caption) {
    used_ = used; text_ = text; caption_ = caption;
    update();
}
void RingGauge::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    constexpr qreal width = 5;
    const QRectF ring = QRectF(rect()).adjusted(width / 2 + 1, width / 2 + 1, -width / 2 - 1, -width / 2 - 1);
    painter.setBrush(Qt::NoBrush);
    QPen track(QColor(used_ < 0 ? "#cfc2a8" : "#ebddc5"), width);
    if (used_ < 0) track.setStyle(Qt::DotLine);
    painter.setPen(track); painter.drawEllipse(ring);
    if (used_ >= 0) {
        QPen arc(QColor(used_ >= 80 ? "#d9480f" : "#2b8a3e"), width, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(arc);
        painter.drawArc(ring, 90 * 16, -qRound(qBound(0.0, used_, 100.0) * 3.6 * 16));
    }
    painter.setPen(QColor("#453324"));
    QFont big = font(); big.setPixelSize(13); big.setBold(true);
    QFont small = font(); small.setPixelSize(9);
    const QRectF inner = ring.adjusted(width, width, -width, -width);
    painter.setFont(big);
    painter.drawText(QRectF(inner.left() - 4, inner.top() + 2, inner.width() + 8, inner.height() / 2 + 3), Qt::AlignCenter, text_);
    painter.setFont(small); painter.setPen(QColor("#7a6450"));
    painter.drawText(QRectF(inner.left() - 4, inner.center().y() + 4, inner.width() + 8, inner.height() / 2 - 2), Qt::AlignHCenter | Qt::AlignTop, caption_);
}
void QuotaPanel::render() {
    title_->setText(tr("Plan limits"));
    hint_->setText(tr("Account-wide · % used"));
    hint_->setToolTip(tr("Uses the CLI account configured on this computer. Limits are shared by all of that account's sessions."));
    refresh_->setText(tr("Refresh"));
    while (auto *item = readings_->takeAt(0)) { delete item->widget(); delete item; }
    if (!service_) return;
    bool loading = false;
    const auto now = service_->now();
    auto label = [](const QString &text, QWidget *parent, const char *style) {
        auto *result = new QLabel(text, parent);
        result->setTextFormat(Qt::PlainText); result->setWordWrap(true);
        result->setStyleSheet(style);
        result->show(); // Rebuilt children must be shown: hints forget hidden ones.
        return result;
    };
    const auto minutesTo = [&](const QDateTime &when) { return qint64(std::ceil(now.msecsTo(when) / 60000.0)); };
    for (auto provider : {quota::Provider::Codex, quota::Provider::Claude}) {
        const auto reading = service_->reading(provider);
        loading |= reading.loading;
        const auto name = provider == quota::Provider::Codex ? QStringLiteral("Codex") : QStringLiteral("Claude Code");
        QString problem;
        switch (reading.problem) {
        case quota::Problem::Missing: problem = tr("Sign in with the CLI to see plan limits."); break;
        case quota::Problem::Expired: problem = tr("Sign-in expired. Open the CLI to renew it."); break;
        case quota::Problem::Locked: problem = tr("Could not read the CLI sign-in from Keychain."); break;
        case quota::Problem::RateLimited: problem = tr("Provider asked to slow down. Try again later."); break;
        case quota::Problem::Failed: problem = tr("Could not read plan limits. Try again later."); break;
        case quota::Problem::None: break;
        }
        if (reading.loading && reading.windows.isEmpty()) problem = tr("Checking limits…");
        // The ring is the shortest window; beside it, the longest unscoped one (scoped limits stay in the tooltip).
        const quota::Window *primary = nullptr, *secondary = nullptr;
        for (const auto &window : reading.windows) if (!primary || window.kind < primary->kind) primary = &window;
        for (const auto &window : reading.windows)
            if (&window != primary && window.scope.isEmpty() && (!secondary || window.kind > secondary->kind)) secondary = &window;
        // Long account names and every window belong in a tooltip, keeping this popup narrow.
        QStringList details;
        if (!reading.account.isEmpty()) details << reading.account;
        if (reading.checked.isValid()) details << tr("Checked %1").arg(QLocale().toString(reading.checked.toLocalTime(), QLocale::ShortFormat));
        if (reading.problem != quota::Problem::None && !reading.windows.isEmpty()) details << tr("Last known usage");
        for (const auto &window : reading.windows) {
            const auto windowName = window.scope.isEmpty() ? kindName(window.kind) : tr("%1 · %2").arg(kindName(window.kind), window.scope);
            auto line = tr("%1: %2% used").arg(windowName, percent(window.used));
            if (window.resets.isValid()) line += " · " + (minutesTo(window.resets) > 0 ? tr("Resets in %1").arg(timeLeft(minutesTo(window.resets))) : tr("Reset due · refresh to check"));
            details << line;
        }
        if (!problem.isEmpty()) details << problem;
        auto *row = new QWidget(this);
        row->setToolTip(details.join('\n'));
        auto *across = new QHBoxLayout(row); across->setContentsMargins(0, 0, 0, 0); across->setSpacing(10);
        auto *ring = new RingGauge(row);
        if (primary) ring->set(primary->used, percent(primary->used) + "%", kindName(primary->kind));
        else ring->set(-1, reading.loading ? QStringLiteral("…") : QStringLiteral("–"), {});
        ring->setAccessibleName(primary ? tr("%1 %2: %3% used").arg(name, kindName(primary->kind), percent(primary->used)) : tr("%1: %2").arg(name, problem));
        ring->show();
        across->addWidget(ring);
        auto *text = new QVBoxLayout; text->setSpacing(1);
        auto *top = new QHBoxLayout; top->setSpacing(8);
        top->addWidget(label(name, row, "color:#453324; font-weight:600;"));
        top->addStretch();
        if (secondary) top->addWidget(label(tr("%1 %2%").arg(kindName(secondary->kind), percent(secondary->used)), row,
                                            secondary->used >= 80 ? "color:#d9480f; font-weight:600;" : "color:#453324; font-weight:600;"));
        text->addLayout(top);
        QStringList resets;
        if (primary && primary->resets.isValid())
            resets << (minutesTo(primary->resets) > 0 ? tr("Resets in %1").arg(timeLeft(minutesTo(primary->resets))) : tr("Reset due · refresh to check"));
        if (secondary && secondary->resets.isValid() && minutesTo(secondary->resets) > 0)
            resets << tr("%1 in %2").arg(kindName(secondary->kind), timeLeft(minutesTo(secondary->resets)));
        // A problem outranks the reset times: last known numbers are shown, but not as current.
        const auto summary = !problem.isEmpty() ? problem : resets.join(" · ");
        text->addWidget(label(summary, row, "color:#7a6450; font-size:11px;"));
        across->addLayout(text, 1);
        readings_->addWidget(row);
        row->show();
    }
    refresh_->setEnabled(!loading);
    emit sizeChanged();
}
}
