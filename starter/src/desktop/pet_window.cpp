#include "pet_window.h"
#include "drag_monitor.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
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
    connect(&player_, &Player::changed, this, qOverload<>(&PetWindow::update));
    connect(&player_, &Player::completed, this, [this](const QString &state) {
        if (quitting_ && state == "closing") qApp->quit();
    });
    auto *states = menu_.addMenu("Preview state");
    for (const auto &state : player_.states())
        states->addAction(state, this, [this, state] { if (!quitting_) player_.select(state); });
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
    return store_.save(preferences);
}
void PetWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (ready_) saveTimer_.start();
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
}
void PetWindow::requestQuit() {
    if (quitting_) { qApp->quit(); return; }
    endDrag(); setClickThrough(false); savePreferences(); quitting_ = true;
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
    auto *recover = new QPushButton("&Recover position and input", dialog); layout->addRow(recover);
    connect(recover, &QPushButton::clicked, this, &PetWindow::recover);
    auto *about = new QPushButton("Artwork &credits and terms", dialog); layout->addRow(about);
    connect(about, &QPushButton::clicked, this, &PetWindow::showAbout);
    auto *status = new QLabel(dialog); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const auto updateStatus = [this, status] {
        status->setText(store_.error().isEmpty() ? "Size and position are saved automatically. Agent integrations are planned."
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
void PetWindow::showAbout() {
    auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("About Agent Pet — artwork and terms"); dialog->resize(560, 440);
    auto *layout = new QVBoxLayout(dialog);
    auto *credits = new QLabel("<b>Agent Pet 0.2.0</b><br>Artwork: VUP-Simulator team, via "
                               "<a href='https://github.com/LorisYounger/VPet'>LorisYounger/VPet</a>.", dialog);
    credits->setOpenExternalLinks(true); credits->setTextInteractionFlags(Qt::TextBrowserInteraction); layout->addWidget(credits);
    auto *terms = new QTextBrowser(dialog); terms->setAccessibleName("Artwork terms and third-party notices");
    QString text;
    for (const auto &path : {":/THIRD_PARTY_NOTICES.md", ":/licenses/VPET-ARTWORK-TERMS.md"}) {
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
