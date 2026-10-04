#include "pet_window.h"
#include "settings/preferences.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>
#include <QShortcut>
#include <QWindow>

namespace pet {
PetWindow::PetWindow(QWidget *parent) : QWidget(parent), player_(this), menu_(this), tray_(this) {
    setWindowTitle("Agent Pet — M1 prototype");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_QuitOnClose);
    setAccessibleName("Agent Pet");
    setToolTip("Drag to move · Right-click for controls · Esc to quit");
    setPetSize(Preferences::defaultSize);
    connect(&player_, &Player::changed, this, qOverload<>(&PetWindow::update));
    menu_.addAction("Idle", this, [this] { player_.select("idle"); });
    menu_.addAction("Thinking", this, [this] { player_.select("thinking"); });
    auto *sizes = menu_.addMenu("Size");
    for (int pixels : {160, 240, 320})
        sizes->addAction(QString::number(pixels) + " px", this, [this, pixels] { setPetSize(pixels); });
    auto *onTop = menu_.addAction("Always on top");
    onTop->setCheckable(true);
    onTop->setChecked(true);
    connect(onTop, &QAction::toggled, this, &PetWindow::setOnTop);
    clickAction_ = menu_.addAction("Click-through for 15 seconds");
    clickAction_->setCheckable(true);
    connect(clickAction_, &QAction::toggled, this, &PetWindow::setClickThrough);
    menu_.addAction("Recover pet position and input", this, &PetWindow::recover);
    menu_.addSeparator();
    menu_.addAction("About Agent Pet", this, [this] {
        QMessageBox box(this);
        box.setWindowTitle("About Agent Pet");
        box.setTextFormat(Qt::RichText);
        box.setText("<b>Agent Pet 0.1.0 — desktop prototype</b><br>"
                    "Idle and thinking preview. Agent integrations are planned.<br><br>"
                    "Artwork: VUP-Simulator team, via "
                    "<a href='https://github.com/LorisYounger/VPet'>LorisYounger/VPet</a>.<br>"
                    "Artwork terms and third-party notices accompany this application.");
        box.exec();
    });
    menu_.addAction("Quit", qApp, &QApplication::quit);
    tray_.setIcon(QIcon(player_.pixmap()));
    tray_.setToolTip("Agent Pet — right-click for controls");
    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) recover();
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_.show();
    recoveryTimer_.setSingleShot(true);
    connect(&recoveryTimer_, &QTimer::timeout, this, [this] { setClickThrough(false); });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [] { qApp->quit(); });
    new QShortcut(QKeySequence(Qt::Key_Space), this, [this] {
        player_.select(player_.state() == "idle" ? "thinking" : "idle");
    });
    new QShortcut(QKeySequence(Qt::Key_Menu), this, [this] { menu_.popup(mapToGlobal(rect().center())); });
    recover();
}
void PetWindow::setPetSize(int pixels) { setFixedSize(qBound(160, pixels, 320), qBound(160, pixels, 320)); }
void PetWindow::setClickThrough(bool enabled) {
    clickThrough_ = enabled;
    const QSignalBlocker blocker(clickAction_);
    clickAction_->setChecked(enabled);
    setWindowFlag(Qt::WindowTransparentForInput, enabled);
    show();
    if (enabled) recoveryTimer_.start(Preferences::recoveryMs);
    else recoveryTimer_.stop();
}
void PetWindow::setOnTop(bool enabled) {
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    show();
}
void PetWindow::recover() {
    setClickThrough(false);
    const QRect area = QApplication::primaryScreen()->availableGeometry();
    move(area.right() - width() - 24, area.bottom() - height() - 24);
    show();
    raise();
}
void PetWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto size = player_.pixmap().size().scaled(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    painter.drawPixmap(target, player_.pixmap());
}
void PetWindow::closeEvent(QCloseEvent *) { qApp->quit(); }
void PetWindow::contextMenuEvent(QContextMenuEvent *event) { menu_.popup(event->globalPos()); }
void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    // TODO(M2): Import the original Raise dragging sequences, play them during
    // dragging, and restore the previous state when the native move ends.
    dragOffset_ = event->globalPosition().toPoint() - pos();
    // Native compositor movement supports Wayland; X11 can use the fallback.
    dragging_ = !windowHandle()->startSystemMove();
}
void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton))
        move(event->globalPosition().toPoint() - dragOffset_);
}
void PetWindow::mouseReleaseEvent(QMouseEvent *) { dragging_ = false; }
}
