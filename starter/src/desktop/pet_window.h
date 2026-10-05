#pragma once
#include "animation/ambient.h"
#include "animation/mood.h"
#include "animation/player.h"
#include "settings/preferences.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWidget>
#include <functional>

namespace pet {
namespace updates { class Controller; }
class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr, const QString &preferencesPath = {}, bool persist = true);
    ~PetWindow() override;
    Player &player() { return player_; }
    Ambient &ambient() { return ambient_; }
    void setAmbientLevel(int level); // Preferences::Ambient; persisted.
    int ambientLevel() const { return int(ambient_.level()); }
    Mood &mood() { return mood_; }
    void setMoodLevel(int level); // Preferences::Mood; persisted.
    int moodLevel() const { return int(mood_.setting()); }
    void setPetSize(int pixels);
    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }
    void recover();
    void setOnTop(bool enabled);
    void showSettings();
    void setUpdates(updates::Controller *controller);
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
    // Hidden: the window is gone but monitoring, alerts and the tray icon keep running.
    bool petHidden() const { return presence_.hidden(); }
    void setPetHidden(bool hidden); // Tray click or "Show pet"; ignored without a tray.
    void setTrayAvailable(bool available); // Detected at startup; replaceable for tests.
    Presence &presence() { return presence_; }
    void updatePresence(int sessions, qint64 now);
    // Tray tooltip and icon dot: top-level sessions, sessions needing the user, recent tool errors.
    void setStatus(int sessions, int attention, int errors);
    QString statusText() const { return tray_.toolTip(); }
    bool trayAlert() const { return trayAttention_ > 0 || trayError_; }
    void setAutostart(bool enabled);
    bool autostart() const { return autostart_; }
    void setWhenIdle(IdlePolicy policy);
    IdlePolicy whenIdle() const { return presence_.policy(); }
    QVector<QRect> screenAreas() const;
    // Global rectangle around the character itself, excluding the sprite's transparent margins.
    QRect figure() const;
    QPoint nativePos() const; // Position as the display server reports it.
signals:
    void moved();
    void notificationsChanged();
    void quitRequested();
    void presenceChanged(); // The pet was hidden or shown.
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
    void applyPresence(Presence::Action action);
    void showAfterFlagChange(QPoint position);
    // Reads the startup keys, which `agent-pet autostart` may change while the pet runs.
    void refreshStartup();
    bool writePreferences(const std::function<void(Preferences &)> &change);
    void updateTrayIcon();
    void showAbout();
    void watchScreen(QScreen *screen);
    QWidget *integrationSettings(QWidget *parent);
    QWidget *startupSettings(QWidget *parent);
    updates::Controller *updates_ = nullptr;
    Player player_;
    Ambient ambient_;
    Mood mood_;
    PreferencesStore store_;
    QMenu menu_;
    QSystemTrayIcon tray_;
    QTimer recoveryTimer_, dragTimer_, saveTimer_;
    QPointer<QDialog> settingsDialog_, previewDialog_;
    QAction *clickAction_ = nullptr, *onTopAction_ = nullptr, *muteAction_ = nullptr, *showAction_ = nullptr;
    Presence presence_;
    QPixmap trayBase_;
    int trayAttention_ = 0; // Badge shown on the tray icon; -1 forces a redraw.
    bool trayError_ = false;
    QPoint dragOffset_;
    bool fallbackDrag_ = false, dragging_ = false, clickThrough_ = false;
    bool persist_ = true, ready_ = false, quitting_ = false, muted_ = false, sound_ = false, autostart_ = false;
    int attention_ = 0, bubbles_ = Preferences::RequestsAndErrors;
    QPoint pressPosition_;
    QElapsedTimer pressTimer_;
};
}
