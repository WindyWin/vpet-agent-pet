#include "packs/steam_workshop.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace pet::packs;
class SteamWorkshopTests : public QObject {
    Q_OBJECT
private slots:
    void libraries() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto root = temp.path() + "/Steam", other = temp.path() + "/Library with spaces";
        QVERIFY(QDir().mkpath(root + "/steamapps/workshop/content/1920960/123"));
        QVERIFY(QDir().mkpath(root + "/steamapps/workshop/content/1920960/not-an-item"));
        QVERIFY(QDir().mkpath(root + "/steamapps/workshop/content/999/456"));
        QVERIFY(QDir().mkpath(other + "/steamapps/workshop/content/1920960/789"));
        QFile file(root + "/steamapps/libraryfolders.vdf"); QVERIFY(file.open(QIODevice::WriteOnly));
        auto escaped = other; escaped.replace('\\', "\\\\");
        file.write(("\"libraryfolders\" { // comment\n \"0\" { \"path\" \"" + root + "\" \"label\" \"\" \"apps\" { \"1920960\" \"1\" } } \"1\" { \"path\" \"" + escaped + "\" } }").toUtf8()); file.close();
        auto items = steamWorkshopItems({root, root, "", temp.path() + "/absent"}); QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].id, QString("123")); QCOMPARE(items[1].id, QString("789"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(("\"LibraryFolders\" { \"1\" \"" + escaped + "\" }").toUtf8()); file.close();
        QCOMPARE(steamWorkshopItems({root}).size(), 2);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("\"libraryfolders\" { \"1\" {"); file.close();
        QCOMPARE(steamWorkshopItems({root}).size(), 1);
    }
};
QTEST_GUILESS_MAIN(SteamWorkshopTests)
#include "steam_workshop_tests.moc"
