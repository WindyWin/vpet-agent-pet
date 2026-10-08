#pragma once
#include "random.h"
#include <QMap>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
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

// What the app asks a pet to show, from src/animation/cues.json (embedded at build time), sorted by name. A state
// cue plays one state with this playback: `state` unless the catalog maps the cue to another (never for a `fixed`
// cue). A reaction cue plays a state drawn from the pool the catalog maps it to, and nothing without one.
struct Cue { QString name; bool reaction = false; QString state, mode, after; bool fixed = false; };
const QVector<Cue> &cues();
const Cue *findCue(const QString &name); // Null for a name outside the vocabulary.
// A pet's identifier, which is also its folder under assets/: [a-z0-9-]{1,32}.
bool validPetId(const QString &id);

// The catalog schema this build reads; scripts/migrate_catalog.py brings older catalogs up to it.
inline constexpr int catalogSchema = 2;
// A pet's animation catalog (assets/<id>/animations.json), parsed and validated. A Player copies it and
// never changes it.
struct Catalog {
    QMap<QString, QVector<Frame>> sequences; // Frame paths are resolved against the root it was loaded from.
    QMap<QString, Animation> animations;
    QVector<Fidget> fidgets;
    QMap<QString, QMap<QString, QVector<Choice>>> moods; // mood -> state -> choices
    QMap<QString, QString> cueStates; // The catalog's own state for a state cue, where it maps one.
    QMap<QString, QVector<Reaction>> pools; // Reaction cue -> its pool, where the catalog maps one.
    QSet<QString> fidgetStates, touchStates;
    Touch touch;
    QMap<QString, Move> moves;
    QMap<QString, ActivityArt> activity;
    int sleepAfterS = 0, moveScale = 0;
    // Reads <root>/assets/<pet>/animations.json. Frames must lie under its `asset_root` (the pet's folder
    // by default) and in subfolders of the pet's folder. Empty, with `error` set, when anything is invalid.
    // Whether every state cue can play is not part of this: see contractError().
    static Catalog load(const QString &root, const QString &pet, QString *error);
    bool valid() const { return !animations.isEmpty(); }
    // The state a state cue plays here (the catalog's mapping, else the cue's default); empty for any other name.
    QString stateFor(const QString &cue) const;
    // Empty when every state cue plays an existing state with the cue's playback; otherwise the first problem.
    QString contractError() const;
};
}
