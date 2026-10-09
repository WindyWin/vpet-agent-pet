#pragma once
#include "animation/activity.h"
#include "animation/ambient.h"
#include "animation/easter_eggs.h"
#include "animation/mood.h"
#include "animation/player.h"
#include "animation/stage.h"
#include "desktop/touch.h"
#include "desktop/alert_bubble.h"
#include "desktop/wander.h"
#include "desktop/snooze.h"
#include "desktop/wellness.h"
#include "settings/preferences.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>
#include <optional>

namespace pet {
namespace updates { class Controller; }
class PetDownloader;
class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr, const QString &preferencesPath = {}, bool persist = true);
    ~PetWindow() override;
    Player &player() { return player_; }
    // The behavior runtime and how it shows on the player: every behavior's way onto the pet.
    Stage &stage() { return stage_; }
    // Tells the runtime what the window knows: whether the pet is in view and whether it is moving.
    void syncBehavior();
    Ambient &ambient() { return ambient_; }
    void setAmbientLevel(int level); // Preferences::Ambient; persisted.
    int ambientLevel() const { return int(ambient_.level()); }
    Activity &activity() { return activity_; }
    void setActivityStyle(int style); // Preferences::Activity; persisted.
    int activityStyle() const { return int(activity_.style()); }
    Mood &mood() { return mood_; }
    void setMoodLevel(int level); // Preferences::Mood; persisted.
    int moodLevel() const { return int(mood_.setting()); }
    EasterEggs &eggs() { return eggs_; }
    // Special days, late nights and rare surprises; persisted. Off, none of them happen.
    void setEasterEggsEnabled(bool enabled);
    bool easterEggsEnabled() const { return eggs_.enabled(); }
    void setBirthday(const QString &monthDay); // "MM-dd" or empty; persisted.
    Wellness &wellness() { return wellness_; }
    // Minutes of active time between eye-break and water reminders, from Wellness's choices; 0 is off. Persisted.
    void setEyeMinutes(int minutes);
    void setWaterMinutes(int minutes);
    QString birthday() const { return eggs_.birthday(); }
    // Today's recap rides on the go-home reminder; persisted. The menu shows it either way.
    void setRecapEnabled(bool enabled);
    bool recapEnabled() const { return recapEnabled_; }
    QString recapPath() const; // recap.json beside the preferences; empty when nothing is persisted.
    void showTrayMessage(const QString &title, const QString &text); // For a hidden pet's notes.
    // Petting, throwing and hiding at a screen edge; persisted. Off, the pet only drags.
    void setTouchEnabled(bool enabled);
    bool touchEnabled() const { return touchEnabled_; }
    // What happens when the user lets go of a dragged pet moving at `velocity` (pixels per second):
    // fast enough and it falls; pushed past a screen edge while idle and it hides there.
    void letGo(QPointF velocity);
    void slideToEdge(touch::Edge edge);
    bool flying() const { return flight_.has_value(); }
    bool sliding() const { return slide_.state() == QAbstractAnimation::Running; }
    // Hiding behind a screen edge, partly off-screen. Session activity brings it back out.
    touch::Edge edge() const { return edge_; }
    bool hiding() const { return edge_ != touch::Edge::None; }
    // Walking, crawling and climbing along the screen after a long idle spell; persisted. Off, the pet
    // stays where it was put.
    void setWanderEnabled(bool enabled);
    bool wanderEnabled() const { return wanderEnabled_; }
    // Whether a move could start now: wandering is on, the mood suits it, the pet is in plain view and
    // free, it has the room the move needs, and the display server lets the app place its window.
    bool canWander(const Move &move) const;
    bool walking() const { return !walk_.isEmpty(); } // A move is playing and carries the window along.
    void setPetSize(int pixels);
    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }
    void recover();
    void setOnTop(bool enabled);
    // Interface language: "auto", "en" or "vi"; persisted. Applies at once: the menu, tray and open
    // bubbles relabel themselves and an open settings dialog reopens on the same tab.
    void setLanguage(const QString &language);
    QString language() const { return language_; }
    // The pet shown from the next start, by id; persisted. The running pet never changes.
    void setPet(const QString &id);
    QString pet() const { return pet_; }
    // The plugin packs loaded from the next start, by id; persisted. The running pet keeps what it loaded.
    void setPlugins(const QStringList &ids);
    QStringList plugins() const { return plugins_; }
    // Downloads pets that are not bundled for Settings' picker; made on first use. It outlives the dialog, and a
    // completed download becomes the choice for the next start.
    PetDownloader &petDownloader();
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
    // Snooze (#34): alerts, sounds, reminders and remarks are silent for a while; the badge stays. In memory only.
    Snooze &snooze() { return snooze_; }
    void snoozeFor(int minutes);
    void snoozeUntilTurnEnds();
    void snoozeUntilTomorrow();
    void resumeSnooze();
    // Muted or snoozed at `now`: nothing but the attention badge shows.
    bool quiet(qint64 now) const { return muted_ || snooze_.activeAt(now); }
    // The monitor tells whether the snooze is still on, so the pet shows its "z" and the tray tooltip says so.
    void setSnoozeShown(bool snoozed);
    bool snoozeShown() const { return snoozeShown_; }
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
    QPoint nativePos() const; // Position as the display server reports it, in Qt's logical pixels.
    // The display server counts device pixels. Qt keeps a screen's origin and divides the rest by its
    // device pixel ratio, which is not 1 under a scaled XWayland.
    static QPoint fromNative(QPoint native, QPoint screenOrigin, qreal ratio);
signals:
    void moved();
    void notificationsChanged();
    void quitRequested();
    void presenceChanged(); // The pet was hidden or shown.
    void sessionsRequested(); // A click on the pet (press and release without moving it), or the menu.
    void recapRequested(); // "Today's recap" in the menu.
protected:
    bool event(QEvent *) override;
    void changeEvent(QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    void beginQuit(const QString &remark = {});
    void endDrag(bool released = false);
    void land();
    void slideTo(QPoint target, touch::Edge hide); // Hides at `hide` on arrival, unless it is None.
    void startWalk(const Move &move);
    void walkStep();
    void stopWalk();
    void entered(const QString &state);
    void applyPresence(Presence::Action action);
    void showAfterFlagChange(QPoint position);
    // Reads the startup keys, which `agent-pet autostart` may change while the pet runs.
    void refreshStartup();
    bool writePreferences(const std::function<void(Preferences &)> &change);
    void updateTrayIcon();
    void retranslate(); // Every label set once rather than built on demand.
    void showAbout();
    void watchScreen(QScreen *screen);
    QWidget *integrationSettings(QWidget *parent);
    QWidget *startupSettings(QWidget *parent);
    QWidget *reminderSettings(QWidget *parent);
    updates::Controller *updates_ = nullptr;
    PetDownloader *downloader_ = nullptr;
    Player player_;
    Stage stage_;
    Ambient ambient_;
    Activity activity_;
    Mood mood_;
    EasterEggs eggs_;
    Wellness wellness_;
    Snooze snooze_;
    PreferencesStore store_;
    QMenu menu_;
    QSystemTrayIcon tray_;
    QTimer recoveryTimer_, dragTimer_, saveTimer_, flightTimer_, walkTimer_, quitTimer_;
    QVariantAnimation slide_; // Eases a let-go pet to its hiding place before the hide plays, or a climber on and off its wall.
    QPointer<QDialog> settingsDialog_, previewDialog_, aboutDialog_;
    QAction *clickAction_ = nullptr, *onTopAction_ = nullptr, *muteAction_ = nullptr, *showAction_ = nullptr;
    QAction *resumeAction_ = nullptr, *turnSnoozeAction_ = nullptr, *tomorrowSnoozeAction_ = nullptr;
    QVector<QAction *> snoozeActions_; // One per Snooze::minuteChoices.
    QAction *updateAction_ = nullptr, *updatesItem_ = nullptr;
    QAction *sessionsAction_ = nullptr, *recapAction_ = nullptr, *settingsAction_ = nullptr, *previewAction_ = nullptr;
    QAction *recoverAction_ = nullptr, *aboutAction_ = nullptr, *quitAction_ = nullptr;
    QMenu *moreMenu_ = nullptr, *statesMenu_ = nullptr, *snoozeMenu_ = nullptr;
    QString language_ = "auto";
    QString pet_ = "vpet";
    QStringList plugins_;
    int statusSessions_ = 0, statusAttention_ = 0, statusErrors_ = 0; // The tray tooltip's last counts.
    Presence presence_;
    QPixmap trayBase_;
    int trayAttention_ = 0; // Badge shown on the tray icon; -1 forces a redraw.
    bool trayError_ = false;
    QPoint dragOffset_;
    bool snoozeShown_ = false, fallbackDrag_ = false, dragging_ = false, clickThrough_ = false, touchEnabled_ = true, wanderEnabled_ = true, recapEnabled_ = true;
    bool persist_ = true, ready_ = false, quitting_ = false, muted_ = false, sound_ = false, autostart_ = false;
    int attention_ = 0, bubbles_ = Preferences::RequestsAndErrors;
    QPoint pressPosition_;
    QElapsedTimer pressTimer_, flightClock_, walkClock_;
    QString pressTouch_; // What a held press pets, decided where it landed.
    QVector<touch::Sample> samples_;
    touch::Patience patience_;
    QElapsedTimer touchClock_;
    NoteBubble quitNote_{this};
    std::optional<touch::Flight> flight_;
    QString walk_; // The move playing, while it carries the window.
    QPointF walkPosition_; // Where the walk has taken the window, kept to fractions of a pixel.
    touch::Edge edge_ = touch::Edge::None, slideEdge_ = touch::Edge::None;
};
}
