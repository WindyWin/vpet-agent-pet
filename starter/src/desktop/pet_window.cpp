#include "pet_window.h"
#include "drag_monitor.h"
#include "providers/integrations.h"
#include "version.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QMessageBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSpinBox>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWindow>

namespace pet {
PetWindow::PetWindow(QWidget *parent, const QString &path, bool persist)
    : QWidget(parent), player_(this), store_(path), menu_(this), tray_(this), persist_(persist) {
    const auto preferences = persist_ ? store_.load() : Preferences{};
    setWindowTitle("Agent Pet");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, preferences.onTop);
    setAttribute(Qt::WA_TranslucentBackground);
    setAccessibleName("Agent Pet");
    setToolTip("Drag to move · Right-click for controls · Esc to quit");
    setPetSize(preferences.size);
    muted_ = preferences.muted; sound_ = preferences.sound;
    connect(&player_, &Player::changed, this, qOverload<>(&PetWindow::update));
    connect(&player_, &Player::completed, this, [this](const QString &state) {
        if (quitting_ && state == "closing") qApp->quit();
    });
    auto *states = menu_.addMenu("Preview state");
    for (const auto &state : player_.states())
        states->addAction(state, this, [this, state] { if (!quitting_) player_.select(state); });
    muteAction_ = menu_.addAction("Mute alerts");
    muteAction_->setCheckable(true); muteAction_->setChecked(muted_);
    connect(muteAction_, &QAction::toggled, this, &PetWindow::setMuted);
    menu_.addAction("Settings…", this, &PetWindow::showSettings);
    menu_.addAction("Animation preview…", this, &PetWindow::showPreview);
    onTopAction_ = menu_.addAction("Always on top");
    onTopAction_->setCheckable(true); onTopAction_->setChecked(preferences.onTop);
    connect(onTopAction_, &QAction::toggled, this, &PetWindow::setOnTop);
    clickAction_ = menu_.addAction("Click-through for 15 seconds");
    clickAction_->setCheckable(true);
    connect(clickAction_, &QAction::toggled, this, &PetWindow::setClickThrough);
    menu_.addAction("Recover pet position and input", this, &PetWindow::recover);
    menu_.addSeparator();
    menu_.addAction("About and artwork terms…", this, &PetWindow::showAbout);
    menu_.addAction("Quit", this, &PetWindow::requestQuit);
    tray_.setIcon(QIcon(player_.pixmap()));
    tray_.setToolTip("Agent Pet — right-click for controls");
    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) recover();
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_.show();
    recoveryTimer_.setSingleShot(true);
    connect(&recoveryTimer_, &QTimer::timeout, this, [this] { setClickThrough(false); });
    dragTimer_.setInterval(40);
    connect(&dragTimer_, &QTimer::timeout, this, [this] {
        const auto native = nativeLeftButtonDown();
        if (!(native.has_value() ? *native : bool(QApplication::mouseButtons() & Qt::LeftButton))) endDrag();
    });
    saveTimer_.setSingleShot(true); saveTimer_.setInterval(250);
    connect(&saveTimer_, &QTimer::timeout, this, &PetWindow::savePreferences);
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { requestQuit(); });
    new QShortcut(QKeySequence(Qt::Key_Space), this, [this] {
        if (!quitting_) player_.select(player_.state() == "idle" ? "thinking" : "idle");
    });
    new QShortcut(QKeySequence(Qt::Key_Menu), this, [this] { menu_.popup(mapToGlobal(rect().center())); });
    new QShortcut(QKeySequence("Ctrl+,"), this, [this] { showSettings(); });
    for (auto *screen : QApplication::screens()) watchScreen(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watchScreen(screen); constrainPosition(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { QTimer::singleShot(0, this, &PetWindow::constrainPosition); });
    move(preferences.hasPosition ? preferences.position : QPoint(-1000000, -1000000));
    constrainPosition();
    ready_ = true;
    player_.select("starting", true);
}
PetWindow::~PetWindow() { savePreferences(); }
bool PetWindow::event(QEvent *event) {
    if (event->type() == QEvent::ScreenChangeInternal && ready_)
        QTimer::singleShot(0, this, [this] { player_.setRenderSize(qRound(width() * devicePixelRatioF())); });
    return QWidget::event(event);
}
QVector<QRect> PetWindow::screenAreas() const {
    QVector<QRect> areas;
    if (auto *primary = QApplication::primaryScreen()) areas.append(primary->availableGeometry());
    for (auto *screen : QApplication::screens())
        if (screen != QApplication::primaryScreen()) areas.append(screen->availableGeometry());
    return areas;
}
void PetWindow::watchScreen(QScreen *screen) {
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { constrainPosition(); });
}
void PetWindow::constrainPosition() {
    const auto adjusted = Preferences::visiblePosition(pos(), size(), screenAreas());
    if (pos() != adjusted) move(adjusted);
    if (ready_) saveTimer_.start();
}
void PetWindow::setPetSize(int pixels) {
    const int size = qBound(160, pixels, 320);
    setFixedSize(size, size);
    player_.setRenderSize(qRound(size * devicePixelRatioF()));
    if (ready_) constrainPosition();
    emit moved();
}
void PetWindow::setClickThrough(bool enabled) {
    if (quitting_) return;
    endDrag();
    clickThrough_ = enabled;
    const QSignalBlocker blocker(clickAction_);
    clickAction_->setChecked(enabled);
    const auto position = pos();
    setWindowFlag(Qt::WindowTransparentForInput, enabled); show(); move(position);
    if (enabled) recoveryTimer_.start(Preferences::recoveryMs);
    else recoveryTimer_.stop();
}
void PetWindow::setOnTop(bool enabled) {
    const QSignalBlocker blocker(onTopAction_);
    onTopAction_->setChecked(enabled);
    const auto position = pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled); show(); move(position);
    if (ready_) saveTimer_.start();
}
void PetWindow::setAttention(int sessions) {
    sessions = qMax(0, sessions);
    if (sessions == attention_) return;
    attention_ = sessions;
    setAccessibleDescription(sessions ? QString("%1 session(s) waiting for you").arg(sessions) : QString());
    update();
}
void PetWindow::setMuted(bool muted) {
    const QSignalBlocker blocker(muteAction_);
    muteAction_->setChecked(muted);
    if (muted == muted_) return;
    muted_ = muted; emit notificationsChanged();
    if (ready_) saveTimer_.start();
}
void PetWindow::setSound(bool enabled) {
    if (enabled == sound_) return;
    sound_ = enabled; emit notificationsChanged();
    if (ready_) saveTimer_.start();
}
void PetWindow::recover() {
    setClickThrough(false);
    move(Preferences::visiblePosition({-1000000, -1000000}, size(), screenAreas()));
    show(); raise();
}
bool PetWindow::savePreferences() {
    if (!persist_ || !ready_) return true;
    Preferences preferences;
    preferences.size = width(); preferences.position = pos(); preferences.hasPosition = true;
    preferences.onTop = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    preferences.muted = muted_; preferences.sound = sound_;
    return store_.save(preferences);
}
void PetWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (ready_) saveTimer_.start();
    emit moved();
}
void PetWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (player_.pixmap().isNull()) {
        painter.setBrush(QColor("#fff2cf")); painter.setPen(QColor("#453324"));
        painter.drawRoundedRect(rect().adjusted(4, 4, -4, -4), 20, 20);
        painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                         "Artwork unavailable\nRight-click for controls");
        return;
    }
    const auto size = player_.pixmap().size().scaled(this->size(), Qt::KeepAspectRatio);
    painter.drawPixmap(QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size), player_.pixmap());
    if (attention_ > 0) {
        // Persistent attention badge: stays until the observed request resolves.
        const int diameter = qMax(26, width() / 8);
        const QRect badge(width() * 3 / 4 - diameter / 2, height() / 8, diameter, diameter);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 2)); painter.setBrush(QColor("#d9480f"));
        painter.drawEllipse(badge);
        auto font = painter.font(); font.setBold(true); font.setPixelSize(diameter * 3 / 5); painter.setFont(font);
        painter.drawText(badge, Qt::AlignCenter, attention_ > 1 ? QString::number(qMin(attention_, 9)) : "!");
    }
}
void PetWindow::requestQuit() {
    if (quitting_) { qApp->quit(); return; }
    endDrag(); setClickThrough(false); savePreferences(); quitting_ = true;
    emit quitRequested(); // Monitoring stops here; closing settings never reaches this.
    if (settingsDialog_) settingsDialog_->close();
    if (previewDialog_) previewDialog_->close();
    menu_.setEnabled(false);
    player_.setPaused(false);
    if (!player_.select("closing", true) || player_.stopped()) { qApp->quit(); return; }
    // A broken shutdown asset must never prevent exit.
    QTimer::singleShot(5000, this, [] { qApp->quit(); });
}
void PetWindow::closeEvent(QCloseEvent *event) { event->ignore(); requestQuit(); }
void PetWindow::contextMenuEvent(QContextMenuEvent *event) { menu_.popup(event->globalPos()); }
void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || quitting_) return;
    dragging_ = true;
    dragOffset_ = event->globalPosition().toPoint() - pos();
    player_.beginDrag();
    fallbackDrag_ = !windowHandle()->startSystemMove();
    dragTimer_.start();
}
void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_ && fallbackDrag_ && (event->buttons() & Qt::LeftButton))
        move(event->globalPosition().toPoint() - dragOffset_);
}
void PetWindow::endDrag() {
    if (!dragging_) return;
    dragging_ = false; fallbackDrag_ = false; dragTimer_.stop();
    player_.endDrag(); constrainPosition();
}
void PetWindow::mouseReleaseEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) endDrag(); }

void PetWindow::showSettings() {
    if (settingsDialog_) { settingsDialog_->show(); settingsDialog_->raise(); return; }
    auto *dialog = new QDialog(this); settingsDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("Agent Pet settings");
    auto *layout = new QFormLayout(dialog);
    auto *size = new QSpinBox(dialog); size->setRange(160, 320); size->setSingleStep(20); size->setSuffix(" px"); size->setValue(width());
    size->setAccessibleName("Pet size"); layout->addRow("&Size", size);
    connect(size, &QSpinBox::valueChanged, this, &PetWindow::setPetSize);
    auto *top = new QCheckBox("Always on &top", dialog); top->setChecked(onTopAction_->isChecked()); layout->addRow(top);
    connect(top, &QCheckBox::toggled, this, &PetWindow::setOnTop);
    connect(onTopAction_, &QAction::toggled, top, &QCheckBox::setChecked);
    auto *mute = new QCheckBox("&Mute alert bubbles (badge stays visible)", dialog); mute->setChecked(muted_); layout->addRow("Alerts", mute);
    connect(mute, &QCheckBox::toggled, this, &PetWindow::setMuted);
    connect(muteAction_, &QAction::toggled, mute, &QCheckBox::setChecked);
    auto *sound = new QCheckBox("Play a &sound for new alerts", dialog); sound->setChecked(sound_); layout->addRow(sound);
    connect(sound, &QCheckBox::toggled, this, &PetWindow::setSound);
    layout->addRow(integrationSettings(dialog));
    auto *recover = new QPushButton("&Recover position and input", dialog); layout->addRow(recover);
    connect(recover, &QPushButton::clicked, this, &PetWindow::recover);
    auto *about = new QPushButton("Artwork &credits and terms", dialog); layout->addRow(about);
    connect(about, &QPushButton::clicked, this, &PetWindow::showAbout);
    auto *status = new QLabel(dialog); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const auto updateStatus = [this, status] {
        status->setText(store_.error().isEmpty() ? "Preferences are saved automatically. Closing this window keeps monitoring; Quit stops it."
                                               : store_.error() + "\n" + store_.path());
    };
    updateStatus(); layout->addRow(status);
    auto *statusTimer = new QTimer(dialog); statusTimer->setInterval(1000);
    connect(statusTimer, &QTimer::timeout, dialog, updateStatus); statusTimer->start();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *quit = buttons->addButton("&Quit Agent Pet", QDialogButtonBox::DestructiveRole);
    connect(quit, &QPushButton::clicked, this, &PetWindow::requestQuit);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addRow(buttons); dialog->show();
}
QWidget *PetWindow::integrationSettings(QWidget *parent) {
    auto *box = new QGroupBox("Agent integrations", parent);
    auto *layout = new QFormLayout(box);
    for (const QString provider : {"claude", "codex"}) {
        auto *row = new QWidget(box); auto *rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *status = new QLabel(row); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *toggle = new QPushButton(row);
        rowLayout->addWidget(status, 1); rowLayout->addWidget(toggle);
        const auto refresh = [provider, status, toggle] {
            QJsonObject report; QString error;
            if (!runIntegration("inspect", provider, {}, QCoreApplication::applicationFilePath(), report, error)) {
                status->setText(error + "\n" + integrationConfigPath(provider)); toggle->setEnabled(false); return;
            }
            const int owned = report["owned_handlers"].toInt(), expected = report["expected_handlers"].toInt();
            QString text = owned == expected ? "Enabled" : owned == 0 ? "Not enabled" : QString("Partial (%1 of %2 hooks)").arg(owned).arg(expected);
            if (report.contains("warning")) text += " · " + report["warning"].toString();
            status->setText(text + "\n" + report["config"].toString());
            status->setToolTip(report["setup"].toString());
            toggle->setText(owned ? "Disable" : "Enable"); toggle->setEnabled(true);
            toggle->setProperty("enable", owned == 0);
        };
        refresh();
        connect(toggle, &QPushButton::clicked, box, [this, provider, toggle, refresh] {
            const bool enable = toggle->property("enable").toBool();
            const auto executable = QCoreApplication::applicationFilePath();
            const auto question = enable
                ? QString("Add Agent Pet hooks to %1?\n\nHook command: %2\nOther settings and hooks are preserved. "
                          "Register from a permanent install location, then restart the client.").arg(integrationConfigPath(provider), executable)
                : QString("Remove only Agent Pet hooks from %1?").arg(integrationConfigPath(provider));
            if (QMessageBox::question(this, "Agent integrations", question) != QMessageBox::Yes) return;
            QJsonObject report; QString error;
            if (!runIntegration(enable ? "enable" : "disable", provider, {}, executable, report, error))
                QMessageBox::warning(this, "Agent integrations", error);
            refresh();
        });
        layout->addRow(provider == "claude" ? "Claude Code" : "Codex", row);
    }
    auto *coverage = new QLabel("Covers sessions on this machine that send events after setup. Restart the client "
                                "after enabling; silent, remote and container sessions are not discovered. "
                                "Reply to requests in the agent's own terminal or editor.", box);
    coverage->setWordWrap(true); layout->addRow(coverage);
    return box;
}
void PetWindow::showAbout() {
    auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("About Agent Pet — artwork and terms"); dialog->resize(560, 440);
    auto *layout = new QVBoxLayout(dialog);
    auto *credits = new QLabel(QString("<b>Agent Pet %1</b> (revision %2, Qt %3)<br>"
                                       "Application code: MIT License. Artwork: VUP-Simulator team, via "
                                       "<a href='https://github.com/LorisYounger/VPet'>LorisYounger/VPet</a>, under its own terms below.")
                                   .arg(QString(AGENT_PET_VERSION).toHtmlEscaped(), QString(AGENT_PET_REVISION).toHtmlEscaped(), qVersion()), dialog);
    credits->setOpenExternalLinks(true); credits->setTextInteractionFlags(Qt::TextBrowserInteraction); layout->addWidget(credits);
    auto *terms = new QTextBrowser(dialog); terms->setAccessibleName("Artwork terms and third-party notices");
    QString text;
    for (const auto &path : {":/THIRD_PARTY_NOTICES.md", ":/licenses/VPET-ARTWORK-TERMS.md", ":/LICENSE"}) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) text += QString::fromUtf8(file.readAll()) + "\n\n";
    }
    terms->setPlainText(text); layout->addWidget(terms);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); layout->addWidget(buttons); dialog->show();
}
void PetWindow::showPreview() {
    if (previewDialog_) { previewDialog_->show(); previewDialog_->raise(); return; }
    auto *dialog = new QDialog(this); previewDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("Animation preview"); dialog->resize(540, 420);
    auto *layout = new QVBoxLayout(dialog);
    auto *states = new QComboBox(dialog); states->addItems(player_.states()); states->setCurrentText(player_.state());
    states->setAccessibleName("Animation state"); layout->addWidget(states);
    auto *interrupt = new QCheckBox("Interrupt immediately (skip outgoing end)", dialog); layout->addWidget(interrupt);
    auto *play = new QPushButton("&Play selected state", dialog); layout->addWidget(play);
    connect(play, &QPushButton::clicked, dialog, [this, states, interrupt] { player_.select(states->currentText(), interrupt->isChecked()); });
    auto *pause = new QCheckBox("&Pause", dialog); pause->setChecked(player_.paused()); layout->addWidget(pause);
    connect(pause, &QCheckBox::toggled, &player_, &Player::setPaused);
    auto *step = new QPushButton("Step one &frame", dialog); layout->addWidget(step);
    connect(step, &QPushButton::clicked, dialog, [this, pause] { pause->setChecked(true); player_.advance(); });
    auto *detail = new QLabel(dialog); detail->setWordWrap(true); detail->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(detail);
    auto *log = new QPlainTextEdit(dialog); log->setReadOnly(true); log->setMaximumBlockCount(100); log->setAccessibleName("Transition history"); layout->addWidget(log);
    const auto refresh = [this, detail, log] {
        const QString transition = player_.state() + " / " + player_.phase() + " / " + player_.sequence();
        if (log->property("transition").toString() != transition) { log->appendPlainText(transition); log->setProperty("transition", transition); }
        detail->setText(QString("%1\nRequested: %2 · frame %3/%4 · %5 ms\nCache: %6 / %7 KiB\n%8")
            .arg(transition, player_.requestedState()).arg(player_.frameIndex() + 1).arg(player_.frameCount())
            .arg(player_.frameDuration()).arg(player_.cacheKiB()).arg(Player::cacheLimitKiB).arg(player_.error()));
    };
    connect(&player_, &Player::changed, dialog, refresh);
    connect(&player_, &Player::failed, dialog, [refresh](const QString &) { refresh(); });
    connect(dialog, &QDialog::finished, this, [this] { player_.setPaused(false); });
    refresh(); dialog->show();
}
}
