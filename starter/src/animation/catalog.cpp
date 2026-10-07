#include "catalog.h"
#include "core_states.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <tuple>

namespace pet {
namespace {
bool safePath(const QString &path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains('\\')
        && !path.contains(':') && !path.split('/').contains("..")
        && !path.split('/').contains(".");
}
}
QString drawReaction(const QVector<Reaction> &pool, const Random &random) {
    if (pool.isEmpty()) return {};
    int total = 0;
    for (const auto &reaction : pool) total += reaction.weight;
    int roll = pool.size() == 1 ? 0 : qBound(0, random(total), total - 1);
    for (const auto &reaction : pool) {
        if (roll < reaction.weight) return reaction.state;
        roll -= reaction.weight;
    }
    return pool.first().state;
}
bool validPetId(const QString &id) {
    // Fully anchored: a plain `$` would also accept a trailing newline.
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("[a-z0-9-]{1,32}"));
    return pattern.match(id).hasMatch();
}
const QVector<CoreState> &coreStates() {
    static const QVector<CoreState> states = [] {
        QVector<CoreState> parsed;
        const auto contract = QJsonDocument::fromJson(QByteArray(coreStatesJson)).object()["states"].toObject();
        for (auto it = contract.begin(); it != contract.end(); ++it) {
            const auto shape = it.value().toObject();
            parsed.append({it.key(), shape["mode"].toString(), shape["after"].toString()});
        }
        return parsed;
    }();
    return states;
}
QString Catalog::contractError() const {
    for (const auto &core : coreStates()) {
        const auto found = animations.constFind(core.name);
        if (found == animations.constEnd()) return "Missing core state: " + core.name;
        // A phased core state ends when the app moves on, so it takes no `loops`.
        if (found->mode != core.mode || found->after != core.after || found->loops != 0)
            return QString("Core state %1 must play %2, then %3").arg(core.name, core.mode, core.after);
    }
    return {};
}
Catalog Catalog::load(const QString &root, const QString &pet, QString *error) {
    auto invalid = [error](const QString &reason) {
        if (error) *error = reason;
        return Catalog();
    };
    if (!validPetId(pet)) return invalid("Invalid pet identifier: " + pet);
    const QString folder = "assets/" + pet;
    QFile file(QDir(root).filePath(folder + "/animations.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return invalid("Animation catalog is missing or too large.");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError || document["schema_version"].toInt() != 1)
        return invalid("Invalid animation catalog format.");
    // Frames lie under `asset_root` (the pet's folder by default) and in subfolders of the pet's folder:
    // each subfolder builds into one sequence pack, and the folder's root holds only metadata.
    const auto assetRoot = document.contains("asset_root") ? document["asset_root"].toString() : folder;
    if (!safePath(assetRoot) || (assetRoot != folder && !assetRoot.startsWith(folder + "/")))
        return invalid("Asset root outside the pet folder: " + assetRoot);
    Catalog catalog;
    for (const auto &entry : document["sequences"].toArray()) {
        const auto object = entry.toObject();
        const auto id = object["path"].toString();
        if (!safePath(id) || catalog.sequences.contains(id)) return invalid("Invalid or duplicate sequence identifier.");
        QVector<Frame> frames;
        int total = 0;
        for (const auto &value : object["frames"].toArray()) {
            const auto frame = value.toObject();
            const auto relative = frame["path"].toString();
            const auto duration = frame["duration_ms"].toInt(-1);
            if (!safePath(relative) || !relative.startsWith(assetRoot + "/") || !relative.mid(folder.size() + 1).contains('/')
                || duration < 1 || duration > 60000)
                return invalid("Invalid frame path or duration in " + id);
            frames.append({QDir(root).filePath(relative), duration});
            total += duration;
            if (frames.size() > 1000) return invalid("Too many frames in " + id);
        }
        if (frames.isEmpty() || total != object["duration_ms"].toInt()) return invalid("Invalid sequence timing: " + id);
        catalog.sequences.insert(id, frames);
    }
    const auto states = document["states"].toObject();
    const auto playback = document["playback"].toObject();
    constexpr int maxWeight = 1000;
    for (auto it = states.begin(); it != states.end(); ++it) {
        Choice primary;
        for (const auto &value : it.value().toArray()) {
            auto id = value.toString();
            if (!catalog.sequences.contains(id)) return invalid("Unknown sequence for " + it.key());
            primary.sequences.append(id);
        }
        const auto policy = playback[it.key()].toObject();
        Animation animation;
        animation.mode = policy["mode"].toString();
        animation.after = policy["after"].toString();
        // `weight` ranks the entry against the state's variants; `loops` ends a phased state by
        // itself after that many passes of its loop phase.
        primary.weight = policy.contains("weight") ? policy["weight"].toInt(0) : 1;
        animation.loops = policy["loops"].toInt(0);
        if ((animation.mode != "phased" && animation.mode != "loop" && animation.mode != "once")
            || primary.sequences.size() != (animation.mode == "phased" ? 3 : 1)
            || !QStringList{"idle", "previous", "stop"}.contains(animation.after)
            || primary.weight < 1 || primary.weight > maxWeight
            || (policy.contains("loops") && (animation.mode != "phased" || animation.loops < 1 || animation.loops > 100)))
            return invalid("Invalid playback policy for " + it.key());
        animation.choices.append(primary);
        catalog.animations.insert(it.key(), animation);
    }
    if (!catalog.animations.contains("idle") || catalog.animations["idle"].mode != "loop") return invalid("Missing idle loop.");
    const auto variants = document["variants"].toObject();
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        if (!catalog.animations.contains(it.key()) || it.value().toArray().isEmpty())
            return invalid("Variants for an unknown or empty state: " + it.key());
        auto &animation = catalog.animations[it.key()];
        const int phases = animation.choices.first().sequences.size();
        for (const auto &value : it.value().toArray()) {
            Choice choice;
            choice.weight = value.toObject()["weight"].toInt(0);
            for (const auto &id : value.toObject()["sequences"].toArray()) {
                if (!catalog.sequences.contains(id.toString())) return invalid("Unknown variant sequence for " + it.key());
                choice.sequences.append(id.toString());
            }
            if (choice.sequences.size() != phases || choice.weight < 1 || choice.weight > maxWeight)
                return invalid("Invalid variant for " + it.key());
            animation.choices.append(choice);
        }
    }
    // Mood art: per mood, states whose choices it replaces. Each choice keeps the state's shape.
    const auto moods = document["moods"].toObject();
    for (auto mood = moods.begin(); mood != moods.end(); ++mood) {
        if (mood.key() != "happy" && mood.key() != "poor") return invalid("Unknown mood: " + mood.key());
        const auto states = mood.value().toObject();
        for (auto it = states.begin(); it != states.end(); ++it) {
            if (!catalog.animations.contains(it.key()) || it.value().toArray().isEmpty())
                return invalid("Mood art for an unknown or empty state: " + it.key());
            const int phases = catalog.animations[it.key()].choices.first().sequences.size();
            QVector<Choice> choices;
            for (const auto &value : it.value().toArray()) {
                Choice choice;
                choice.weight = value.toObject()["weight"].toInt(0);
                for (const auto &id : value.toObject()["sequences"].toArray()) {
                    if (!catalog.sequences.contains(id.toString())) return invalid("Unknown mood sequence for " + it.key());
                    choice.sequences.append(id.toString());
                }
                if (choice.sequences.size() != phases || choice.weight < 1 || choice.weight > maxWeight)
                    return invalid("Invalid mood choice for " + it.key());
                choices.append(choice);
            }
            catalog.moods[mood.key()].insert(it.key(), choices);
        }
    }
    // A fidget or a reaction must end by itself and hand back to idle, so nothing has to wait for it.
    auto endsItself = [&catalog](const QString &state) {
        const auto found = catalog.animations.constFind(state);
        return state != "idle" && found != catalog.animations.constEnd() && found->after == "idle"
            && (found->mode == "once" || (found->mode == "phased" && found->loops > 0));
    };
    const auto reactions = document["reactions"].toObject();
    for (auto it = reactions.begin(); it != reactions.end(); ++it) {
        if (it.value().toArray().isEmpty()) return invalid("Empty reaction: " + it.key());
        for (const auto &value : it.value().toArray()) {
            const auto object = value.toObject();
            const Reaction reaction{object["state"].toString(), object["weight"].toInt(0)};
            if (!endsItself(reaction.state) || reaction.weight < 1 || reaction.weight > maxWeight)
                return invalid("Invalid reaction for " + it.key() + ": " + reaction.state);
            catalog.reactions[it.key()].append(reaction);
        }
    }
    const auto ambient = document["ambient"].toObject();
    if (ambient.contains("sleep_after_s")) {
        catalog.sleepAfterS = ambient["sleep_after_s"].toInt(0);
        if (catalog.sleepAfterS < 60 || catalog.sleepAfterS > 86400) return invalid("Invalid ambient sleep delay.");
    }
    for (const auto &value : ambient["fidgets"].toArray()) {
        const auto object = value.toObject();
        Fidget fidget{object["state"].toString(), object["weight"].toInt(0), object["min_idle_s"].toInt(0),
                      object["rare"].toBool()};
        if (!endsItself(fidget.state) || catalog.fidgetStates.contains(fidget.state)
            || fidget.weight < 1 || fidget.weight > maxWeight || fidget.minIdleS < 0 || fidget.minIdleS > 86400)
            return invalid("Invalid ambient fidget: " + fidget.state);
        catalog.fidgets.append(fidget);
        catalog.fidgetStates.insert(fidget.state);
    }
    // Touch reactions are held while the user (or a fall) keeps them, so each holds a loop phase that
    // only a release ends. Every part is optional; a catalog without the section has no reactions.
    if (document.contains("touch")) {
        const auto touch = document["touch"].toObject();
        catalog.touch.scale = touch["scale"].toInt(0);
        if (catalog.touch.scale < 10 || catalog.touch.scale > 10000) return invalid("Invalid touch scale.");
        auto heldState = [&catalog](const QJsonValue &value, QString &state) {
            state = value.toString();
            const auto found = catalog.animations.constFind(state);
            if (found == catalog.animations.constEnd() || found->mode != "phased" || found->loops > 0
                || catalog.fidgetStates.contains(state) || state == "idle" || state == "dragging") return false;
            catalog.touchStates.insert(state);
            return true;
        };
        for (const auto &value : touch["regions"].toArray()) {
            const auto object = value.toObject();
            const auto rect = object["rect"].toArray();
            TouchRegion region;
            if (!heldState(object["state"], region.state) || rect.size() != 4) return invalid("Invalid touch region.");
            region.rect = QRect(rect[0].toInt(-1), rect[1].toInt(-1), rect[2].toInt(0), rect[3].toInt(0));
            if (region.rect.left() < 0 || region.rect.top() < 0 || region.rect.width() < 1 || region.rect.height() < 1
                || region.rect.right() >= catalog.touch.scale || region.rect.bottom() >= catalog.touch.scale)
                return invalid("Touch region outside the artwork: " + region.state);
            catalog.touch.regions.append(region);
        }
        const auto fall = touch["fall"].toObject(), edge = touch["edge"].toObject();
        if ((fall.contains("left") && !heldState(fall["left"], catalog.touch.fallLeft))
            || (fall.contains("right") && !heldState(fall["right"], catalog.touch.fallRight)))
            return invalid("Invalid fall reaction.");
        for (auto [side, state, at] : {std::tuple{QString("left"), &catalog.touch.edgeLeft, &catalog.touch.edgeLeftAt},
                                       std::tuple{QString("right"), &catalog.touch.edgeRight, &catalog.touch.edgeRightAt}}) {
            if (!edge.contains(side)) continue;
            const auto object = edge[side].toObject();
            *at = object["at"].toInt(0);
            if (!heldState(object["state"], *state) || *at < 1 || *at >= catalog.touch.scale)
                return invalid("Invalid edge reaction: " + side);
        }
    }
    // Moves carry the window across the screen during their loop phase, which a count must end, like
    // a fidget's. Every part is optional; a catalog without the section never moves on its own.
    if (document.contains("moves")) {
        const auto moves = document["moves"].toObject();
        catalog.moveScale = moves["scale"].toInt(0);
        if (catalog.moveScale < 10 || catalog.moveScale > 10000) return invalid("Invalid move scale.");
        auto sides = [&catalog](const QJsonValue &value, Sides &sides) {
            if (value.isUndefined()) return true;
            const auto object = value.toObject();
            if (!value.isObject() || object.isEmpty()) return false;
            for (auto it = object.begin(); it != object.end(); ++it) {
                const double distance = it.value().toDouble(-1);
                if (!it.value().isDouble() || distance < 0 || distance > 10.0 * catalog.moveScale) return false;
                if (it.key() == "left") sides.left = distance;
                else if (it.key() == "top") sides.top = distance;
                else if (it.key() == "right") sides.right = distance;
                else if (it.key() == "bottom") sides.bottom = distance;
                else return false;
            }
            return true;
        };
        for (const auto &value : moves["list"].toArray()) {
            const auto object = value.toObject();
            const auto speed = object["speed"].toArray();
            const auto wall = object["wall"].toObject();
            Move move;
            move.state = object["state"].toString();
            move.mood = object["mood"].toString();
            move.wall = wall["side"].toString();
            move.at = wall["at"].toInt(0);
            if (speed.size() == 2) move.speed = {speed[0].toDouble(), speed[1].toDouble()};
            const double limit = 4.0 * catalog.moveScale;
            if (!endsItself(move.state) || catalog.animations[move.state].mode != "phased" || catalog.moves.contains(move.state)
                || speed.size() != 2 || !speed[0].isDouble() || !speed[1].isDouble() || move.speed.isNull()
                || qAbs(move.speed.x()) > limit || qAbs(move.speed.y()) > limit
                || (object.contains("mood") && move.mood != "happy" && move.mood != "poor")
                || (object.contains("wall") && ((move.wall != "left" && move.wall != "right") || move.at < 1 || move.at >= catalog.moveScale))
                || !sides(object.value("room"), move.room) || !sides(object.value("near"), move.near)
                || !sides(object.value("keep"), move.keep))
                return invalid("Invalid move: " + move.state);
            catalog.moves.insert(move.state, move);
        }
    }
    // Activity decoration: alternate loops, reactions and desk continuity for states that end only when
    // asked. Every part is optional; a state without an entry plays only its own sequences.
    if (document.contains("activity")) {
        const auto activity = document["activity"].toObject();
        auto decorated = [&](const QString &state) {
            const auto found = catalog.animations.constFind(state);
            return found != catalog.animations.constEnd() && found->mode == "phased" && found->loops == 0 && state != "idle"
                && state != "dragging" && !catalog.fidgetStates.contains(state) && !catalog.touchStates.contains(state);
        };
        auto pool = [&](const QJsonValue &value, QVector<ActivityChoice> &choices, bool styled) {
            if (!value.isArray() || value.toArray().isEmpty()) return false;
            for (const auto &item : value.toArray()) {
                const auto object = item.toObject();
                const auto style = object.value("style");
                const ActivityChoice choice{object["sequence"].toString(), object["weight"].toInt(0), style == "playful"};
                if (!catalog.sequences.contains(choice.sequence) || choice.weight < 1 || choice.weight > maxWeight
                    || (!style.isUndefined() && (!styled || (style != "subtle" && style != "playful"))))
                    return false;
                choices.append(choice);
            }
            return true;
        };
        if (!document["activity"].isObject()) return invalid("Invalid activity section.");
        for (auto it = activity.begin(); it != activity.end(); ++it) {
            const auto object = it.value().toObject();
            auto other = [&](const QString &state) { return state != it.key() && activity.contains(state); };
            ActivityArt art;
            bool ok = decorated(it.key()) && it.value().isObject();
            if (ok && object.contains("loops")) ok = pool(object["loops"], art.loops, true);
            if (ok && object.contains("enter")) {
                const auto enter = object["enter"].toObject();
                ok = !enter["from"].toArray().isEmpty() && pool(enter["choices"], art.enter, false);
                for (const auto &state : enter["from"].toArray()) {
                    ok = ok && other(state.toString());
                    art.enterFrom.append(state.toString());
                }
            }
            if (ok && object.contains("exit")) {
                const auto exit = object["exit"].toObject();
                ok = !exit.isEmpty();
                for (auto target = exit.begin(); ok && target != exit.end(); ++target)
                    ok = other(target.key()) && pool(target.value(), art.exit[target.key()], false);
            }
            if (ok && object.contains("linger")) {
                const auto linger = object["linger"].toObject();
                const int maxS = linger["max_s"].toInt(0);
                art.linger = {linger["to"].toString(), maxS * 1000, linger["in"].toString(), linger["out"].toString(), {}};
                ok = other(art.linger.to) && maxS >= 1 && maxS <= 60 && !linger["loop"].toArray().isEmpty()
                    && (!linger.contains("in") || catalog.sequences.contains(art.linger.in))
                    && (!linger.contains("out") || catalog.sequences.contains(art.linger.out));
                for (const auto &id : linger["loop"].toArray()) {
                    ok = ok && catalog.sequences.contains(id.toString());
                    art.linger.loop.append(id.toString());
                }
            }
            if (ok && object.contains("handover")) {
                const auto handover = object["handover"].toObject();
                ok = !handover.isEmpty();
                for (auto target = handover.begin(); ok && target != handover.end(); ++target) {
                    ok = other(target.key()) && catalog.sequences.contains(target.value().toString());
                    art.handover.insert(target.key(), target.value().toString());
                }
            }
            if (!ok) return invalid("Invalid activity for " + it.key());
            catalog.activity.insert(it.key(), art);
        }
    }
    return catalog;
}
}
