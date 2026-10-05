#include "desktop/pet_window.h"
#include "desktop/host_focus.h"
#include <QApplication>
#include <QCursor>
#include <QSignalSpy>
#include <QTest>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

class DesktopTests : public QObject {
    Q_OBJECT
private slots:
    void waylandFocusRestoresWindow() {
        if (QGuiApplication::platformName() != "wayland") QSKIP("Requires native Wayland on KDE Plasma 6");
        QWidget target, other;
        target.setWindowTitle("agent-pet-focus-regression");
        other.setWindowTitle("other-test-window");
        target.show(); other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        target.showMinimized();
        QTest::qWait(200);
        pet::Session session;
        session.host = {"terminal", {QCoreApplication::applicationPid()}, {}, {}};
        session.project = "/tmp/agent-pet-focus-regression";
        QVERIFY(pet::hostFocus::focus(session));
        QTRY_VERIFY(target.isActiveWindow());
        QVERIFY(!target.isMinimized());
        session.host.pids = {2147483647};
        QVERIFY(!pet::hostFocus::focus(session));
    }
    void nativeDragReleasesAndRestores() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Requires X11/XWayland and XTest");
        struct Pointer {
            Display *display = XOpenDisplay(nullptr);
            QPoint original = QCursor::pos();
            ~Pointer() {
                if (!display) return;
                XTestFakeButtonEvent(display, 1, False, 0);
                XTestFakeMotionEvent(display, -1, original.x(), original.y(), 0);
                XFlush(display); XCloseDisplay(display);
            }
        } pointer;
        QVERIFY(pointer.display);
        pet::PetWindow window(nullptr, {}, false);
        window.move(200, 200); window.show(); window.raise();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.player().select("working", true);
        QTest::qWait(200);
        const auto before = window.nativePos();
        const auto center = window.mapToGlobal(window.rect().center());
        XTestFakeMotionEvent(pointer.display, -1, center.x(), center.y(), 0);
        XSync(pointer.display, False);
        QTest::qWait(200);
        XTestFakeButtonEvent(pointer.display, 1, True, 0); XFlush(pointer.display);
        QTest::qWait(200);
        QVERIFY(!window.player().isDragging()); // A press alone may still be a click.
        XTestFakeMotionEvent(pointer.display, -1, center.x() + 90, center.y() + 50, 0); XFlush(pointer.display);
        QTRY_VERIFY_WITH_TIMEOUT((window.nativePos() - before).manhattanLength() > 40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(window.player().isDragging(), 10000);
        QCOMPARE(window.player().state(), QString("dragging"));
        XTestFakeButtonEvent(pointer.display, 1, False, 0); XFlush(pointer.display);
        QTRY_VERIFY_WITH_TIMEOUT(!window.player().isDragging(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(window.player().state(), QString("working"), 4500);
        QVERIFY(window.player().error().isEmpty());
        window.showPreview();
        QTest::qWait(200);
        QVERIFY(window.grab().save("/tmp/agent-pet-m2-pet.png"));
        for (auto *dialog : window.findChildren<QDialog*>())
            if (dialog->windowTitle() == "Animation preview")
                QVERIFY(dialog->grab().save("/tmp/agent-pet-m2-preview.png"));
    }
};
QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
