#include "easter_eggs.h"
#include "i18n/contexts.h"
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
    if (hour >= lateNightFrom && hour < lateNightUntil) occasions << "late-night";
    if (date.dayOfWeek() == Qt::Friday && hour >= fridayEveningFrom) occasions << "friday-evening";
    return occasions;
}
QString EasterEggs::draw(const QString &cue) { return drawReaction(player_.pool(cue), random_); }
QString EasterEggs::fidget() {
    if (!enabled_) return {};
    const auto now = clock_();
    const auto occasions = occasionsAt(now, birthday_);
    if (greetedOn_ != now.date()) { greetedOn_ = now.date(); greeted_.clear(); }
    QStringList days;
    for (const QString occasion : {"birthday", "may20"})
        if (occasions.contains(occasion) && !player_.pool(occasion).isEmpty()) days << occasion;
    // The first fidgets of the day greet with each occasion in turn; afterwards they come up now and then.
    for (const auto &occasion : days)
        if (!greeted_.contains(occasion)) { greeted_.insert(occasion); return draw(occasion); }
    for (const auto &occasion : days)
        if (random_(occasionOneIn) == 0) return draw(occasion);
    if (occasions.contains("late-night") && !player_.pool("late-night").isEmpty() && random_(lateNightOneIn) == 0)
        return draw("late-night");
    return {};
}
QString EasterEggs::celebration(qint64 turnMs) {
    if (!enabled_) return {};
    auto has = [this](const QString &cue) { return !player_.pool(cue).isEmpty(); };
    if (turnMs >= longTurnMs && has("long-turn")) return "long-turn";
    const auto now = clock_();
    const auto occasions = occasionsAt(now, birthday_);
    if (occasions.contains("birthday") && has("birthday") && cheeredOn_ != now.date()) {
        cheeredOn_ = now.date(); return "birthday";
    }
    if (occasions.contains("friday-evening") && has("friday-evening")) return "friday-evening";
    return {};
}
bool EasterEggs::surprise(const QString &cue, bool evenWhenOff) {
    if ((!enabled_ && !evenWhenOff) || player_.held() || player_.stopped()) return false;
    const auto state = draw(cue);
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
    if (!enabled_ || bedtimeOn_ == now.date() || !occasionsAt(now, {}).contains("late-night")) return false;
    bedtimeOn_ = now.date();
    return true;
}
QStringList EasterEggs::remindersAt(const QDateTime &local, const ReminderSchedule &schedule) {
    QStringList due;
    const int day = local.date().dayOfWeek(), hour = local.time().hour();
    const int minutes = hour * 60 + local.time().minute();
    const auto minuteOfDay = [](QTime time) { return time.hour() * 60 + time.minute(); };
    const int monday = minuteOfDay(schedule.monday), leave = minuteOfDay(schedule.leaveWork);
    // Keep the original reminder windows, shifted with the configured start and ending at midnight.
    if (day == Qt::Monday && minutes >= monday && minutes < monday + 6 * 60) due << "monday";
    if (day <= Qt::Friday && minutes >= leave && minutes < leave + 75) due << "leave-work";
    if (minutes >= minuteOfDay(schedule.sleep)) due << "sleep";
    return due;
}
QString EasterEggs::reminderNote(const QString &reminder, const ReminderSchedule &schedule) {
    if (reminder == "monday") return Pet::tr("Monday again... I'm so tired. Let's take it slow today.");
    if (reminder == "leave-work") return Pet::tr("It's %1. Time to wrap up and get ready to head home!").arg(schedule.leaveWork.toString("HH:mm"));
    if (reminder == "sleep") return Pet::tr("It's %1. Time to put everything down and go to sleep!").arg(schedule.sleep.toString("HH:mm"));
    return {};
}
QString EasterEggs::reminder() {
    if (!enabled_) return {};
    const auto now = clock_();
    for (const auto &due : remindersAt(now, schedule_)) {
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
