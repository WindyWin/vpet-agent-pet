#include "updates/controller.h"
#include "pet_window.h"
#include "animation/pet_library.h"
#include "desktop/pet_downloader.h"
#include "desktop/pet_picker.h"
#include "platform/contracts/native_window.h"
#include "ipc/autostart.h"
#include "providers/integrations.h"
#include "i18n/contexts.h"
#include "i18n/language.h"
#include "version.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
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
#include <QTabWidget>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>

namespace pet {
// Qt has no Vietnamese catalog for its standard buttons, so the button is labeled here.
static void warn(QWidget *parent, const QString &title, const QString &text) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
    box.addButton(PetWindow::tr("OK"), QMessageBox::AcceptRole); box.exec();
}
// Dialogs are built in one go, so a language change rebuilds them rather than relabeling them, at the same place.
// The old one is closed and deleted later: this may run inside one of its own signals. `open` builds the new one,
// or returns null when it should not come back.
static void rebuild(QPointer<QDialog> &slot, QObject *context, std::function<QDialog *()> open) {
    auto *dialog = slot.data(); if (!dialog) return;
    const auto position = dialog->pos();
    slot.clear(); dialog->close();
    QTimer::singleShot(0, context, [open = std::move(open), position] { if (auto *fresh = open()) fresh->move(position); });
}
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
// Hiding behind a screen edge is a rest: it lasts while the pet idles, and session activity ends it.
static behavior::Intent edgeIntent(const QString &state) {
    return {"touch", "edge", state, behavior::Policy::Ambient, behavior::Lifetime::Persistent};
}
PetWindow::PetWindow(QWidget *parent, const QString &path, bool persist)
    : QWidget(parent), player_(this), stage_(player_, [this] { return eggs_.now().toMSecsSinceEpoch(); }, this),
      ambient_(player_, this), activity_(player_, this), mood_(player_, this), eggs_(player_, this), store_(path), menu_(this), tray_(this), persist_(persist) {
    const auto preferences = persist_ ? store_.load() : Preferences{};
    setWindowTitle("Agent Pet");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, preferences.onTop);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow); // macOS otherwise hides tool windows while another app is active.
    setAccessibleName("Agent Pet");
    setPetSize(preferences.size);
    muted_ = preferences.muted; sound_ = preferences.sound; bubbles_ = qBound(0, preferences.bubbles, 2);
    ambient_.setLevel(AmbientLevel(qBound(0, preferences.ambient, 2)));
    activity_.setStyle(ActivityStyle(qBound(0, preferences.activity, 2)));
    mood_.setSetting(MoodSetting(qBound(0, preferences.mood, 2))); mood_.setTurns(preferences.turns);
    connect(&mood_, &Mood::counted, this, [this] { if (ready_) saveTimer_.start(); });
    touchEnabled_ = preferences.touch; wanderEnabled_ = preferences.wander; recapEnabled_ = preferences.recap;
    ambient_.setMoveGate([this](const Move &move) { return canWander(move); });
    eggs_.setReminderSchedule(preferences.reminderSchedule);
    eggs_.setEnabled(preferences.easterEggs); eggs_.setBirthday(preferences.birthday);
    wellness_.setEyeMinutes(preferences.eyeMinutes); wellness_.setWaterMinutes(preferences.waterMinutes);
    ambient_.setEasterEggs(&eggs_);
    ambient_.setStage(&stage_); eggs_.setStage(&stage_);
    connect(&player_, &Player::entered, this, &PetWindow::entered);
    autostart_ = preferences.autostart; presence_.setPolicy(preferences.whenIdle);
    language_ = preferences.language;
    pet_ = preferences.pet;
    connect(&player_, &Player::changed, this, qOverload<>(&PetWindow::update));
    // Shutdown always ends: its animation finished, failed or ran out of time, or nobody could see it.
    connect(&stage_, &Stage::finished, this, [] { qApp->quit(); });
    // Monitoring stops when quitting starts, so this keeps the runtime's clock going for the shutdown's bound.
    quitTimer_.setInterval(250);
    connect(&quitTimer_, &QTimer::timeout, this, [this] { stage_.runtime().tick(); });
    touchClock_.start();
    // Everyday actions stay at the top level; previews, troubleshooting, updates and credits go under More.
    // retranslate() labels them.
    showAction_ = menu_.addAction(QString());
    showAction_->setCheckable(true); showAction_->setChecked(true);
    connect(showAction_, &QAction::toggled, this, [this](bool shown) { setPetHidden(!shown); });
    menu_.addSeparator();
    sessionsAction_ = menu_.addAction(QString(), this, &PetWindow::sessionsRequested);
    recapAction_ = menu_.addAction(QString(), this, &PetWindow::recapRequested);
    muteAction_ = menu_.addAction(QString());
    muteAction_->setCheckable(true); muteAction_->setChecked(muted_);
    connect(muteAction_, &QAction::toggled, this, &PetWindow::setMuted);
    onTopAction_ = menu_.addAction(QString());
    onTopAction_->setCheckable(true); onTopAction_->setChecked(preferences.onTop);
    connect(onTopAction_, &QAction::toggled, this, &PetWindow::setOnTop);
    menu_.addSeparator();
    updateAction_ = menu_.addAction(QString()); updateAction_->setVisible(false); // Only while an update waits.
    settingsAction_ = menu_.addAction(QString(), this, &PetWindow::showSettings);
    auto *more = moreMenu_ = menu_.addMenu(QString());
    statesMenu_ = more->addMenu(QString());
    for (const auto &state : player_.states()) // Catalog names, for developers.
        statesMenu_->addAction(state, this, [this, state] { if (!quitting_) player_.select(state); });
    previewAction_ = more->addAction(QString(), this, &PetWindow::showPreview);
    more->addSeparator();
    clickAction_ = more->addAction(QString());
    clickAction_->setCheckable(true);
    connect(clickAction_, &QAction::toggled, this, &PetWindow::setClickThrough);
    recoverAction_ = more->addAction(QString(), this, &PetWindow::recover);
    more->addSeparator();
    updatesItem_ = more->addAction(QString()); updatesItem_->setVisible(false);
    // Connected once; they open whichever controller setUpdates() handed over last.
    const auto openUpdates = [this] { if (updates_) updates_->showSettings(this); };
    connect(updatesItem_, &QAction::triggered, this, openUpdates);
    connect(updateAction_, &QAction::triggered, this, openUpdates);
    aboutAction_ = more->addAction(QString(), this, &PetWindow::showAbout);
    menu_.addSeparator();
    quitAction_ = menu_.addAction(QString(), this, &PetWindow::requestQuit);
    trayBase_ = player_.pixmap();
    updateTrayIcon();
    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason != QSystemTrayIcon::Trigger) return;
        if (presence_.canHide()) setPetHidden(!petHidden());
        else recover();
    });
    setTrayAvailable(QSystemTrayIcon::isSystemTrayAvailable());
    retranslate();
    recoveryTimer_.setSingleShot(true);
    connect(&recoveryTimer_, &QTimer::timeout, this, [this] { setClickThrough(false); });
    dragTimer_.setInterval(40);
    connect(&dragTimer_, &QTimer::timeout, this, [this] {
        const auto native = platform::nativeLeftButtonDown();
        const auto position = nativePos();
        samples_.append({pressTimer_.elapsed(), position}); // The last ones tell how fast it was let go.
        if (samples_.size() > 16) samples_.removeFirst();
        if (!(native.has_value() ? *native : bool(QApplication::mouseButtons() & Qt::LeftButton))) endDrag(true);
        // Lift the pet only once it moves, so a click opens the session list without the drop animation.
        else if (!player_.isDragging() && position != pressPosition_) player_.beginDrag();
        // Held still past a click, a press pets whatever it landed on until let go.
        else if (!player_.held() && !pressTouch_.isEmpty() && pressTimer_.elapsed() >= touch::holdMs) player_.hold(pressTouch_);
        if (patience_.petting(touchEnabled_ && dragging_ && player_.held()
                             && !pressTouch_.isEmpty() && player_.state() == pressTouch_, touchClock_.elapsed()))
            beginQuit(Pet::tr("Too much petting! I need a break. Bye!"));
        if (patience_.dragging(touchEnabled_ && dragging_ && player_.isDragging(), touchClock_.elapsed()))
            beginQuit(Pet::tr("Put me down! I'm leaving!"));
    });
    flightTimer_.setInterval(16);
    connect(&flightTimer_, &QTimer::timeout, this, [this] {
        if (!flight_) { flightTimer_.stop(); return; }
        const bool airborne = flight_->step(flightClock_.restart());
        move(flight_->position());
        if (!airborne) land();
    });
    walkTimer_.setInterval(33);
    connect(&walkTimer_, &QTimer::timeout, this, &PetWindow::walkStep);
    slide_.setDuration(280); slide_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { move(value.toPoint()); });
    connect(&slide_, &QVariantAnimation::finished, this, [this] {
        // Arrived: it hides there as a rest, which the runtime allows only while nothing else is going on.
        const auto &touch = player_.touch();
        syncBehavior();
        if (dragging_ || quitting_ || flight_) return;
        const auto hide = slideEdge_ == touch::Edge::Left ? touch.edgeLeft : slideEdge_ == touch::Edge::Right ? touch.edgeRight : QString();
        if (!hide.isEmpty()) stage_.runtime().submit(edgeIntent(hide));
    });
    saveTimer_.setSingleShot(true); saveTimer_.setInterval(250);
    connect(&saveTimer_, &QTimer::timeout, this, &PetWindow::savePreferences);
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { requestQuit(); });
    new QShortcut(QKeySequence(Qt::Key_Space), this, [this] {
        if (!quitting_) player_.play(player_.state() == player_.stateFor("idle") ? "thinking" : "idle");
    });
    new QShortcut(QKeySequence(Qt::Key_Menu), this, [this] { menu_.popup(mapToGlobal(rect().center())); });
    new QShortcut(QKeySequence("Ctrl+,"), this, [this] { showSettings(); });
    for (auto *screen : QApplication::screens()) watchScreen(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watchScreen(screen); constrainPosition(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { QTimer::singleShot(0, this, &PetWindow::constrainPosition); });
    move(preferences.hasPosition ? preferences.position : QPoint(-1000000, -1000000));
    constrainPosition();
    ready_ = true;
    stage_.runtime().submit({"lifecycle", "start", "start", behavior::Policy::Startup});
}
PetWindow::~PetWindow() { savePreferences(); }
void PetWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) retranslate();
    QWidget::changeEvent(event);
}
void PetWindow::retranslate() {
    setToolTip(tr("Click for running sessions · Hold still to pet · Drag to move, or throw · Push past a screen edge to hide\n"
                  "Right-click for controls · Esc to quit"));
    showAction_->setText(tr("Show pet"));
    showAction_->setToolTip(showAction_->isEnabled() ? QString() : tr("Hiding needs a system tray to show the pet again"));
    sessionsAction_->setText(tr("Running sessions…"));
    recapAction_->setText(tr("Today's recap"));
    muteAction_->setText(tr("Mute alerts"));
    onTopAction_->setText(tr("Always on top"));
    settingsAction_->setText(tr("Settings…"));
    moreMenu_->setTitle(tr("More"));
    statesMenu_->setTitle(tr("Preview state"));
    previewAction_->setText(tr("Animation preview…"));
    clickAction_->setText(tr("Click-through for 15 seconds"));
    recoverAction_->setText(tr("Recover pet position and input"));
    updatesItem_->setText(tr("Updates…"));
    if (updates_) updateAction_->setText(updates_->indicator());
    aboutAction_->setText(tr("About and artwork terms…"));
    quitAction_->setText(tr("Quit"));
    setAccessibleDescription(attention_ == 1 ? tr("1 session waiting for you")
                             : attention_ > 1 ? tr("%1 sessions waiting for you").arg(attention_) : QString());
    setStatus(statusSessions_, statusAttention_, statusErrors_);
    // Settings come back on the same tab.
    const auto *tabs = settingsDialog_ ? settingsDialog_->findChild<QTabWidget *>() : nullptr;
    const int tab = tabs ? tabs->currentIndex() : 0;
    rebuild(settingsDialog_, this, [this, tab]() -> QDialog * {
        if (quitting_) return nullptr;
        showSettings();
        if (auto *tabs = settingsDialog_->findChild<QTabWidget *>()) tabs->setCurrentIndex(tab);
        return settingsDialog_;
    });
    rebuild(previewDialog_, this, [this]() -> QDialog * { if (quitting_) return nullptr; showPreview(); return previewDialog_; });
    rebuild(aboutDialog_, this, [this]() -> QDialog * { if (quitting_) return nullptr; showAbout(); return aboutDialog_; });
    if (updates_) updates_->retranslate(this);
}
void PetWindow::setLanguage(const QString &language) {
    language_ = language == "en" || language == "vi" ? language : "auto";
    writePreferences([this](Preferences &preferences) { preferences.language = language_; });
    i18n::install(i18n::fromName(language_));
}
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
    const auto origin = isVisible() ? platform::nativeWindowOrigin(winId()) : std::nullopt;
    if (!origin) return pos();
    // Qt maps a window through the screen it is on, even where the window reaches past that screen.
    const auto *on = screen();
    return on ? fromNative(*origin, on->geometry().topLeft(), on->devicePixelRatio()) : *origin;
}
QPoint PetWindow::fromNative(QPoint native, QPoint screenOrigin, qreal ratio) {
    if (ratio <= 0) return native;
    return screenOrigin + (QPointF(native - screenOrigin) / ratio).toPoint();
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
    setAccessibleDescription(sessions == 1 ? tr("1 session waiting for you")
                             : sessions > 1 ? tr("%1 sessions waiting for you").arg(sessions) : QString());
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
void PetWindow::setActivityStyle(int style) {
    style = qBound(0, style, 2);
    if (style == activityStyle()) return;
    activity_.setStyle(ActivityStyle(style));
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
    if (!enabled) patience_.reset();
    if (ready_) saveTimer_.start();
}
void PetWindow::setWanderEnabled(bool enabled) {
    if (enabled == wanderEnabled_) return;
    wanderEnabled_ = enabled;
    if (!enabled && walking()) player_.play("idle", true); // Stops where it is.
    if (ready_) saveTimer_.start();
}
void PetWindow::setRecapEnabled(bool enabled) {
    if (enabled == recapEnabled_) return;
    recapEnabled_ = enabled;
    if (ready_) saveTimer_.start();
}
QString PetWindow::recapPath() const { return persist_ ? QFileInfo(store_.path()).absolutePath() + "/recap.json" : QString(); }
void PetWindow::showTrayMessage(const QString &title, const QString &text) {
    if (tray_.isVisible()) tray_.showMessage(title, text, QSystemTrayIcon::NoIcon);
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
void PetWindow::setEyeMinutes(int minutes) {
    if (minutes == wellness_.eyeMinutes() || !Wellness::validEyeMinutes(minutes)) return;
    wellness_.setEyeMinutes(minutes);
    if (ready_) saveTimer_.start();
}
void PetWindow::setWaterMinutes(int minutes) {
    if (minutes == wellness_.waterMinutes() || !Wellness::validWaterMinutes(minutes)) return;
    wellness_.setWaterMinutes(minutes);
    if (ready_) saveTimer_.start();
}
void PetWindow::showAfterFlagChange(QPoint position) {
    // Changing window flags hides the window; a hidden pet stays hidden until shown.
    if (!petHidden()) { show(); move(position); }
}
void PetWindow::recover() {
    if (quitting_) return;
    slide_.stop();
    applyPresence(presence_.setUserHidden(false));
    if (hiding() || walking()) player_.play("idle", true); // Out from behind the edge, or off a walk, to be placed in plain view.
    setClickThrough(false);
    move(Preferences::visiblePosition({-1000000, -1000000}, size(), screenAreas()));
    show(); raise();
}
void PetWindow::setTrayAvailable(bool available) {
    presence_.setTrayAvailable(available);
    showAction_->setEnabled(available);
    showAction_->setToolTip(available ? QString() : tr("Hiding needs a system tray to show the pet again"));
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
        if (walking()) player_.play("idle", true); // Nobody would see where it went.
        hide();
        player_.setPaused(true); // Nobody sees it; sessions keep driving its state.
    } else {
        const auto position = pos();
        show(); move(position); raise();
        player_.setPaused(false);
        constrainPosition();
    }
    syncBehavior();
    emit presenceChanged();
}
void PetWindow::syncBehavior() {
    stage_.update([this](behavior::Context &c) {
        c.visible = !petHidden();
        c.moving = walking() || sliding() || flying();
    });
}
void PetWindow::updatePresence(int sessions, qint64 now) {
    if (quitting_) return;
    // The idle policy may have changed from the command line since it was read.
    if (presence_.idleDeadline() && now >= presence_.idleDeadline()) refreshStartup();
    applyPresence(presence_.update(sessions, now));
}
void PetWindow::setStatus(int sessions, int attention, int errors) {
    statusSessions_ = sessions; statusAttention_ = attention; statusErrors_ = errors;
    QString text = sessions == 0 ? tr("Agent Pet — idle") : sessions == 1 ? tr("Agent Pet — 1 session")
                                 : tr("Agent Pet — %1 sessions").arg(sessions);
    if (attention > 0) text += " · " + (attention == 1 ? tr("1 needs attention") : tr("%1 need attention").arg(attention));
    if (errors > 0) text += " · " + (errors == 1 ? tr("1 tool error") : tr("%1 tool errors").arg(errors));
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
void PetWindow::setPet(const QString &id) {
    if (!Preferences::validPet(id)) return;
    pet_ = id;
    writePreferences([id](Preferences &preferences) { preferences.pet = id; });
}
PetDownloader &PetWindow::petDownloader() {
    if (!downloader_) {
        downloader_ = new PetDownloader(PetLibrary::shared(), nullptr, this);
        connect(downloader_, &PetDownloader::finished, this, [this](const QString &id, const QString &error) {
            if (error.isEmpty() && id != pet_) setPet(id);
        });
    }
    return *downloader_;
}
bool PetWindow::savePreferences() { return writePreferences([](Preferences &) {}); }
bool PetWindow::writePreferences(const std::function<void(Preferences &)> &change) {
    if (!persist_ || !ready_) return true;
    // Start from the file: `agent-pet autostart` may have changed the startup keys meanwhile.
    auto preferences = store_.load();
    preferences.size = width(); preferences.position = pos(); preferences.hasPosition = true;
    preferences.onTop = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    preferences.muted = muted_; preferences.sound = sound_; preferences.bubbles = bubbles_;
    preferences.ambient = ambientLevel(); preferences.activity = activityStyle(); preferences.mood = moodLevel(); preferences.turns = mood_.turns();
    preferences.touch = touchEnabled_; preferences.wander = wanderEnabled_; preferences.easterEggs = easterEggsEnabled(); preferences.birthday = birthday();
    preferences.eyeMinutes = wellness_.eyeMinutes(); preferences.waterMinutes = wellness_.waterMinutes();
    preferences.reminderSchedule = eggs_.reminderSchedule();
    preferences.recap = recapEnabled_;
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
                         tr("Artwork unavailable\nRight-click for controls"));
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
    beginQuit();
}
void PetWindow::beginQuit(const QString &remark) {
    if (quitting_) return;
    slide_.stop(); endDrag(); setClickThrough(false); savePreferences(); quitting_ = true;
    stopWalk();
    player_.release();
    emit quitRequested(); // Monitoring stops here; closing settings never reaches this.
    if (settingsDialog_) settingsDialog_->close();
    if (previewDialog_) previewDialog_->close();
    if (aboutDialog_) aboutDialog_->close();
    menu_.setEnabled(false);
    syncBehavior(); // A hidden pet quits at once: no one would see the closing animation.
    if (!petHidden()) {
        player_.setPaused(false);
        if (!remark.isEmpty()) quitNote_.say(remark, figure(), screenAreas());
    }
    quitTimer_.start();
    // Pestered into leaving, it complains first: the annoyed cue, a moment to read the remark, then quit-angry.
    stage_.runtime().submit({"lifecycle", "quit", remark.isEmpty() ? "quit" : "annoyed", behavior::Policy::Shutdown});
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
    patience_.petting(false, touchClock_.elapsed());
    patience_.dragging(false, touchClock_.elapsed());
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
    if (quitting_) return;
    const auto &touch = player_.touch();
    const auto fall = velocity.x() < 0 ? touch.fallLeft : touch.fallRight;
    if (touchEnabled_ && !quitting_ && !fall.isEmpty() && qHypot(velocity.x(), velocity.y()) >= touch::throwSpeed) {
        if (patience_.thrown(touchClock_.elapsed())) { beginQuit(Pet::tr("Stop throwing me! I'm leaving!")); return; }
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
    if (!hide.isEmpty() && stage_.runtime().admits(edgeIntent(hide))) { slideToEdge(edge); return; }
    constrainPosition();
}
void PetWindow::slideToEdge(touch::Edge edge) {
    if (quitting_) return;
    const auto &touch = player_.touch();
    slideTo(touch::hidePosition(edge, QRect(nativePos(), size()), screenAreas(),
                                edge == touch::Edge::Left ? touch.edgeLeftAt : touch.edgeRightAt, touch.scale), edge);
}
void PetWindow::slideTo(QPoint target, touch::Edge hide) {
    slideEdge_ = hide;
    slide_.stop();
    slide_.setStartValue(nativePos()); slide_.setEndValue(target);
    slide_.start();
}
bool PetWindow::canWander(const Move &move) const {
    // Native Wayland leaves window placement to the compositor, so the pet could not go anywhere.
    if (!wanderEnabled_ || quitting_ || petHidden() || !isVisible() || hiding() || dragging_ || flight_ || sliding()
        || QGuiApplication::platformName().startsWith("wayland"))
        return false;
    if (!move.mood.isEmpty() && move.mood != player_.mood()) return false;
    const QRect window(nativePos(), size());
    return wander::fits(move, wander::distances(window, touch::areaFor(window, screenAreas()), player_.moveScale()));
}
void PetWindow::startWalk(const Move &move) {
    walk_ = move.state; walkClock_.invalidate();
    // A climb first leaps onto its wall: the screen edge then cuts the artwork where the hands hold on.
    if (!move.wall.isEmpty())
        slideTo(touch::hidePosition(move.wall == "left" ? touch::Edge::Left : touch::Edge::Right, QRect(nativePos(), size()),
                                    screenAreas(), move.at, player_.moveScale()), touch::Edge::None);
    walkTimer_.start();
}
void PetWindow::walkStep() {
    const auto *walk = player_.move(walk_);
    if (!walk || player_.state() != walk_) { stopWalk(); return; }
    // Only the loop phase travels; the start and end play on the spot, and a paused, sliding or held pet waits.
    if (player_.paused() || player_.phase() != "loop" || sliding() || dragging_) { walkClock_.invalidate(); return; }
    if (!walkClock_.isValid()) { walkClock_.start(); walkPosition_ = pos(); return; }
    if (walkPosition_.toPoint() != pos()) walkPosition_ = pos(); // Moved by something else, such as a screen change.
    walkPosition_ += wander::step(*walk, width(), player_.moveScale(), walkClock_.restart());
    move(walkPosition_.toPoint());
    // Short of the screen edge, or of the top or bottom for a climb, it stops and plays its end.
    const QRect window(pos(), size());
    if (!wander::keeps(*walk, wander::distances(window, touch::areaFor(window, screenAreas()), player_.moveScale())))
        player_.finish();
}
void PetWindow::stopWalk() {
    walkTimer_.stop();
    const auto *walk = player_.move(walk_);
    walk_.clear();
    // A climber clings partly past the screen edge; off the wall it steps back into plain view. A drag or a
    // fall owns the position instead.
    if (walk && !walk->wall.isEmpty() && !dragging_ && !flight_ && !quitting_)
        slideTo(Preferences::visiblePosition(pos(), size(), screenAreas()), touch::Edge::None);
}
void PetWindow::land() {
    flightTimer_.stop();
    if (!flight_) return;
    flight_.reset();
    player_.release(); // Plays the landing, then whatever sessions asked for meanwhile.
    constrainPosition();
}
void PetWindow::entered(const QString &state) {
    if (walking() && state != walk_) stopWalk();
    if (const auto *move = player_.move(state); move && !walking() && !dragging_ && !flight_) startWalk(*move);
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
    // "Updates…" always sits under More; a waiting update also shows at the top level.
    const auto refresh = [this] {
        const auto text = updates_ ? updates_->indicator() : QString();
        updatesItem_->setVisible(updates_);
        updateAction_->setText(text); updateAction_->setVisible(updates_ && updates_->waiting());
    };
    refresh();
    if (controller) connect(controller, &updates::Controller::changed, this, refresh);
}
void PetWindow::showSettings() {
    if (settingsDialog_) { settingsDialog_->show(); settingsDialog_->raise(); return; }
    auto *dialog = new QDialog(this); settingsDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("Agent Pet settings"));
    // Three short tabs instead of one long form; status and Close/Quit stay below them.
    auto *outer = new QVBoxLayout(dialog);
    auto *tabs = new QTabWidget(dialog); outer->addWidget(tabs);
    const auto page = [tabs](const QString &title) {
        auto *widget = new QWidget(tabs); tabs->addTab(widget, title);
        return new QFormLayout(widget);
    };
    auto *layout = page(tr("General"));
    // Each language is named in itself, so it can be found whatever the current one is.
    auto *language = new QComboBox(dialog); language->setAccessibleName(tr("Language"));
    language->addItem(tr("Automatic (system)"), "auto");
    language->addItem("English", "en");
    language->addItem("Tiếng Việt", "vi");
    language->setCurrentIndex(qMax(0, language->findData(language_)));
    layout->addRow(tr("&Language"), language);
    connect(language, &QComboBox::currentIndexChanged, this, [this, language] { setLanguage(language->currentData().toString()); });
    auto *size = new QSpinBox(dialog); size->setRange(160, 320); size->setSingleStep(20); size->setSuffix(" px"); size->setValue(width());
    size->setAccessibleName(tr("Pet size")); layout->addRow(tr("&Size"), size);
    connect(size, &QSpinBox::valueChanged, this, &PetWindow::setPetSize);
    auto *top = new QCheckBox(tr("Always on &top"), dialog); top->setChecked(onTopAction_->isChecked()); layout->addRow(top);
    connect(top, &QCheckBox::toggled, this, &PetWindow::setOnTop);
    connect(onTopAction_, &QAction::toggled, top, &QCheckBox::setChecked);
    auto *mute = new QCheckBox(tr("&Mute alert bubbles (badge stays visible)"), dialog); mute->setChecked(muted_); layout->addRow(tr("Alerts"), mute);
    connect(mute, &QCheckBox::toggled, this, &PetWindow::setMuted);
    connect(muteAction_, &QAction::toggled, mute, &QCheckBox::setChecked);
    auto *sound = new QCheckBox(tr("Play a &sound for new alerts"), dialog); sound->setChecked(sound_); layout->addRow(sound);
    connect(sound, &QCheckBox::toggled, this, &PetWindow::setSound);
    auto *bubbles = new QComboBox(dialog);
    bubbles->addItems({tr("Only when a session needs me"), tr("When a session needs me or a tool fails"), tr("Also when a turn finishes")});
    bubbles->setCurrentIndex(bubbles_); bubbles->setAccessibleName(tr("Show alert bubbles"));
    layout->addRow(tr("Show &bubbles"), bubbles);
    connect(bubbles, &QComboBox::currentIndexChanged, this, &PetWindow::setBubbles);
    auto *reminders = reminderSettings(dialog);
    layout->addRow(reminders);
    layout = page(tr("Pet"));
    // Shown once there is a choice; the chosen pet appears on the next start.
    if (const auto pets = PetLibrary::shared().pets(); pets.size() > 1) {
        auto *picker = new PetPicker(pets, pet_, PetLibrary::shared().active(), dialog);
        layout->addRow(tr("&Character"), picker);
        connect(picker, &PetPicker::chosen, this, &PetWindow::setPet);
        // Pets that are not bundled download first; the download outlives the dialog.
        auto &downloads = petDownloader();
        connect(picker, &PetPicker::downloadRequested, &downloads, &PetDownloader::start);
        connect(picker, &PetPicker::cancelRequested, &downloads, &PetDownloader::cancel);
        connect(&downloads, &PetDownloader::progressed, picker, &PetPicker::downloading);
        connect(&downloads, &PetDownloader::finished, picker, &PetPicker::downloaded);
        if (!downloads.pet().isEmpty()) picker->downloading(downloads.pet(), downloads.done(), downloads.total());
    }
    auto *ambient = new QComboBox(dialog);
    ambient->addItems({tr("Off (no fidgets or alternate idle loops)"), tr("Subtle (a fidget about once a minute)"), tr("Lively (a fidget every 15–25 seconds)")});
    ambient->setCurrentIndex(ambientLevel()); ambient->setAccessibleName(tr("Idle animation"));
    ambient->setToolTip(tr("What the pet does on its own while no agent needs it: fidgets, alternate idle loops, wandering,\n"
                        "and dozing off after about ten quiet minutes. Any agent activity ends it at once."));
    layout->addRow(tr("&Idle animation"), ambient);
    connect(ambient, &QComboBox::currentIndexChanged, this, &PetWindow::setAmbientLevel);
    auto *activity = new QComboBox(dialog);
    activity->addItems({tr("Classic (one loop per activity, as before)"),
                        tr("Subtle (calm variations; stays at the desk for short thinking pauses)"),
                        tr("Playful (also pen spinning and small reactions)")});
    activity->setCurrentIndex(activityStyle()); activity->setAccessibleName(tr("Active animation"));
    activity->setToolTip(tr("How the pet thinks, reads and works while an agent is busy: alternate loops now and then,\n"
                         "staying at its desk through short thinking pauses, and (Playful) small reactions.\n"
                         "Independent of Idle animation. Requests, errors and finished turns still show at once:\n"
                         "it never delays alerts."));
    layout->addRow(tr("&Active animation"), activity);
    connect(activity, &QComboBox::currentIndexChanged, this, &PetWindow::setActivityStyle);
    auto *wander = new QCheckBox(tr("Walk, crawl and climb along the screen after a while"), dialog);
    wander->setChecked(wanderEnabled_); wander->setAccessibleName(tr("Wandering"));
    wander->setToolTip(tr("After about four quiet minutes the idle pet sometimes walks or crawls along the screen, and\n"
                       "climbs up or down a screen edge it has reached, then steps back into view. It goes with the\n"
                       "idle animation, so Off above keeps it still too. Native Wayland sessions cannot move it.\n"
                       "Off, the pet stays where you put it."));
    layout->addRow(tr("&Wander"), wander);
    connect(wander, &QCheckBox::toggled, this, &PetWindow::setWanderEnabled);
    auto *mood = new QComboBox(dialog);
    mood->addItems({tr("Off (always neutral)"), tr("Cheerful only (happy after a run of finished turns)"),
                    tr("Full (also droopy after repeated tool errors)")});
    mood->setCurrentIndex(moodLevel()); mood->setAccessibleName(tr("Mood"));
    mood->setToolTip(tr("Whether finished turns and tool errors change how the idle pet looks. The mood fades\n"
                     "back to neutral over a few quiet minutes."));
    layout->addRow(tr("M&ood"), mood);
    connect(mood, &QComboBox::currentIndexChanged, this, &PetWindow::setMoodLevel);
    auto *touch = new QCheckBox(tr("React to &petting, throwing and screen edges"), dialog);
    touch->setChecked(touchEnabled_); touch->setAccessibleName(tr("Touch reactions"));
    touch->setToolTip(tr("Hold the pet still on its head, cheek or body to pet it. Let go while dragging fast and it\n"
                      "falls to the bottom of the screen. Push it past the left or right edge while it idles and it\n"
                      "hides there until an agent needs it. Off, the pet is only dragged."));
    layout->addRow(tr("&Touch"), touch);
    connect(touch, &QCheckBox::toggled, this, &PetWindow::setTouchEnabled);
    auto *eggs = new QCheckBox(tr("Special days, late nights and surprises"), dialog);
    eggs->setChecked(easterEggsEnabled()); eggs->setAccessibleName(tr("Easter eggs"));
    eggs->setToolTip(tr("Small surprises: a greeting on May 20 and on your birthday, extra yawns late at night, a\n"
                     "dance for turns finished on a Friday evening, a bigger celebration for a very long turn,\n"
                     "and a startled jump when an agent runs a destructive command. Some are left to be found."));
    layout->addRow(tr("&Easter eggs"), eggs);
    connect(eggs, &QCheckBox::toggled, this, &PetWindow::setEasterEggsEnabled);
    auto *birthdayRow = new QWidget(dialog); auto *birthdayLayout = new QHBoxLayout(birthdayRow);
    birthdayLayout->setContentsMargins(0, 0, 0, 0);
    auto *hasBirthday = new QCheckBox(tr("Celebrate on"), birthdayRow); hasBirthday->setAccessibleName(tr("Birthday"));
    auto *birthdayDate = new QDateEdit(birthdayRow); birthdayDate->setAccessibleName(tr("Birthday date"));
    // The year is never shown or kept; a leap year lets February 29 be picked.
    // Month names follow the pet's language, not the system's; Vietnamese puts the day first ("1 tháng 1").
    const bool vietnamese = i18n::installed() == i18n::Language::Vietnamese;
    birthdayDate->setLocale(QLocale(vietnamese ? QLocale::Vietnamese : QLocale::English));
    birthdayDate->setDisplayFormat(vietnamese ? "d MMMM" : "MMMM d"); birthdayDate->setDateRange(QDate(2000, 1, 1), QDate(2000, 12, 31));
    const auto saved = QDate::fromString("2000-" + birthday(), "yyyy-MM-dd");
    hasBirthday->setChecked(saved.isValid()); birthdayDate->setDate(saved.isValid() ? saved : QDate(2000, 1, 1));
    birthdayDate->setEnabled(saved.isValid());
    birthdayLayout->addWidget(hasBirthday); birthdayLayout->addWidget(birthdayDate, 1);
    layout->addRow(tr("Birthday"), birthdayRow);
    const auto applyBirthday = [this, hasBirthday, birthdayDate] {
        birthdayDate->setEnabled(hasBirthday->isChecked());
        setBirthday(hasBirthday->isChecked() ? birthdayDate->date().toString("MM-dd") : QString());
    };
    connect(hasBirthday, &QCheckBox::toggled, this, applyBirthday);
    connect(birthdayDate, &QDateEdit::dateChanged, this, applyBirthday);
    auto *clockLayout = qobject_cast<QFormLayout*>(reminders->layout());
    const auto clockTime = [this, dialog, clockLayout](const QString &label, QTime ReminderSchedule::*field, const QString &name) {
        auto *time = new QTimeEdit(eggs_.reminderSchedule().*field, dialog);
        time->setDisplayFormat("HH:mm"); time->setAccessibleName(label); time->setObjectName(name);
        clockLayout->addRow(label, time);
        // Commit a complete edit: intermediate hour/minute values must not consume today's reminder.
        const auto applyTime = [this, time, field] {
            auto schedule = eggs_.reminderSchedule(); schedule.*field = time->time();
            eggs_.setReminderSchedule(schedule); saveTimer_.start();
        };
        connect(time, &QTimeEdit::editingFinished, this, applyTime);
        connect(dialog, &QDialog::finished, this, applyTime);
    };
    clockTime(tr("Monday reminder"), &ReminderSchedule::monday, "mondayTime");
    clockTime(tr("Lunch reminder"), &ReminderSchedule::lunch, "lunchTime");
    clockTime(tr("Go-home reminder"), &ReminderSchedule::leaveWork, "leaveWorkTime");
    clockTime(tr("Sleep reminder"), &ReminderSchedule::sleep, "sleepTime");
    auto *recap = new QCheckBox(tr("Add today's recap to the &go-home reminder"), dialog);
    recap->setChecked(recapEnabled_); recap->setAccessibleName(tr("Daily recap"));
    recap->setToolTip(tr("At the go-home reminder time on weekdays the pet sums up the day's agent work: finished turns, projects,\n"
                      "errors, approvals that waited and the longest run. Needs easter eggs on. Today's recap\n"
                      "in the menu shows it any time."));
    layout->addRow(tr("Re&cap"), recap);
    connect(recap, &QCheckBox::toggled, this, &PetWindow::setRecapEnabled);
    layout = page(tr("Startup and agents"));
    layout->addRow(startupSettings(dialog));
    layout->addRow(integrationSettings(dialog));
    auto *status = new QLabel(dialog); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const auto updateStatus = [this, status] {
        status->setText(store_.error().isEmpty() ? tr("Preferences are saved automatically. Closing this window keeps monitoring; Quit stops it.")
                                               : store_.error() + "\n" + store_.path());
    };
    updateStatus(); outer->addWidget(status);
    auto *statusTimer = new QTimer(dialog); statusTimer->setInterval(1000);
    connect(statusTimer, &QTimer::timeout, dialog, updateStatus); statusTimer->start();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    // Recover and the artwork credits live in the menu under More.
    if (updates_) {
        auto *updatesButton = buttons->addButton(updates_->indicator(), QDialogButtonBox::ActionRole);
        connect(updatesButton, &QPushButton::clicked, this, [this] { updates_->showSettings(this); });
        connect(updates_, &updates::Controller::changed, updatesButton, [this, updatesButton] { updatesButton->setText(updates_->indicator()); });
    }
    auto *quit = buttons->addButton(tr("&Quit Agent Pet"), QDialogButtonBox::DestructiveRole);
    connect(quit, &QPushButton::clicked, this, &PetWindow::requestQuit);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    outer->addWidget(buttons); dialog->show();
}
QWidget *PetWindow::reminderSettings(QWidget *parent) {
    auto *box = new QGroupBox(tr("Reminders"), parent); box->setObjectName("reminders");
    box->setToolTip(tr("While you are active (agent activity or moving the pointer), the pet now and then reminds you\n"
                    "to rest your eyes and to drink some water. It waits while an alert or approval is waiting, while\n"
                    "alerts are muted and from 22:00 to 06:00. Five minutes away counts as a break and starts both over.\n"
                    "Click a reminder to say you did it."));
    auto *layout = new QFormLayout(box);
    const auto choices = [box](const auto &minutes, int current, const QString &name) {
        auto *combo = new QComboBox(box); combo->setAccessibleName(name);
        for (const int value : minutes) combo->addItem(value ? tr("Every %1 minutes").arg(value) : tr("Off"), value);
        combo->setCurrentIndex(qMax(0, combo->findData(current)));
        return combo;
    };
    auto *eyes = choices(Wellness::eyeChoices, wellness_.eyeMinutes(), tr("Eye break reminder"));
    eyes->setToolTip(tr("The 20-20-20 rule: look at something about 20 feet (6 m) away for 20 seconds."));
    layout->addRow(tr("E&ye break"), eyes);
    connect(eyes, &QComboBox::currentIndexChanged, this, [this, eyes] { setEyeMinutes(eyes->currentData().toInt()); });
    auto *water = choices(Wellness::waterChoices, wellness_.waterMinutes(), tr("Water reminder"));
    layout->addRow(tr("W&ater"), water);
    connect(water, &QComboBox::currentIndexChanged, this, [this, water] { setWaterMinutes(water->currentData().toInt()); });
    return box;
}
QWidget *PetWindow::startupSettings(QWidget *parent) {
    refreshStartup();
    auto *box = new QGroupBox(tr("Startup"), parent); box->setObjectName("startup");
    auto *layout = new QFormLayout(box);
    auto *autostart = new QCheckBox(tr("&Autostart on agent session start"), box);
    autostart->setChecked(autostart_);
    autostart->setToolTip(tr("When a connected agent starts a session and no pet is running, its hook launches the pet.\n"
                          "Needs a graphical session; SSH and container sessions do not start it."));
    layout->addRow(autostart);
    connect(autostart, &QCheckBox::toggled, this, &PetWindow::setAutostart);
    auto *login = new QCheckBox(tr("Start Agent Pet at &login"), box);
    login->setChecked(loginStartEnabled());
    login->setToolTip(tr("Starts the pet whenever you log in to your desktop, even before an agent session."));
    layout->addRow(login);
    connect(login, &QCheckBox::toggled, this, [login](bool enabled) {
        QString error;
        if (setLoginStart(enabled, QCoreApplication::applicationFilePath(), &error)) return;
        QSignalBlocker blocker(login); login->setChecked(!enabled);
        warn(login, "Agent Pet", tr("Cannot change start at login: %1").arg(error));
    });
    auto *idle = new QComboBox(box); idle->setAccessibleName(tr("When no sessions remain"));
    idle->addItem(tr("Keep the pet running"), int(IdlePolicy::Keep));
    idle->addItem(tr("Hide the pet (tray icon stays)"), int(IdlePolicy::Hide));
    idle->addItem(tr("Quit Agent Pet"), int(IdlePolicy::Quit));
    if (auto *model = qobject_cast<QStandardItemModel *>(idle->model()); model && !presence_.canHide()) {
        model->item(1)->setEnabled(false); // Without a tray nothing could show it again; it keeps running.
        model->item(1)->setToolTip(tr("Needs a system tray"));
    }
    idle->setCurrentIndex(qMax(0, idle->findData(int(whenIdle()))));
    layout->addRow(tr("When no sessions &remain"), idle);
    connect(idle, &QComboBox::currentIndexChanged, this, [this, idle] { setWhenIdle(IdlePolicy(idle->currentData().toInt())); });
    auto *note = new QLabel(tr("Applies two minutes after the last session ends; a new session cancels it. "
                            "A pet hidden this way returns with the next session."), box);
    note->setWordWrap(true); layout->addRow(note);
    return box;
}
QWidget *PetWindow::integrationSettings(QWidget *parent) {
    auto *box = new QGroupBox(tr("Agent integrations"), parent); box->setObjectName("integrations");
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
            QString text = owned == expected ? tr("Enabled") : owned == 0 ? tr("Not enabled") : tr("Partial (%1 of %2 hooks)").arg(owned).arg(expected);
            if (report.contains("warning")) text += " · " + report["warning"].toString();
            status->setText(text + "\n" + report["config"].toString());
            status->setToolTip(report["setup"].toString());
            // A partial install, such as one from before a hook was added, is completed rather than removed.
            toggle->setText(owned == expected ? tr("Disable") : owned ? tr("Update") : tr("Enable")); toggle->setEnabled(true);
            toggle->setProperty("enable", owned != expected);
        };
        refresh();
        connect(toggle, &QPushButton::clicked, box, [this, provider, toggle, refresh] {
            const bool enable = toggle->property("enable").toBool();
            const auto executable = QCoreApplication::applicationFilePath();
            const auto question = enable
                ? tr("Add Agent Pet hooks to %1?\n\nHook command: %2\nOther settings and hooks are preserved. "
                          "Register from a permanent install location, then restart the client.").arg(integrationConfigPath(provider), hookExecutable(executable))
                : tr("Remove only Agent Pet hooks from %1?").arg(integrationConfigPath(provider));
            // Own buttons: Qt has no Vietnamese for its standard Yes and No.
            QMessageBox ask(QMessageBox::Question, tr("Agent integrations"), question, QMessageBox::NoButton, this);
            auto *confirm = ask.addButton(enable ? tr("Add hooks") : tr("Remove hooks"), QMessageBox::AcceptRole);
            ask.addButton(tr("Cancel"), QMessageBox::RejectRole);
            if (ask.exec(), ask.clickedButton() != confirm) return;
            QJsonObject report; QString error;
            if (!runIntegration(enable ? "enable" : "disable", provider, {}, executable, report, error))
                warn(this, tr("Agent integrations"), error);
            refresh();
        });
        layout->addRow(provider == "claude" ? "Claude Code" : "Codex", row);
    }
    auto *coverage = new QLabel(tr("Covers sessions on this machine that send events after setup. Restart the client "
                                "after enabling; silent, remote and container sessions are not discovered. "
                                "Reply to requests in the agent's own terminal or editor."), box);
    coverage->setWordWrap(true); layout->addRow(coverage);
    return box;
}
void PetWindow::showAbout() {
    if (aboutDialog_) { aboutDialog_->show(); aboutDialog_->raise(); return; }
    auto *dialog = new QDialog(this); aboutDialog_ = dialog; dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("About Agent Pet — artwork and terms")); dialog->resize(560, 440);
    auto *layout = new QVBoxLayout(dialog);
    // The running pet's credit and terms; the application's notices are the same for every pet.
    const auto &library = PetLibrary::shared();
    const auto artwork = library.info(library.active());
    QString credit = tr("<b>Agent Pet %1</b> (revision %2, Qt %3)<br>Application code: Apache License 2.0.")
                         .arg(QString(AGENT_PET_VERSION).toHtmlEscaped(), QString(AGENT_PET_REVISION).toHtmlEscaped(), qVersion());
    if (!artwork.id.isEmpty() && artwork.url.isEmpty())
        credit += " " + tr("Artwork: %1, under its own terms below.").arg(artwork.author.toHtmlEscaped());
    else if (!artwork.id.isEmpty())
        credit += " " + tr("Artwork: %1, via %2, under its own terms below.")
                            .arg(artwork.author.toHtmlEscaped(), QString("<a href=\"%1\">%2</a>")
                                 .arg(artwork.url.toHtmlEscaped(), artwork.url.mid(8).toHtmlEscaped()));
    auto *credits = new QLabel(credit, dialog);
    credits->setOpenExternalLinks(true); credits->setTextInteractionFlags(Qt::TextBrowserInteraction); layout->addWidget(credits);
    auto *terms = new QTextBrowser(dialog); terms->setAccessibleName(tr("Artwork terms and third-party notices"));
    auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) + "\n\n" : QString();
    };
    auto artworkTerms = artwork.terms.isEmpty() ? QString() : read(":/licenses/" + artwork.terms);
    if (artworkTerms.isEmpty()) artworkTerms = tr("Artwork terms unavailable.") + "\n\n";
    terms->setPlainText(read(":/NOTICE") + read(":/THIRD_PARTY_NOTICES.md") + artworkTerms + read(":/LICENSE"));
    layout->addWidget(terms);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); layout->addWidget(buttons); dialog->show();
}
void PetWindow::showPreview() {
    if (previewDialog_) { previewDialog_->show(); previewDialog_->raise(); return; }
    auto *dialog = new QDialog(this); previewDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("Animation preview")); dialog->resize(540, 420);
    auto *layout = new QVBoxLayout(dialog);
    auto *states = new QComboBox(dialog); states->addItems(player_.states()); states->setCurrentText(player_.state());
    states->setAccessibleName(tr("Animation state")); layout->addWidget(states);
    auto *interrupt = new QCheckBox(tr("Interrupt immediately (skip outgoing end)"), dialog); layout->addWidget(interrupt);
    auto *play = new QPushButton(tr("&Play selected state"), dialog); layout->addWidget(play);
    connect(play, &QPushButton::clicked, dialog, [this, states, interrupt] { player_.select(states->currentText(), interrupt->isChecked()); });
    auto *pause = new QCheckBox(tr("&Pause"), dialog); pause->setChecked(player_.paused()); layout->addWidget(pause);
    connect(pause, &QCheckBox::toggled, &player_, &Player::setPaused);
    auto *step = new QPushButton(tr("Step one &frame"), dialog); layout->addWidget(step);
    connect(step, &QPushButton::clicked, dialog, [this, pause] { pause->setChecked(true); player_.advance(); });
    auto *detail = new QLabel(dialog); detail->setWordWrap(true); detail->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(detail);
    auto *log = new QPlainTextEdit(dialog); log->setReadOnly(true); log->setMaximumBlockCount(100); log->setAccessibleName(tr("Transition history")); layout->addWidget(log);
    const auto refresh = [this, detail, log] {
        const QString transition = player_.state() + " / " + player_.phase() + " / " + player_.sequence();
        if (log->property("transition").toString() != transition) { log->appendPlainText(transition); log->setProperty("transition", transition); }
        detail->setText(QString("%1\nRequested: %2 · frame %3/%4 · %5 ms\nCache: %6 / %7 KiB\n%8") // Developer detail: English.
            .arg(transition, player_.requestedState()).arg(player_.frameIndex() + 1).arg(player_.frameCount())
            .arg(player_.frameDuration()).arg(player_.cacheKiB()).arg(Player::cacheLimitKiB).arg(player_.error()));
    };
    connect(&player_, &Player::changed, dialog, refresh);
    connect(&player_, &Player::failed, dialog, [refresh](const QString &) { refresh(); });
    connect(dialog, &QDialog::finished, this, [this] { player_.setPaused(false); });
    refresh(); dialog->show();
}
}
