#pragma once
#include "catalog.h"
#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <functional>

namespace pet {

class Player : public QObject {
    Q_OBJECT
public:
    // Plays PetLibrary::shared()'s active pet, activating VPet first when none is active.
    explicit Player(QObject *parent = nullptr);
    // Plays the pet in a folder holding assets/<pet>/animations.json and its frames (tests).
    Player(QObject *parent, const QString &root, const QString &pet = "vpet");
    // Plays a catalog that was already loaded, such as a PetLibrary's.
    Player(QObject *parent, const Catalog &catalog);
    // Normal changes finish the current held state's exit; urgent changes cut immediately.
    bool select(const QString &state, bool interrupt = false);
    // Selects the state a state cue plays on this pet (src/animation/cues.json); false for any other name.
    bool play(const QString &cue, bool interrupt = false);
    QString stateFor(const QString &cue) const { return catalog_.stateFor(cue); }
    // The latest request is the state this cue plays.
    bool requested(const QString &cue) const { return requestedState() == stateFor(cue); }
    // A held state stays until released, whatever else is selected meanwhile; release() then plays
    // its end and goes on to the latest request. Holding another state swaps it in at once.
    void hold(const QString &state);
    void release();
    bool held() const { return held_; }
    void beginDrag() { hold(stateFor("drag")); }
    void endDrag() { release(); }
    void setRenderSize(int physicalPixels);
    void setPaused(bool paused);
    void advance(); // A single deterministic frame step, also used by the timer and preview.
    QString state() const { return state_; }
    QString requestedState() const { return pending_.isEmpty() ? state_ : pending_; }
    QString sequence() const { return sequence_; }
    QString phase() const;
    QString error() const { return error_; }
    // A state is ending by itself and the next one is being entered: `entered` is then not an interruption.
    bool finishing() const { return finishing_; }
    QStringList states() const { return catalog_.animations.keys(); }
    const QPixmap &pixmap() const { return pixmap_; }
    int frameIndex() const { return index_; }
    int frameCount() const { return catalog_.sequences.value(sequence_).size(); }
    int frameDuration() const { return duration_; }
    int cacheKiB() const { return cache_.totalCost(); }
    static constexpr int cacheLimitKiB = 8192;
    bool paused() const { return paused_; }
    bool stopped() const { return stopped_; }
    bool isDragging() const { return held_ && state_ == stateFor("drag"); }
    bool valid() const { return !catalog_.animations.isEmpty(); }
    void clearError() { error_.clear(); }
    // Alternate sequences for idle and fidgets; off plays only the catalog's own entry.
    void setVariants(bool enabled) { variants_ = enabled; }
    bool variants() const { return variants_; }
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
    const QVector<Fidget> &fidgets() const { return catalog_.fidgets; }
    bool isFidget(const QString &state) const { return catalog_.fidgetStates.contains(state); }
    int sleepAfterS() const { return catalog_.sleepAfterS; } // Idle time before drifting to sleep; 0 when unset.
    // The catalog's mood art ("happy" or "poor") replaces a state's choices while that mood is set; an
    // empty or unknown mood, or a state without mood art, plays the usual choices. A held state keeps
    // what it drew until it is entered again; a loop picks up the new mood on its next pass.
    void setMood(const QString &mood) { mood_ = mood; }
    QString mood() const { return mood_; }
    bool hasMood(const QString &mood, const QString &state) const { return catalog_.moods.value(mood).contains(state); }
    // The weighted states a reaction cue ("celebrate", "snack", "danger", ...) draws from; empty when the pet maps none.
    QVector<Reaction> pool(const QString &cue) const { return catalog_.pools.value(cue); }
    const Touch &touch() const { return catalog_.touch; }
    bool isTouch(const QString &state) const { return catalog_.touchStates.contains(state); }
    // The state for a press at `point` on a square widget `side` pixels wide; empty off every region.
    QString touchAt(QPointF point, int side) const;
    // The move a state plays, or null. Moves are in the artwork's square space of moveScale() units.
    const Move *move(const QString &state) const;
    int moveScale() const { return catalog_.moveScale; }
    // The decoration of an activity state, or null for a state without any.
    const ActivityArt *activity(const QString &state) const;
    // Handover and linger keep the pet at its desk between activities. Off, every change plays the
    // outgoing state's end, as without the section.
    void setContinuity(bool enabled) { continuity_ = enabled; }
    bool continuity() const { return continuity_; }
    // Asked each time an enter or exit reaction could play; true spends that opportunity on it. Unset,
    // none plays.
    void setReactionGate(std::function<bool()> gate) { reactionGate_ = std::move(gate); }
    // Plays one of the state's alternate loops for this pass instead of its own. Only at the first frame
    // of a loop pass, as from a `looped` handler; false otherwise.
    bool vary(const QString &sequence);
    // Ends a phased state's start or loop now: it plays its end, then goes on as it would have.
    void finish();
signals:
    void changed();
    void failed(const QString &message);
    // A state played to its end: a one-shot, or a phased state's end phase. Emitted after the next state
    // is entered, so listeners may select.
    void completed(const QString &state);
    void heldChanged(bool held); // hold() took the pet, or release() let it go.
    // A state was just entered; listeners must not select from this signal.
    void entered(const QString &state);
    // A looping state, or a phased state's loop phase, started another pass; listeners may select a
    // new state or vary the pass.
    void looped(const QString &state);
private:
    // Plays `catalog`, or shows `error` (the "Artwork unavailable" placeholder) when it is empty.
    void open(const Catalog &catalog, const QString &error);
    enum class Decoration { None, LingerIn, Linger, LingerOut, Handover };
    void enter(const QString &state, int phase = 0);
    // `sequence` replaces the phase's own for this pass, as an alternate or a reaction does.
    void enterSequence(int phase, const QString &sequence = {});
    bool decorate(const QString &target);
    void decorationEnded();
    void leaveLinger();
    void beginEnd();
    QString drawChoice(const QVector<ActivityChoice> &pool);
    QString drawLinger();
    void display();
    void fail(const QString &message);
    QString resumeTarget() const;
    QStringList choose(const QString &state);
    Catalog catalog_; // The pet's validated catalog; playback never changes it.
    std::function<bool()> reactionGate_;
    Decoration decoration_ = Decoration::None;
    QString welcome_, lingerLast_; // `welcome_`: the enter reaction waiting for the first loop pass.
    QString handoverTo_; // Where a playing handover lands; the request may have moved on meanwhile.
    int lingerMs_ = 0;
    bool continuity_ = false;
    QStringList chosen_;
    Random random_ = systemRandom();
    QCache<QString, QPixmap> cache_{cacheLimitKiB};
    QPixmap pixmap_;
    QTimer timer_;
    QString state_, sequence_, pending_, previous_ = "idle", holdResume_ = "idle", error_, mood_;
    int index_ = 0, phase_ = 0, duration_ = 0, renderSize_ = 240, loopCount_ = 0;
    bool paused_ = false, stopped_ = false, held_ = false, variants_ = true, finishing_ = false;
};
}
