#include "language.h"
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QTranslator>

namespace pet::i18n {
static QPointer<QTranslator> translator;
static Language active = Language::English;

Language fromName(const QString &name) {
    return name == "en" ? Language::English : name == "vi" ? Language::Vietnamese : Language::Auto;
}
QString name(Language language) {
    return language == Language::English ? "en" : language == Language::Vietnamese ? "vi" : "auto";
}
Language resolve(Language preference, const QLocale &system) {
    if (preference != Language::Auto) return preference;
    // "vi-VN", "en-US", ...; the first one we have a translation for wins.
    for (const auto &language : system.uiLanguages()) {
        if (language.startsWith("vi")) return Language::Vietnamese;
        if (language.startsWith("en")) return Language::English;
    }
    return system.language() == QLocale::Vietnamese ? Language::Vietnamese : Language::English;
}
bool install(Language language) {
    language = resolve(language);
    if (language == active && (language == Language::English || translator)) return true;
    if (translator) { QCoreApplication::removeTranslator(translator); delete translator; }
    active = Language::English;
    if (language == Language::English) return true;
    auto *loaded = new QTranslator(QCoreApplication::instance());
    if (!loaded->load(":/i18n/agent-pet_vi.qm") || !QCoreApplication::installTranslator(loaded)) {
        qWarning() << "Cannot load the Vietnamese translation; the interface stays in English.";
        delete loaded; return false;
    }
    translator = loaded; active = language;
    return true;
}
Language installed() { return active; }
}
