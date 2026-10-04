#pragma once
#include "animation/player.h"
#include "settings/preferences.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWidget>

namespace pet {
class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr, const QString &preferencesPath = {}, bool persist = true);
    ~PetWindow() override;
    Player &player() { return player_; }
    void setPetSize(int pixels);
    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }
    void recover();
    void setOnTop(bool enabled);
    void showSettings();
    void showPreview();
    void requestQuit();
    void constrainPosition();
    bool savePreferences();
    // Unresolved observed requests; independent of whether their alerts were dismissed.
    void setAttention(int sessions);
    int attention() const { return attention_; }
    void setMuted(bool muted);
    bool muted() const { return muted_; }
    void setSound(bool enabled);
    bool sound() const { return sound_; }
    void setBubbles(int level);
    int bubbles() const { return bubbles_; }
    bool quitting() const { return quitting_; }
    QVector<QRect> screenAreas() const;
    // Global rectangle around the character itself, excluding the sprite's transparent margins.
    QRect figure() const;
    QPoint nativePos() const; // Position as the display server reports it.
signals:
    void moved();
    void notificationsChanged();
    void quitRequested();
    void sessionsRequested(); // A click on the pet (press and release without moving it), or the menu.
protected:
    bool event(QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void endDrag(bool released = false);
    void showAbout();
    void watchScreen(QScreen *screen);
    QWidget *integrationSettings(QWidget *parent);
    Player player_;
    PreferencesStore store_;
    QMenu menu_;
    QSystemTrayIcon tray_;
    QTimer recoveryTimer_, dragTimer_, saveTimer_;
    QPointer<QDialog> settingsDialog_, previewDialog_;
    QAction *clickAction_ = nullptr, *onTopAction_ = nullptr, *muteAction_ = nullptr;
    QPoint dragOffset_;
    bool fallbackDrag_ = false, dragging_ = false, clickThrough_ = false;
    bool persist_ = true, ready_ = false, quitting_ = false, muted_ = false, sound_ = false;
    int attention_ = 0, bubbles_ = Preferences::RequestsAndErrors;
    QPoint pressPosition_;
    QElapsedTimer pressTimer_;
};
}
