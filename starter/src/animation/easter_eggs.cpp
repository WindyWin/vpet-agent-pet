#include "easter_eggs.h"
#include "settings/preferences.h"

namespace pet {
EasterEggs::EasterEggs(Player &player, QObject *parent) : QObject(parent), player_(player) {
    // Anything else showing ends a surprise, whether it finished or was cut short.
    connect(&player_, &Player::entered, this, [this](const QString &state) { if (state != surprise_) surprise_.clear(); });
}
void EasterEggs::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) surprise_.clear();
}
bool EasterEggs::setBirthday(const QString &monthDay) {
    if (!monthDay.isEmpty() && !Preferences::validBirthday(monthDay)) return false;
    birthday_ = monthDay;
    return true;
}
QStringList EasterEggs::occasionsAt(const QDateTime &local, const QString &birthday) {
    QStringList occasions;
    const auto date = local.date();
    const int hour = local.time().hour();
    if (date.month() == 5 && date.day() == 20) occasions << "may20";
    if (Preferences::validBirthday(birthday)) {
        const int month = birthday.left(2).toInt();
        int day = birthday.mid(3).toInt();
        if (month == 2 && day == 29 && !QDate::isLeapYear(date.year())) day = 28;
        if (date.month() == month && date.day() == day) occasions << "birthday";
    }
    if (hour >= lateNightFrom && hour < lateNightUntil) occasions << "late_night";
    if (date.dayOfWeek() == Qt::Friday && hour >= fridayEveningFrom) occasions << "friday_evening";
    return occasions;
}
QString EasterEggs::draw(const QString &pool) { return drawReaction(player_.reactions(pool), random_); }
QString EasterEggs::fidget() {
    if (!enabled_) return {};
    const auto now = clock_();
    const auto occasions = occasionsAt(now, birthday_);
    if (greetedOn_ != now.date()) { greetedOn_ = now.date(); greeted_.clear(); }
    QStringList days;
    for (const QString occasion : {"birthday", "may20"})
        if (occasions.contains(occasion) && !player_.reactions(occasion).isEmpty()) days << occasion;
    // The first fidgets of the day greet with each occasion in turn; afterwards they come up now and then.
    for (const auto &occasion : days)
        if (!greeted_.contains(occasion)) { greeted_.insert(occasion); return draw(occasion); }
    for (const auto &occasion : days)
        if (random_(occasionOneIn) == 0) return draw(occasion);
    if (occasions.contains("late_night") && !player_.reactions("late_night").isEmpty() && random_(lateNightOneIn) == 0)
        return draw("late_night");
    return {};
}
QString EasterEggs::celebration(qint64 turnMs) {
    if (!enabled_) return {};
    auto has = [this](const QString &pool) { return !player_.reactions(pool).isEmpty(); };
    if (turnMs >= longTurnMs && has("long_turn")) return "long_turn";
    const auto now = clock_();
    const auto occasions = occasionsAt(now, birthday_);
    if (occasions.contains("birthday") && has("birthday") && cheeredOn_ != now.date()) {
        cheeredOn_ = now.date(); return "birthday";
    }
    if (occasions.contains("friday_evening") && has("friday_evening")) return "friday_evening";
    return {};
}
bool EasterEggs::surprise(const QString &pool) {
    if (!enabled_ || player_.held() || player_.stopped()) return false;
    const auto state = draw(pool);
    if (state.isEmpty()) return false;
    surprise_ = state; // Before selecting: entering it must not count as something else showing.
    surpriseUntil_ = clock_().toMSecsSinceEpoch() + surpriseMs;
    player_.select(state, true);
    return true;
}
bool EasterEggs::surprising() const {
    return !surprise_.isEmpty() && player_.requestedState() == surprise_ && clock_().toMSecsSinceEpoch() < surpriseUntil_;
}
bool EasterEggs::bedtime() {
    const auto now = clock_();
    if (!enabled_ || bedtimeOn_ == now.date() || !occasionsAt(now, {}).contains("late_night")) return false;
    bedtimeOn_ = now.date();
    return true;
}
QStringList EasterEggs::remindersAt(const QDateTime &local) {
    QStringList due;
    const int day = local.date().dayOfWeek(), hour = local.time().hour();
    const int minutes = hour * 60 + local.time().minute();
    if (day == Qt::Monday && hour >= mondayFrom && hour < mondayUntil) due << "monday";
    if (day <= Qt::Friday && minutes >= leaveWorkAt && minutes < leaveWorkUntil) due << "leave_work";
    if (hour >= sleepFrom) due << "sleep";
    return due;
}
QString EasterEggs::reminderNote(const QString &reminder) {
    if (reminder == "monday") return "Monday again... I'm so tired. Let's take it slow today.";
    if (reminder == "leave_work") return "It's 4:45 PM. Time to wrap up and get ready to head home!";
    if (reminder == "sleep") return "It's 10 PM. Time to put everything down and go to sleep!";
    return {};
}
QString EasterEggs::reminder() {
    if (!enabled_) return {};
    const auto now = clock_();
    for (const auto &due : remindersAt(now)) {
        if (reminded_.value(due) == now.date()) continue;
        reminded_.insert(due, now.date());
        return due;
    }
    return {};
}
bool EasterEggs::key(int key) {
    static const QVector<int> code{Qt::Key_Up, Qt::Key_Up, Qt::Key_Down, Qt::Key_Down, Qt::Key_Left,
                                   Qt::Key_Right, Qt::Key_Left, Qt::Key_Right, Qt::Key_B, Qt::Key_A};
    keys_.append(key);
    if (keys_.size() > code.size()) keys_.removeFirst();
    if (keys_ != code) return false;
    keys_.clear();
    return true;
}
}
