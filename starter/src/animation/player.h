#pragma once
#include "random.h"
#include <QCache>
#include <QMap>
#include <QObject>
#include <QPixmap>
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

class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr, const QString &resourceRoot = ":/");
    // Normal changes finish the current held state's exit; urgent changes cut immediately.
    bool select(const QString &state, bool interrupt = false);
    void beginDrag();
    void endDrag();
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
    bool isDragging() const { return dragActive_; }
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
    QSet<QString> fidgetStates_;
    QStringList chosen_;
    Random random_ = systemRandom();
    QCache<QString, QPixmap> cache_{cacheLimitKiB};
    QPixmap pixmap_;
    QTimer timer_;
    QString state_, sequence_, pending_, previous_ = "idle", dragResume_ = "idle", error_, mood_;
    int index_ = 0, phase_ = 0, duration_ = 0, renderSize_ = 240, loopCount_ = 0, sleepAfterS_ = 0;
    bool paused_ = false, stopped_ = false, dragActive_ = false, variants_ = true;
};
}
