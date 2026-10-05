#include "updates/controller.h"
#include "pet_window.h"
#include "drag_monitor.h"
#include "ipc/autostart.h"
#include "providers/integrations.h"
#include "version.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QKeyEvent>
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
#include <QStandardItemModel>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>

namespace pet {
// The persistent attention badge, drawn on the pet and on the tray icon.
static void drawBadge(QPainter &painter, const QRect &badge, const QColor &color, const QString &text) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::white, 2)); painter.setBrush(color);
    painter.drawEllipse(badge);
    auto font = painter.font(); font.setBold(true); font.setPixelSize(badge.height() * 3 / 5); painter.setFont(font);
    painter.drawText(badge, Qt::AlignCenter, text);
    painter.restore();
}
PetWindow::PetWindow(QWidget *parent, const QString &path, bool persist)
    : QWidget(parent), player_(this), ambient_(player_, this), mood_(player_, this), eggs_(player_, this), store_(path), menu_(this), tray_(this), persist_(persist) {
    const auto preferences = persist_ ? store_.load() : Preferences{};
    setWindowTitle("Agent Pet");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, preferences.onTop);
    setAttribute(Qt::WA_TranslucentBackground);
    setAccessibleName("Agent Pet");
    setToolTip("Click for running sessions · Hold still to pet · Drag to move, or throw · Push past a screen edge to hide\n"
               "Right-click for controls · Esc to quit");
    setPetSize(preferences.size);
    muted_ = preferences.muted; sound_ = preferences.sound; bubbles_ = qBound(0, preferences.bubbles, 2);
    ambient_.setLevel(AmbientLevel(qBound(0, preferences.ambient, 2)));
    mood_.setSetting(MoodSetting(qBound(0, preferences.mood, 2))); mood_.setTurns(preferences.turns);
    connect(&mood_, &Mood::counted, this, [this] { if (ready_) saveTimer_.start(); });
    touchEnabled_ = preferences.touch;
    eggs_.setEnabled(preferences.easterEggs); eggs_.setBirthday(preferences.birthday);
    ambient_.setEasterEggs(&eggs_);
    connect(&player_, &Player::entered, this, &PetWindow::entered);
    autostart_ = preferences.autostart; presence_.setPolicy(preferences.whenIdle);
    connect(&player_, &Player::changed, this, qOverload<>(&PetWindow::update));
    connect(&player_, &Player::completed, this, [this](const QString &state) {
        if (quitting_ && state == "closing") qApp->quit();
    });
    showAction_ = menu_.addAction("Show pet");
    showAction_->setCheckable(true); showAction_->setChecked(true);
    connect(showAction_, &QAction::toggled, this, [this](bool shown) { setPetHidden(!shown); });
    menu_.addSeparator();
    auto *states = menu_.addMenu("Preview state");
    for (const auto &state : player_.states())
        states->addAction(state, this, [this, state] { if (!quitting_) player_.select(state); });
    menu_.addAction("Running sessions…", this, &PetWindow::sessionsRequested);
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
    trayBase_ = player_.pixmap();
    updateTrayIcon();
    tray_.setToolTip("Agent Pet — idle");
    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason != QSystemTrayIcon::Trigger) return;
        if (presence_.canHide()) setPetHidden(!petHidden());
        else recover();
    });
    setTrayAvailable(QSystemTrayIcon::isSystemTrayAvailable());
    recoveryTimer_.setSingleShot(true);
    connect(&recoveryTimer_, &QTimer::timeout, this, [this] { setClickThrough(false); });
    dragTimer_.setInterval(40);
    connect(&dragTimer_, &QTimer::timeout, this, [this] {
        const auto native = nativeLeftButtonDown();
        const auto position = nativePos();
        samples_.append({pressTimer_.elapsed(), position}); // The last ones tell how fast it was let go.
        if (samples_.size() > 16) samples_.removeFirst();
        if (!(native.has_value() ? *native : bool(QApplication::mouseButtons() & Qt::LeftButton))) endDrag(true);
        // Lift the pet only once it moves, so a click opens the session list without the drop animation.
        else if (!player_.isDragging() && position != pressPosition_) player_.beginDrag();
        // Held still past a click, a press pets whatever it landed on until let go.
        else if (!player_.held() && !pressTouch_.isEmpty() && pressTimer_.elapsed() >= touch::holdMs) player_.hold(pressTouch_);
    });
    flightTimer_.setInterval(16);
    connect(&flightTimer_, &QTimer::timeout, this, [this] {
        if (!flight_) { flightTimer_.stop(); return; }
        const bool airborne = flight_->step(flightClock_.restart());
        move(flight_->position());
        if (!airborne) land();
    });
    slide_.setDuration(280); slide_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { move(value.toPoint()); });
    connect(&slide_, &QVariantAnimation::finished, this, [this] {
        // Arrived: hide only if the pet is still idle and nobody has picked it up meanwhile.
        const auto &touch = player_.touch();
        if (dragging_ || quitting_ || flight_ || player_.requestedState() != "idle") return;
        const auto hide = slideEdge_ == touch::Edge::Left ? touch.edgeLeft : slideEdge_ == touch::Edge::Right ? touch.edgeRight : QString();
        if (!hide.isEmpty()) player_.select(hide);
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
QRect PetWindow::figure() const {
    // Ask the display server: some window managers leave Qt's cached position stale.
    return QRect(nativePos(), size()).adjusted(width() / 4, 0, -width() / 4, 0);
}
QPoint PetWindow::nativePos() const {
    const auto origin = isVisible() ? nativeWindowOrigin(winId()) : std::nullopt;
    return origin.value_or(pos());
}
void PetWindow::watchScreen(QScreen *screen) {
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { constrainPosition(); });
}
void PetWindow::constrainPosition() {
    const auto &touch = player_.touch();
    const auto adjusted = hiding()
        ? touch::hidePosition(edge_, QRect(pos(), size()), screenAreas(),
                              edge_ == touch::Edge::Left ? touch.edgeLeftAt : touch.edgeRightAt, touch.scale)
        : Preferences::visiblePosition(pos(), size(), screenAreas());
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
    setWindowFlag(Qt::WindowTransparentForInput, enabled); showAfterFlagChange(position);
    if (enabled) recoveryTimer_.start(Preferences::recoveryMs);
    else recoveryTimer_.stop();
}
void PetWindow::setOnTop(bool enabled) {
    const QSignalBlocker blocker(onTopAction_);
    onTopAction_->setChecked(enabled);
    const auto position = pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled); showAfterFlagChange(position);
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
void PetWindow::setBubbles(int level) {
    level = qBound(0, level, 2);
    if (level == bubbles_) return;
    bubbles_ = level; emit notificationsChanged();
    if (ready_) saveTimer_.start();
}
void PetWindow::setAmbientLevel(int level) {
    level = qBound(0, level, 2);
    if (level == ambientLevel()) return;
    ambient_.setLevel(AmbientLevel(level));
    if (ready_) saveTimer_.start();
}
void PetWindow::setMoodLevel(int level) {
    level = qBound(0, level, 2);
    if (level == moodLevel()) return;
    mood_.setSetting(MoodSetting(level));
    if (ready_) saveTimer_.start();
}
void PetWindow::setTouchEnabled(bool enabled) {
    if (enabled == touchEnabled_) return;
    touchEnabled_ = enabled;
    if (ready_) saveTimer_.start();
}
void PetWindow::setEasterEggsEnabled(bool enabled) {
    if (enabled == easterEggsEnabled()) return;
    eggs_.setEnabled(enabled);
    if (ready_) saveTimer_.start();
}
void PetWindow::setBirthday(const QString &monthDay) {
    if (monthDay == birthday() || !eggs_.setBirthday(monthDay)) return;
    if (ready_) saveTimer_.start();
}
void PetWindow::showAfterFlagChange(QPoint position) {
    // Changing window flags hides the window; a hidden pet stays hidden until shown.
    if (!petHidden()) { show(); move(position); }
}
void PetWindow::recover() {
    slide_.stop();
    applyPresence(presence_.setUserHidden(false));
    if (hiding()) player_.select("idle", true); // Out from behind the edge, to be placed in plain view.
    setClickThrough(false);
    move(Preferences::visiblePosition({-1000000, -1000000}, size(), screenAreas()));
    show(); raise();
}
void PetWindow::setTrayAvailable(bool available) {
    presence_.setTrayAvailable(available);
    showAction_->setEnabled(available);
    showAction_->setToolTip(available ? QString() : "Hiding needs a system tray to show the pet again");
    tray_.setVisible(available);
    if (!available) applyPresence(presence_.setUserHidden(false));
}
void PetWindow::setPetHidden(bool hidden) {
    if (quitting_) return;
    applyPresence(presence_.setUserHidden(hidden));
    const QSignalBlocker blocker(showAction_);
    showAction_->setChecked(!petHidden()); // Refused without a tray.
}
void PetWindow::applyPresence(Presence::Action action) {
    if (action == Presence::Action::Quit) { requestQuit(); return; }
    if (action == Presence::Action::None) return;
    const bool hidden = action == Presence::Action::Hide;
    {
        const QSignalBlocker blocker(showAction_);
        showAction_->setChecked(!hidden);
    }
    if (hidden) {
        endDrag();
        if (clickThrough_) setClickThrough(false);
        hide();
        player_.setPaused(true); // Nobody sees it; sessions keep driving its state.
    } else {
        const auto position = pos();
        show(); move(position); raise();
        player_.setPaused(false);
        constrainPosition();
    }
    emit presenceChanged();
}
void PetWindow::updatePresence(int sessions, qint64 now) {
    if (quitting_) return;
    // The idle policy may have changed from the command line since it was read.
    if (presence_.idleDeadline() && now >= presence_.idleDeadline()) refreshStartup();
    applyPresence(presence_.update(sessions, now));
}
void PetWindow::setStatus(int sessions, int attention, int errors) {
    QString text = sessions == 0 ? QString("Agent Pet — idle")
                                 : QString("Agent Pet — %1 session%2").arg(sessions).arg(sessions == 1 ? "" : "s");
    if (attention > 0) text += QString(" · %1 need%2 attention").arg(attention).arg(attention == 1 ? "s" : "");
    if (errors > 0) text += QString(" · %1 tool error%2").arg(errors).arg(errors == 1 ? "" : "s");
    if (tray_.toolTip() != text) tray_.setToolTip(text);
    // Redraw only on a change: every icon update is a D-Bus round trip on desktop trays.
    const int badge = qBound(0, attention, 9);
    if (badge == trayAttention_ && (errors > 0) == trayError_) return;
    trayAttention_ = badge; trayError_ = errors > 0;
    updateTrayIcon();
}
void PetWindow::updateTrayIcon() {
    constexpr int size = 64;
    QPixmap icon(size, size); icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.setRenderHint(QPainter::Antialiasing);
    if (!trayBase_.isNull()) {
        const auto scaled = trayBase_.size().scaled(size, size, Qt::KeepAspectRatio);
        painter.drawPixmap(QRect(QPoint((size - scaled.width()) / 2, (size - scaled.height()) / 2), scaled), trayBase_);
    } else {
        painter.setBrush(QColor("#fff2cf")); painter.setPen(QColor("#453324"));
        painter.drawRoundedRect(QRect(8, 8, size - 16, size - 16), 12, 12);
    }
    // Tray icons are tiny: the dot takes the top-right corner, larger than on the pet.
    const QRect badge(size * 9 / 16, 1, size * 7 / 16 - 1, size * 7 / 16 - 1);
    if (trayAttention_ > 0) drawBadge(painter, badge, QColor("#d9480f"), trayAttention_ > 1 ? QString::number(trayAttention_) : "!");
    else if (trayError_) drawBadge(painter, badge, QColor("#c92a2a"), "!");
    painter.end();
    tray_.setIcon(QIcon(icon));
}
void PetWindow::refreshStartup() {
    if (!persist_) return;
    const auto preferences = store_.load();
    autostart_ = preferences.autostart; presence_.setPolicy(preferences.whenIdle);
}
void PetWindow::setAutostart(bool enabled) {
    autostart_ = enabled;
    writePreferences([enabled](Preferences &preferences) { preferences.autostart = enabled; });
}
void PetWindow::setWhenIdle(IdlePolicy policy) {
    presence_.setPolicy(policy);
    writePreferences([policy](Preferences &preferences) { preferences.whenIdle = policy; });
}
bool PetWindow::savePreferences() { return writePreferences([](Preferences &) {}); }
bool PetWindow::writePreferences(const std::function<void(Preferences &)> &change) {
    if (!persist_ || !ready_) return true;
    // Start from the file: `agent-pet autostart` may have changed the startup keys meanwhile.
    auto preferences = store_.load();
    preferences.size = width(); preferences.position = pos(); preferences.hasPosition = true;
    preferences.onTop = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    preferences.muted = muted_; preferences.sound = sound_; preferences.bubbles = bubbles_;
    preferences.ambient = ambientLevel(); preferences.mood = moodLevel(); preferences.turns = mood_.turns();
    preferences.touch = touchEnabled_; preferences.easterEggs = easterEggsEnabled(); preferences.birthday = birthday();
    change(preferences);
    autostart_ = preferences.autostart; presence_.setPolicy(preferences.whenIdle);
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
        drawBadge(painter, badge, QColor("#d9480f"), attention_ > 1 ? QString::number(qMin(attention_, 9)) : "!");
    }
}
void PetWindow::requestQuit() {
    if (quitting_) { qApp->quit(); return; }
    slide_.stop(); endDrag(); setClickThrough(false); savePreferences(); quitting_ = true;
    emit quitRequested(); // Monitoring stops here; closing settings never reaches this.
    if (settingsDialog_) settingsDialog_->close();
    if (previewDialog_) previewDialog_->close();
    menu_.setEnabled(false);
    if (petHidden()) { qApp->quit(); return; } // No one would see the closing animation.
    player_.setPaused(false);
    if (!player_.select("closing", true) || player_.stopped()) { qApp->quit(); return; }
    // A broken shutdown asset must never prevent exit.
    QTimer::singleShot(5000, this, [] { qApp->quit(); });
}
void PetWindow::closeEvent(QCloseEvent *event) { event->ignore(); requestQuit(); }
void PetWindow::contextMenuEvent(QContextMenuEvent *event) { menu_.popup(event->globalPos()); }
void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || quitting_) return;
    if (flight_) land(); // Caught in mid-air.
    slide_.stop(); // Caught on its way to the edge.
    dragging_ = true;
    pressPosition_ = nativePos(); pressTimer_.start(); samples_.clear();
    // A pet hiding at an edge is only partly there; it can be dragged back out, not petted.
    pressTouch_ = touchEnabled_ && !hiding() ? player_.touchAt(event->position(), width()) : QString();
    dragOffset_ = event->globalPosition().toPoint() - pos();
    fallbackDrag_ = !windowHandle()->startSystemMove();
    dragTimer_.start();
}
void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_ && fallbackDrag_ && (event->buttons() & Qt::LeftButton)) {
        player_.beginDrag();
        move(event->globalPosition().toPoint() - dragOffset_);
    }
}
void PetWindow::endDrag(bool released) {
    if (flight_) land(); // Hiding, quitting or click-through: put it down where it is.
    if (!dragging_) return;
    dragging_ = false; fallbackDrag_ = false; dragTimer_.stop(); pressTouch_.clear();
    // Every press starts a move, so a short press that left the pet in place is a click.
    const bool click = released && nativePos() == pressPosition_ && pressTimer_.isValid() && pressTimer_.elapsed() < touch::holdMs;
    if (released && !click) letGo(touch::velocity(samples_));
    else { player_.release(); constrainPosition(); }
    samples_.clear();
    // Wait for the window manager to release its move grab: a popup opened while it
    // holds the pointer cannot take its own grab and closes at once.
    if (click) QTimer::singleShot(150, this, &PetWindow::sessionsRequested);
}
void PetWindow::letGo(QPointF velocity) {
    const auto &touch = player_.touch();
    const auto fall = velocity.x() < 0 ? touch.fallLeft : touch.fallRight;
    if (touchEnabled_ && !quitting_ && !fall.isEmpty() && qHypot(velocity.x(), velocity.y()) >= touch::throwSpeed) {
        // The fall replaces the drag as what is held, so session changes still wait for the landing.
        const QRect window(nativePos(), size());
        player_.hold(fall);
        flight_.emplace(window.topLeft(), velocity, touch::areaFor(window, screenAreas()), size());
        flightClock_.start(); flightTimer_.start();
        return;
    }
    player_.release();
    // Only an idle pet hides; one with work to show stays in view.
    const auto edge = touchEnabled_ ? touch::pushedEdge(QRect(nativePos(), size()), screenAreas()) : touch::Edge::None;
    const auto hide = edge == touch::Edge::Left ? touch.edgeLeft : edge == touch::Edge::Right ? touch.edgeRight : QString();
    if (!hide.isEmpty() && player_.requestedState() == "idle") { slideToEdge(edge); return; }
    constrainPosition();
}
void PetWindow::slideToEdge(touch::Edge edge) {
    const auto &touch = player_.touch();
    const QRect window(nativePos(), size());
    const auto target = touch::hidePosition(edge, window, screenAreas(), edge == touch::Edge::Left ? touch.edgeLeftAt : touch.edgeRightAt, touch.scale);
    slideEdge_ = edge;
    slide_.stop();
    if (target == window.topLeft()) { slide_.setStartValue(target); slide_.setEndValue(target); }
    else { slide_.setStartValue(window.topLeft()); slide_.setEndValue(target); }
    slide_.start();
}
void PetWindow::land() {
    flightTimer_.stop();
    if (!flight_) return;
    flight_.reset();
    player_.release(); // Plays the landing, then whatever sessions asked for meanwhile.
    constrainPosition();
}
void PetWindow::entered(const QString &state) {
    const auto &touch = player_.touch();
    const auto edge = state.isEmpty() ? touch::Edge::None
        : state == touch.edgeLeft ? touch::Edge::Left : state == touch.edgeRight ? touch::Edge::Right : touch::Edge::None;
    if (edge == edge_) return;
    edge_ = edge;
    // Into hiding, or back out because something else is showing. A drag in progress owns the position.
    if (!dragging_) constrainPosition();
}
void PetWindow::mouseReleaseEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) endDrag(true); }
void PetWindow::keyPressEvent(QKeyEvent *event) {
    if (eggs_.key(event->key()) && !quitting_) eggs_.surprise("konami");
    QWidget::keyPressEvent(event);
}

void PetWindow::setUpdates(updates::Controller *controller) {
    updates_ = controller;
    auto *action = menu_.addAction(controller->indicator(), this, [this, controller] { controller->showSettings(this); });
    connect(controller, &updates::Controller::changed, action, [controller, action] { action->setText(controller->indicator()); });
}
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
    auto *bubbles = new QComboBox(dialog);
    bubbles->addItems({"Only when a session needs me", "When a session needs me or a tool fails", "Also when a turn finishes"});
    bubbles->setCurrentIndex(bubbles_); bubbles->setAccessibleName("Show alert bubbles");
    layout->addRow("Show &bubbles", bubbles);
    connect(bubbles, &QComboBox::currentIndexChanged, this, &PetWindow::setBubbles);
    auto *ambient = new QComboBox(dialog);
    ambient->addItems({"Off (no fidgets or alternate idle loops)", "Subtle (a fidget about once a minute)", "Lively (a fidget every 15–25 seconds)"});
    ambient->setCurrentIndex(ambientLevel()); ambient->setAccessibleName("Idle animation");
    ambient->setToolTip("What the pet does on its own while no agent needs it: fidgets, alternate idle loops, and\n"
                        "dozing off after about ten quiet minutes. Any agent activity ends it at once.");
    layout->addRow("&Idle animation", ambient);
    connect(ambient, &QComboBox::currentIndexChanged, this, &PetWindow::setAmbientLevel);
    auto *mood = new QComboBox(dialog);
    mood->addItems({"Off (always neutral)", "Cheerful only (happy after a run of finished turns)",
                    "Full (also droopy after repeated tool errors)"});
    mood->setCurrentIndex(moodLevel()); mood->setAccessibleName("Mood");
    mood->setToolTip("Whether finished turns and tool errors change how the idle pet looks. The mood fades\n"
                     "back to neutral over a few quiet minutes.");
    layout->addRow("M&ood", mood);
    connect(mood, &QComboBox::currentIndexChanged, this, &PetWindow::setMoodLevel);
    auto *touch = new QCheckBox("React to &petting, throwing and screen edges", dialog);
    touch->setChecked(touchEnabled_); touch->setAccessibleName("Touch reactions");
    touch->setToolTip("Hold the pet still on its head, cheek or body to pet it. Let go while dragging fast and it\n"
                      "falls to the bottom of the screen. Push it past the left or right edge while it idles and it\n"
                      "hides there until an agent needs it. Off, the pet is only dragged.");
    layout->addRow("&Touch", touch);
    connect(touch, &QCheckBox::toggled, this, &PetWindow::setTouchEnabled);
    auto *eggs = new QCheckBox("Special days, late nights and surprises", dialog);
    eggs->setChecked(easterEggsEnabled()); eggs->setAccessibleName("Easter eggs");
    eggs->setToolTip("Small surprises: a greeting on May 20 and on your birthday, extra yawns late at night, a\n"
                     "dance for turns finished on a Friday evening, a bigger celebration for a very long turn,\n"
                     "and a startled jump when an agent runs a destructive command. Some are left to be found.");
    layout->addRow("&Easter eggs", eggs);
    connect(eggs, &QCheckBox::toggled, this, &PetWindow::setEasterEggsEnabled);
    auto *birthdayRow = new QWidget(dialog); auto *birthdayLayout = new QHBoxLayout(birthdayRow);
    birthdayLayout->setContentsMargins(0, 0, 0, 0);
    auto *hasBirthday = new QCheckBox("Celebrate on", birthdayRow); hasBirthday->setAccessibleName("Birthday");
    auto *birthdayDate = new QDateEdit(birthdayRow); birthdayDate->setAccessibleName("Birthday date");
    // The year is never shown or kept; a leap year lets February 29 be picked.
    birthdayDate->setDisplayFormat("MMMM d"); birthdayDate->setDateRange(QDate(2000, 1, 1), QDate(2000, 12, 31));
    const auto saved = QDate::fromString("2000-" + birthday(), "yyyy-MM-dd");
    hasBirthday->setChecked(saved.isValid()); birthdayDate->setDate(saved.isValid() ? saved : QDate(2000, 1, 1));
    birthdayDate->setEnabled(saved.isValid());
    birthdayLayout->addWidget(hasBirthday); birthdayLayout->addWidget(birthdayDate, 1);
    layout->addRow("Birthday", birthdayRow);
    const auto applyBirthday = [this, hasBirthday, birthdayDate] {
        birthdayDate->setEnabled(hasBirthday->isChecked());
        setBirthday(hasBirthday->isChecked() ? birthdayDate->date().toString("MM-dd") : QString());
    };
    connect(hasBirthday, &QCheckBox::toggled, this, applyBirthday);
    connect(birthdayDate, &QDateEdit::dateChanged, this, applyBirthday);
    if (updates_) {
        auto *updatesButton = new QPushButton(updates_->indicator(), dialog); layout->addRow(updatesButton);
        connect(updatesButton, &QPushButton::clicked, this, [this] { updates_->showSettings(this); });
        connect(updates_, &updates::Controller::changed, updatesButton, [this, updatesButton] { updatesButton->setText(updates_->indicator()); });
    }
    layout->addRow(startupSettings(dialog));
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
QWidget *PetWindow::startupSettings(QWidget *parent) {
    refreshStartup();
    auto *box = new QGroupBox("Startup", parent); box->setObjectName("startup");
    auto *layout = new QFormLayout(box);
    auto *autostart = new QCheckBox("&Autostart on agent session start", box);
    autostart->setChecked(autostart_);
    autostart->setToolTip("When a connected agent starts a session and no pet is running, its hook launches the pet.\n"
                          "Needs a graphical session; SSH and container sessions do not start it.");
    layout->addRow(autostart);
    connect(autostart, &QCheckBox::toggled, this, &PetWindow::setAutostart);
    auto *login = new QCheckBox("Start Agent Pet at &login", box);
    login->setChecked(loginStartEnabled());
    login->setToolTip("Starts the pet whenever you log in to your desktop, even before an agent session.");
    layout->addRow(login);
    connect(login, &QCheckBox::toggled, this, [login](bool enabled) {
        QString error;
        if (setLoginStart(enabled, QCoreApplication::applicationFilePath(), &error)) return;
        QSignalBlocker blocker(login); login->setChecked(!enabled);
        QMessageBox::warning(login, "Agent Pet", "Cannot change start at login: " + error);
    });
    auto *idle = new QComboBox(box); idle->setAccessibleName("When no sessions remain");
    idle->addItem("Keep the pet running", int(IdlePolicy::Keep));
    idle->addItem("Hide the pet (tray icon stays)", int(IdlePolicy::Hide));
    idle->addItem("Quit Agent Pet", int(IdlePolicy::Quit));
    if (auto *model = qobject_cast<QStandardItemModel *>(idle->model()); model && !presence_.canHide()) {
        model->item(1)->setEnabled(false); // Without a tray nothing could show it again; it keeps running.
        model->item(1)->setToolTip("Needs a system tray");
    }
    idle->setCurrentIndex(qMax(0, idle->findData(int(whenIdle()))));
    layout->addRow("When no sessions &remain", idle);
    connect(idle, &QComboBox::currentIndexChanged, this, [this, idle] { setWhenIdle(IdlePolicy(idle->currentData().toInt())); });
    auto *note = new QLabel("Applies two minutes after the last session ends; a new session cancels it. "
                            "A pet hidden this way returns with the next session.", box);
    note->setWordWrap(true); layout->addRow(note);
    return box;
}
QWidget *PetWindow::integrationSettings(QWidget *parent) {
    auto *box = new QGroupBox("Agent integrations", parent); box->setObjectName("integrations");
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
                                       "Application code: Apache License 2.0. Artwork: VUP-Simulator team, via "
                                       "<a href='https://github.com/LorisYounger/VPet'>LorisYounger/VPet</a>, under its own terms below.")
                                   .arg(QString(AGENT_PET_VERSION).toHtmlEscaped(), QString(AGENT_PET_REVISION).toHtmlEscaped(), qVersion()), dialog);
    credits->setOpenExternalLinks(true); credits->setTextInteractionFlags(Qt::TextBrowserInteraction); layout->addWidget(credits);
    auto *terms = new QTextBrowser(dialog); terms->setAccessibleName("Artwork terms and third-party notices");
    QString text;
    for (const auto &path : {":/NOTICE", ":/THIRD_PARTY_NOTICES.md", ":/licenses/VPET-ARTWORK-TERMS.md", ":/LICENSE"}) {
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
