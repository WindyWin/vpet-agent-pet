#include "desktop/pet_window.h"
#include <QTest>

class PrototypeTests : public QObject {
    Q_OBJECT
private slots:
    void playbackUsesBundledTiming() {
        pet::Player player;
        QVERIFY(!player.pixmap().isNull());
        QVERIFY(player.pixmap().hasAlphaChannel());
        QCOMPARE(player.frameDuration(), 250);
        QTRY_COMPARE_WITH_TIMEOUT(player.frameIndex(), 1, 400);
        QCOMPARE(player.frameDuration(), 125);
        player.select("thinking");
        QCOMPARE(player.frameIndex(), 0);
        QVERIFY(!player.pixmap().isNull());
        player.select("idle");
        QCOMPARE(player.frameIndex(), 0);
        QVERIFY_EXCEPTION_THROWN(player.select("unknown"), std::invalid_argument);
        QCOMPARE(player.state(), QString("idle"));
    }
    void controlsAndRecovery() {
        pet::PetWindow window;
        QVERIFY(window.testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
        window.setPetSize(320);
        QCOMPARE(window.size(), QSize(320, 320));
        window.setPetSize(1);
        QCOMPARE(window.width(), 160);
        window.setClickThrough(true);
        QVERIFY(window.windowFlags().testFlag(Qt::WindowTransparentForInput));
        window.recover();
        QVERIFY(!window.clickThrough());
        QVERIFY(!window.windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(window.isVisible());
        window.setOnTop(false);
        QVERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    }
};
QTEST_MAIN(PrototypeTests)
#include "prototype_tests.moc"
