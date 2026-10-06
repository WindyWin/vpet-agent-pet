#include "i18n/contexts.h"
#include "i18n/language.h"
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

using pet::i18n::Language;

class I18nTests : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { pet::i18n::install(Language::English); }
    void names() {
        QCOMPARE(pet::i18n::fromName("en"), Language::English);
        QCOMPARE(pet::i18n::fromName("vi"), Language::Vietnamese);
        for (const auto &other : {"auto", "", "fr", "VI", "vi_VN"}) QCOMPARE(pet::i18n::fromName(other), Language::Auto);
        for (const auto language : {Language::Auto, Language::English, Language::Vietnamese})
            QCOMPARE(pet::i18n::fromName(pet::i18n::name(language)), language);
    }
    void resolve() {
        QCOMPARE(pet::i18n::resolve(Language::Auto, QLocale("vi_VN")), Language::Vietnamese);
        QCOMPARE(pet::i18n::resolve(Language::Auto, QLocale("en_US")), Language::English);
        QCOMPARE(pet::i18n::resolve(Language::Auto, QLocale("fr_FR")), Language::English); // Not translated.
        QCOMPARE(pet::i18n::resolve(Language::Auto, QLocale::c()), Language::English);
        // A choice wins over the system.
        QCOMPARE(pet::i18n::resolve(Language::English, QLocale("vi_VN")), Language::English);
        QCOMPARE(pet::i18n::resolve(Language::Vietnamese, QLocale("en_US")), Language::Vietnamese);
    }
    void installSwitchesEveryContext() {
        QCOMPARE(Alerts::tr("Needs approval"), QString("Needs approval")); // No translator: the source.
        QVERIFY(pet::i18n::install(Language::Vietnamese));
        QCOMPARE(pet::i18n::installed(), Language::Vietnamese);
        QCOMPARE(Alerts::tr("Needs approval"), QString("Cần phê duyệt"));
        QCOMPARE(Pet::tr("Time for some water 💧"), QString("Uống chút nước nào bạn ơi 💧"));
        QCOMPARE(QCoreApplication::translate("pet::PetWindow", "Settings…"), QString("Cài đặt…"));
        QVERIFY(pet::i18n::install(Language::Vietnamese)); // Again: no change.
        QVERIFY(pet::i18n::install(Language::English));
        QCOMPARE(pet::i18n::installed(), Language::English);
        QCOMPARE(Alerts::tr("Needs approval"), QString("Needs approval"));
    }
    // What Linguist would flag but lrelease does not: a finished translation must keep every %1…%9 and %n
    // of its source and at most one & mnemonic.
    void translationsKeepPlaceMarkers() {
        QFile file(TS_PATH); QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QString context, source, translation; bool finished = true; int checked = 0;
        const QRegularExpression marker("%(n|[1-9])");
        const auto markers = [&](const QString &text) {
            QStringList found;
            for (auto it = marker.globalMatch(text); it.hasNext();) found << it.next().captured();
            found.sort(); return found;
        };
        const auto mnemonics = [](QString text) { return text.remove("&&").count('&'); };
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"name") context = xml.readElementText();
            else if (xml.isStartElement() && xml.name() == u"source") source = xml.readElementText();
            else if (xml.isStartElement() && xml.name() == u"translation") {
                const auto type = xml.attributes().value("type").toString();
                finished = type.isEmpty();
                translation = xml.readElementText();
            } else if (xml.isEndElement() && xml.name() == u"message" && finished) {
                const auto where = (context + ": " + source).toUtf8(); // Kept alive for the messages.
                QVERIFY2(!translation.isEmpty(), where.constData());
                QVERIFY2(markers(translation) == markers(source), where.constData());
                QVERIFY2(mnemonics(translation) <= 1 && (mnemonics(source) == 0) == (mnemonics(translation) == 0), where.constData());
                ++checked;
            }
        }
        QVERIFY(!xml.hasError());
        QVERIFY(checked > 250);
    }
};
QTEST_GUILESS_MAIN(I18nTests)
#include "i18n_tests.moc"
