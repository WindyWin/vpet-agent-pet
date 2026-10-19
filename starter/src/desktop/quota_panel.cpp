#include "quota_panel.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
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
void QuotaPanel::render() {
    title_->setText(tr("Plan limits"));
    hint_->setText(tr("Account-wide · % used"));
    hint_->setToolTip(tr("Uses the CLI account configured on this computer. Limits are shared by all of that account's sessions."));
    refresh_->setText(tr("Refresh"));
    while (auto *item = readings_->takeAt(0)) { delete item->widget(); delete item; }
    if (!service_) return;
    bool loading = false;
    const auto now = service_->now();
    auto label = [](const QString &text, QWidget *parent) {
        auto *result = new QLabel(text, parent);
        result->setTextFormat(Qt::PlainText); result->setWordWrap(true);
        result->setStyleSheet("color:#7a6450; font-size:11px;");
        result->show(); // Rebuilt children must be shown: hints forget hidden ones.
        return result;
    };
    for (auto provider : {quota::Provider::Codex, quota::Provider::Claude}) {
        const auto reading = service_->reading(provider);
        loading |= reading.loading;
        auto *group = new QWidget(this);
        auto *layout = new QVBoxLayout(group); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
        const auto name = provider == quota::Provider::Codex ? QStringLiteral("Codex") : QStringLiteral("Claude Code");
        auto *heading = label(name, group);
        heading->setStyleSheet("color:#453324; font-weight:600; font-size:11px;");
        // Long account names belong in a tooltip, keeping this popup narrow.
        heading->setToolTip(reading.account);
        if (reading.checked.isValid()) heading->setToolTip(reading.account + "\n" + tr("Checked %1").arg(QLocale().toString(reading.checked.toLocalTime(), QLocale::ShortFormat)));
        layout->addWidget(heading);
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
        if (!problem.isEmpty()) layout->addWidget(label(problem, group));
        if (reading.problem != quota::Problem::None && !reading.windows.isEmpty()) layout->addWidget(label(tr("Last known usage"), group));
        for (const auto &window : reading.windows) {
            const auto kind = window.kind == quota::Window::Session ? tr("5 hours") : window.kind == quota::Window::Day ? tr("Daily")
                : window.kind == quota::Window::Week ? tr("Weekly") : tr("Monthly");
            const auto windowName = window.scope.isEmpty() ? kind : tr("%1 · %2").arg(kind, window.scope);
            const auto used = QLocale().toString(window.used, 'f', window.used > 0 && window.used < 1 ? 1 : 0);
            auto *line = new QHBoxLayout;
            line->addWidget(label(windowName, group)); line->addStretch(); line->addWidget(label(tr("%1% used").arg(used), group));
            layout->addLayout(line);
            auto *bar = new QProgressBar(group); bar->setRange(0, 1000); bar->setValue(qRound(window.used * 10));
            bar->setTextVisible(false); bar->setFixedHeight(5);
            bar->show();
            bar->setAccessibleName(tr("%1: %2% used").arg(windowName, used));
            const auto color = window.used >= 80 ? "#d9480f" : "#2b8a3e";
            bar->setStyleSheet(QString("QProgressBar{background:#ebddc5; border:0; border-radius:2px;} QProgressBar::chunk{background:%1; border-radius:2px;}").arg(color));
            layout->addWidget(bar);
            if (window.resets.isValid()) {
                const auto minutes = qint64(std::ceil(now.msecsTo(window.resets) / 60000.0));
                const auto time = minutes >= 1440 ? tr("%1d %2h").arg(minutes / 1440).arg(minutes / 60 % 24)
                    : minutes >= 60 ? tr("%1h %2m").arg(minutes / 60).arg(minutes % 60) : tr("%1m").arg(minutes);
                auto *reset = label(minutes > 0 ? tr("Resets in %1").arg(time) : tr("Reset due · refresh to check"), group);
                reset->setToolTip(QLocale().toString(window.resets.toLocalTime(), QLocale::LongFormat));
                layout->addWidget(reset);
            }
        }
        readings_->addWidget(group);
        group->show();
    }
    refresh_->setEnabled(!loading);
    emit sizeChanged();
}
}
