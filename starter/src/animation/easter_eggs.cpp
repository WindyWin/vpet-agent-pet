#include "easter_eggs.h"
#include "stage.h"
#include "i18n/contexts.h"
#include "settings/preferences.h"

namespace pet {
EasterEggs::EasterEggs(Player &player, QObject *parent) : QObject(parent), player_(player) {}
void EasterEggs::setEnabled(bool enabled) { enabled_ = enabled; }
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
QString EasterEggs::fidget() {
    if (!enabled_) return {};
    const auto now = clock_();
    const auto occasions = occasionsAt(now, birthday_);
    if (greetedOn_ != now.date()) { greetedOn_ = now.date(); greeted_.clear(); }
    // Fidgets are silent: the rules of these triggers play a state and say nothing.
    QStringList days;
    for (const auto &[occasion, trigger] : {std::pair{"birthday", "birthday-greeting"}, std::pair{"may20", "may20"}})
        if (occasions.contains(occasion) && answers(trigger)) days << trigger;
    // The first fidgets of the day greet with each occasion in turn; afterwards they come up now and then.
    for (const auto &trigger : days)
        if (!greeted_.contains(trigger)) { greeted_.insert(trigger); return draw(trigger)->state; }
    for (const auto &trigger : days)
        if (random_(occasionOneIn) == 0) return draw(trigger)->state;
    if (occasions.contains("late-night") && answers("late-night") && random_(lateNightOneIn) == 0)
        return draw("late-night")->state;
    return {};
}
QString EasterEggs::celebration(qint64 turnMs) {
    if (!enabled_) return {};
    if (turnMs >= longTurnMs && answers("long-turn")) return "long-turn";
    const auto now = clock_();
    const auto occasions = occasionsAt(now, birthday_);
    if (occasions.contains("birthday") && answers("birthday") && cheeredOn_ != now.date()) return "birthday";
    if (occasions.contains("friday-evening") && answers("friday-evening")) return "friday-evening";
    return {};
}
bool EasterEggs::surprise(const QString &trigger, bool evenWhenOff) {
    if ((!enabled_ && !evenWhenOff) || !stage_) return false;
    const auto reaction = draw(trigger);
    if (!reaction || reaction->state.isEmpty()) return false;
    using namespace behavior;
    return stage_->runtime().submit({"eggs", trigger, trigger, Policy::Surprise, Lifetime::OneShot, 0, {}, reaction->state, reaction->say})
        == Submission::Admitted;
}
bool EasterEggs::surprising() const {
    const auto *showing = stage_ ? stage_->runtime().showing() : nullptr;
    return showing && showing->source == "eggs" && showing->policy == behavior::Policy::Surprise;
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
    if (minutes >= minuteOfDay(schedule.lunch) && minutes < minuteOfDay(schedule.lunch) + 75) due << "lunch";
    if (day <= Qt::Friday && minutes >= leave && minutes < leave + 75) due << "leave-work";
    if (minutes >= minuteOfDay(schedule.sleep)) due << "sleep";
    return due;
}
QString EasterEggs::reminderNote(const QString &reminder, const ReminderSchedule &schedule) {
    if (reminder == "monday") return Pet::tr("Monday again... I'm so tired. Let's take it slow today.");
    if (reminder == "lunch") return Pet::tr("It's %1. Time to step away, take a break, and enjoy some lunch! 🍱").arg(schedule.lunch.toString("HH:mm"));
    if (reminder == "leave-work") return Pet::tr("It's %1. Time to wrap up and get ready to head home!").arg(schedule.leaveWork.toString("HH:mm"));
    if (reminder == "sleep") return Pet::tr("It's %1. Time to put everything down and go to sleep!").arg(schedule.sleep.toString("HH:mm"));
    return {};
}
QStringList EasterEggs::dueReminders() const {
    QStringList due;
    if (!enabled_) return due;
    const auto now = clock_();
    for (const auto &reminder : remindersAt(now, schedule_))
        if (reminded_.value(reminder) != now.date()) due << reminder;
    return due;
}
QString EasterEggs::dueReminder() const {
    const auto due = dueReminders();
    return due.isEmpty() ? QString() : due.first();
}
void EasterEggs::reminded(const QString &reminder) { if (!reminder.isEmpty()) reminded_.insert(reminder, clock_().date()); }
QString EasterEggs::reminder() {
    const auto due = dueReminder();
    reminded(due);
    return due;
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
