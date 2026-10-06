#pragma once
#include "random.h"
#include <QCache>
#include <QMap>
#include <QObject>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <QVector>

namespace pet {
struct Frame { QString path; int durationMs; };
// One way to play a state: a sequence per phase. The catalog's own entry is the first choice;
// "variants" add more, and one is drawn by weight each time the state is entered.
struct Choice { QStringList sequences; int weight = 1; };
struct Animation { QVector<Choice> choices; QString mode; QString after; int loops = 0; };
// An ambient one-shot that may play while the pet idles. `minIdleS` gates it behind idle time;
// a rare fidget is drawn separately, with a small fixed chance.
struct Fidget { QString state; int weight = 1; int minIdleS = 0; bool rare = false; };
// One state a reaction pool may play, such as a way to celebrate a finished turn.
struct Reaction { QString state; int weight = 1; };
// A state drawn by weight from a reaction pool; a pool of one draws no number. Empty for an empty pool.
QString drawReaction(const QVector<Reaction> &pool, const Random &random);
// How the pet answers being handled, from the catalog's "touch" section. Rectangles and edge lines are
// in the artwork's own square space of `scale` units, so they follow the pet's size.
struct TouchRegion { QString state; QRect rect; };
struct Touch {
    int scale = 0; // 0: the catalog has no touch section.
    QVector<TouchRegion> regions; // Held presses; the first region containing the point wins.
    QString fallLeft, fallRight; // Thrown, by direction of travel.
    QString edgeLeft, edgeRight; // Pushed past a screen edge.
    int edgeLeftAt = 0, edgeRightAt = 0; // Where the screen edge cuts the artwork while hiding.
};
// Distances from each side of the pet's window to the same side of its screen, in the artwork's units.
// For a condition, a negative value leaves that side out.
struct Sides { double left = -1, top = -1, right = -1, bottom = -1; };
// A walk, crawl or climb across the screen, from the catalog's "moves" section (ported from the
// upstream vup.lps `move` lines). It plays as an ambient fidget; its window moves during the loop phase.
struct Move {
    QString state;
    QPointF speed; // Artwork units per second.
    QString mood; // Only in this mood ("happy" or "poor"); empty for any.
    QString wall; // "left" or "right": clings to that screen edge, which cuts the artwork at `at` units.
    int at = 0;
    Sides room; // Starts only with at least this much space on these sides...
    Sides near; // ...and less than this much on these.
    Sides keep; // Ends early once the space on these sides is this much or less.
};
// Decoration of a sustained activity (thinking, reading, working) from the catalog's "activity" section.
// It changes which sequence plays inside the state, never the state itself.
struct ActivityChoice { QString sequence; int weight = 1; bool playful = false; };
// Staying at the desk while another activity is requested: `in`, passes of `loop`, then `out`.
struct ActivityLinger { QString to; int maxMs = 0; QString in, out; QStringList loop; };
struct ActivityArt {
    QVector<ActivityChoice> loops; // Alternate loop passes; `playful` ones only in the Playful style.
    QStringList enterFrom; QVector<ActivityChoice> enter; // A first loop pass after one of these states.
    QMap<QString, QVector<ActivityChoice>> exit; // An end phase for leaving to that state.
    ActivityLinger linger; // None while `linger.to` is empty.
    QMap<QString, QString> handover; // To that state without leaving the desk.
};

class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr, const QString &resourceRoot = ":/");
    // Normal changes finish the current held state's exit; urgent changes cut immediately.
    bool select(const QString &state, bool interrupt = false);
    // A held state stays until released, whatever else is selected meanwhile; release() then plays
    // its end and goes on to the latest request. Holding another state swaps it in at once.
    void hold(const QString &state);
    void release();
    bool held() const { return held_; }
    void beginDrag() { hold("dragging"); }
    void endDrag() { release(); }
    void setRenderSize(int physicalPixels);
    void setPaused(bool paused);
    void advance(); // A single deterministic frame step, also used by the timer and preview.
    QString state() const { return state_; }
    QString requestedState() const { return pending_.isEmpty() ? state_ : pending_; }
    QString sequence() const { return sequence_; }
    QString phase() const;
    QString error() const { return error_; }
    QStringList states() const { return animations_.keys(); }
    const QPixmap &pixmap() const { return pixmap_; }
    int frameIndex() const { return index_; }
    int frameCount() const { return sequences_.value(sequence_).size(); }
    int frameDuration() const { return duration_; }
    int cacheKiB() const { return cache_.totalCost(); }
    static constexpr int cacheLimitKiB = 8192;
    bool paused() const { return paused_; }
    bool stopped() const { return stopped_; }
    bool isDragging() const { return held_ && state_ == "dragging"; }
    bool valid() const { return !animations_.isEmpty(); }
    void clearError() { error_.clear(); }
    // Alternate sequences for idle and fidgets; off plays only the catalog's own entry.
    void setVariants(bool enabled) { variants_ = enabled; }
    bool variants() const { return variants_; }
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
    const QVector<Fidget> &fidgets() const { return fidgets_; }
    bool isFidget(const QString &state) const { return fidgetStates_.contains(state); }
    int sleepAfterS() const { return sleepAfterS_; } // Idle time before drifting to sleep; 0 when unset.
    // The catalog's mood art ("happy" or "poor") replaces a state's choices while that mood is set; an
    // empty or unknown mood, or a state without mood art, plays the usual choices. A held state keeps
    // what it drew until it is entered again; a loop picks up the new mood on its next pass.
    void setMood(const QString &mood) { mood_ = mood; }
    QString mood() const { return mood_; }
    bool hasMood(const QString &mood, const QString &state) const { return moods_.value(mood).contains(state); }
    // Weighted states for a named reaction ("turn_finished", "snack", "milestone"); empty when not in the catalog.
    QVector<Reaction> reactions(const QString &name) const { return reactions_.value(name); }
    const Touch &touch() const { return touch_; }
    bool isTouch(const QString &state) const { return touchStates_.contains(state); }
    // The state for a press at `point` on a square widget `side` pixels wide; empty off every region.
    QString touchAt(QPointF point, int side) const;
    // The move a state plays, or null. Moves are in the artwork's square space of moveScale() units.
    const Move *move(const QString &state) const;
    int moveScale() const { return moveScale_; }
    // The decoration of an activity state, or null for a state without any.
    const ActivityArt *activity(const QString &state) const;
    // Ends a phased state's start or loop now: it plays its end, then goes on as it would have.
    void finish();
signals:
    void changed();
    void failed(const QString &message);
    void completed(const QString &state);
    // A state was just entered; listeners must not select from this signal.
    void entered(const QString &state);
    // A looping state started another pass; listeners may select a new state.
    void looped(const QString &state);
private:
    bool load(const QString &resourceRoot);
    void enter(const QString &state);
    void enterSequence(int phase);
    void display();
    void fail(const QString &message);
    QString resumeTarget() const;
    QStringList choose(const QString &state);
    QMap<QString, QVector<Frame>> sequences_;
    QMap<QString, Animation> animations_;
    QVector<Fidget> fidgets_;
    QMap<QString, QMap<QString, QVector<Choice>>> moods_; // mood -> state -> choices
    QMap<QString, QVector<Reaction>> reactions_;
    QSet<QString> fidgetStates_, touchStates_;
    Touch touch_;
    QMap<QString, Move> moves_;
    QMap<QString, ActivityArt> activity_;
    QStringList chosen_;
    Random random_ = systemRandom();
    QCache<QString, QPixmap> cache_{cacheLimitKiB};
    QPixmap pixmap_;
    QTimer timer_;
    QString state_, sequence_, pending_, previous_ = "idle", holdResume_ = "idle", error_, mood_;
    int index_ = 0, phase_ = 0, duration_ = 0, renderSize_ = 240, loopCount_ = 0, sleepAfterS_ = 0, moveScale_ = 0;
    bool paused_ = false, stopped_ = false, held_ = false, variants_ = true;
};
}
