#pragma once
#include <QLocale>
#include <QString>

namespace pet::i18n {
// The interface language. English is the source language, so it needs no translation file.
enum class Language { Auto, English, Vietnamese };
// The preferences.json spelling: "auto", "en" or "vi". Anything else reads as Auto.
Language fromName(const QString &name);
QString name(Language language);
// Auto follows the system's preferred UI languages in order; a language without a translation is English.
Language resolve(Language preference, const QLocale &system = QLocale::system());
// Replaces the application's translation with the one for `language` (resolved first if Auto); English
// removes it. Widgets receive QEvent::LanguageChange. Needs a QCoreApplication. False when the embedded
// translation cannot be loaded, and the text stays English.
bool install(Language language);
Language installed(); // English or Vietnamese: what install() put in place.
}
