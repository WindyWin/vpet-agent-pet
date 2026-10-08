#pragma once
#include "sessions/presence.h"
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTime>
#include <QVector>

namespace pet {
struct ReminderSchedule {
    QTime lunch = QTime(12, 0);
    QTime monday = QTime(6, 0), leaveWork = QTime(16, 45), sleep = QTime(22, 0);
};
struct Preferences {
    static constexpr int defaultSize = 240;
    static constexpr int recoveryMs = 15000;
    int size = defaultSize;
    QPoint position;
    bool hasPosition = false;
    bool onTop = true;
    bool muted = false; // Hides alert bubbles; the attention badge stays visible.
    bool sound = false; // Optional system beep for newly raised alerts.
    // Which alerts pop a bubble. Pet animation and badge react to everything regardless.
    enum Bubbles { RequestsOnly = 0, RequestsAndErrors = 1, AllAlerts = 2 };
    int bubbles = RequestsAndErrors;
    // How much the idle pet does on its own: fidgets, alternate idle loops and dozing off.
    enum Ambient { AmbientOff = 0, AmbientSubtle = 1, AmbientLively = 2 };
    int ambient = AmbientSubtle;
    // How sustained thinking, reading and working vary: one loop each as before, calm alternates with
    // desk continuity, or also pen spinning and small reactions. Independent of `ambient`.
    enum Activity { ActivityClassic = 0, ActivitySubtle = 1, ActivityPlayful = 2 };
    int activity = ActivityPlayful;
    // Whether agent activity sets the pet's mood: never, only cheerful, or also droopy after errors.
    enum Mood { MoodOff = 0, MoodCheerful = 1, MoodFull = 2 };
    int mood = MoodFull;
    int turns = 0; // Finished turns seen so far, for the pet's every-hundredth celebration.
    bool touch = true; // Reacts to petting, throwing and being pushed against a screen edge.
    bool wander = true; // Walks, crawls and climbs along the screen after a long idle spell.
    bool easterEggs = true; // Special days, late nights and a few rare surprises.
    QString birthday; // "MM-dd" for a birthday surprise, or empty.
    // Wellness reminders: minutes of active time between eye breaks and between sips of water; 0 is off.
    int eyeMinutes = 20, waterMinutes = 60;
    ReminderSchedule reminderSchedule; // Local clock times; reminders fire at most once a day.
    bool recap = true; // Adds today's recap to the go-home reminder.
    // Interface language: "auto" (the system's), "en" or "vi". An unknown value, perhaps from a newer
    // version, reads as "auto" instead of invalidating the file.
    QString language = "auto";
    // The pet shown from the next start, by id ([a-z0-9-]{1,32}). An id this build does not know, perhaps
    // from a newer version, is kept and runs VPet meanwhile; an invalid one reads as "vpet".
    QString pet = "vpet";
    // Startup keys. `agent-pet autostart` edits them without a display, possibly
    // while a pet runs, so the pet re-reads them before each save.
    bool autostart = false; // Hook launches the pet on a session start when none is running.
    IdlePolicy whenIdle = IdlePolicy::Keep;
    static QPoint visiblePosition(QPoint position, QSize size, const QVector<QRect> &screens);
    static bool validBirthday(const QString &monthDay); // "MM-dd" of a real date; February 29 counts.
    static bool validPet(const QString &id); // The rule of pet::validPetId, which this headless library cannot link.
};
class PreferencesStore {
public:
    explicit PreferencesStore(QString path = {});
    Preferences load();
    bool save(const Preferences &preferences);
    QString error() const { return error_; }
    QString path() const { return path_; }
private:
    QString path_, error_;
    bool writable_ = true;
};
}
