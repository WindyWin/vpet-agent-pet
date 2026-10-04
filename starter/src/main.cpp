#include "desktop/pet_window.h"
#include "ipc/local.h"
#include "providers/integrations.h"
#include "desktop/session_playback.h"
#include <QDateTime>
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <exception>
#include <cstdio>

int main(int argc, char **argv) {
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "integration") {
        QCoreApplication app(argc, argv);
        return pet::integrationCommand(app.arguments());
    }
    if (argc > 1 && (QString::fromLocal8Bit(argv[1]) == "hook" || QString::fromLocal8Bit(argv[1]) == "emit")) {
        QCoreApplication app(argc, argv);
        return pet::eventCommand(app.arguments());
    }
    // XWayland is the prototype default; native Wayland is opt-in for testing.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("agent-pet");
    app.setApplicationVersion("0.4.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Agent Pet animation and desktop controls");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"state", "Initial animation state (default: starting)", "state", "starting"});
    parser.addOption({"preview", "Open the developer animation preview"});
    parser.addOption({"settings", "Open desktop settings"});
    parser.addOption({"no-persist", "Do not read or write preferences (testing)"});
    parser.addOption({"smoke-test", "Exercise playback, input recovery and shutdown; exit after about 19 seconds"});
    parser.process(app);
    try {
        pet::PetWindow window(nullptr, {}, !parser.isSet("smoke-test") && !parser.isSet("no-persist"));
        if (!window.player().select(parser.value("state"), true)) {
            std::fprintf(stderr, "%s\n", qPrintable(window.player().error()));
            return 1;
        }
        pet::Sessions sessions;
        pet::Receiver receiver;
        QString lastAggregate;
        bool observed = false;
        auto update = [&] {
            if (!observed || window.player().requestedState() == "closing") return;
            const auto now = QDateTime::currentMSecsSinceEpoch();
            sessions.expire(now);
            const auto state = sessions.aggregate(now);
            const auto animation = pet::sessionAnimation(state);
            if (state != lastAggregate || (!window.player().isDragging() && state != "error" && state != "turn-finished" && window.player().requestedState() != animation)) {
                window.player().select(animation, state == "attention" || state == "error");
                lastAggregate = state;
            }
        };
        receiver.received = [&](const pet::Event &event) {
            if (sessions.apply(event, QDateTime::currentMSecsSinceEpoch())) { observed = true; update(); }
        };
        QString receiverError;
        if (!parser.isSet("smoke-test") && !receiver.start(receiverError)) {
            std::fprintf(stderr, "%s\n", qPrintable(receiverError));
            return 1;
        }
        QTimer sessionTimer;
        sessionTimer.setInterval(250);
        QObject::connect(&sessionTimer, &QTimer::timeout, &window, update);
        sessionTimer.start();
        window.show();
        if (parser.isSet("preview")) window.showPreview();
        if (parser.isSet("settings")) window.showSettings();
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
                if (!recovered || !window.player().error().isEmpty()) app.exit(1);
                else window.requestQuit();
            });
        }
        return app.exec();
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
}
