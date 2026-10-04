#include "desktop/pet_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <exception>
#include <cstdio>

int main(int argc, char **argv) {
    // XWayland is the prototype default; native Wayland is opt-in for testing.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("agent-pet");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Agent Pet M1 desktop prototype");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"state", "Initial preview: idle or thinking", "state", "idle"});
    parser.addOption({"smoke-test", "Exercise both states and controls, then exit after 17 seconds"});
    parser.process(app);
    try {
        pet::PetWindow window;
        window.player().select(parser.value("state"));
        window.show();
        if (parser.isSet("smoke-test")) {
            std::printf("Platform: %s; Qt: %s\n", qPrintable(app.platformName()), qVersion());
            std::fflush(stdout);
            QTimer::singleShot(500, &window, [&] { window.player().select("thinking"); window.setPetSize(320); });
            QTimer::singleShot(1000, &window, [&] { window.setOnTop(false); window.setClickThrough(true); });
            QTimer::singleShot(17000, &window, [&] {
                const bool recovered = !window.clickThrough();
                window.player().select("idle");
                window.setOnTop(true);
                window.recover();
                std::printf("Idle/thinking decoded; automatic input recovery: %s\n", recovered ? "passed" : "FAILED");
                app.exit(recovered ? 0 : 1);
            });
        }
        return app.exec();
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
}
