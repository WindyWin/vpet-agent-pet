#include "updates/controller.h"
#include <QSaveFile>
#include <QSslSocket>
#include <QRegularExpression>
#include "desktop/monitor.h"
#include "ipc/autostart.h"
#include "ipc/local.h"
#include "platform/native.h"
#include "platform/headless.h"
#include "providers/integrations.h"
#include "version.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <exception>
#include <cstdio>
#include <memory>

// An autostarted pet that lost the single-instance race hands its event to the
// winner. The winner holds the lock just before binding, so retry briefly.
static void forwardLaunchEvent(const QByteArray &data) {
    QElapsedTimer elapsed; elapsed.start();
    QString error;
    while (!pet::sendEvent(data, error) && elapsed.elapsed() < 2000) QThread::msleep(50);
}

int main(int argc, char **argv) {
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--check-update-runtime") {
        QCoreApplication app(argc, argv);
        const bool available = QSslSocket::supportsSsl();
        std::printf("Update HTTPS runtime: %s\n", available ? "available" : "unavailable");
        return available ? 0 : 1;
    }
    // Answer without a display so install scripts can report versions headlessly.
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--version") {
        std::printf("agent-pet %s (%s)\n", AGENT_PET_VERSION, AGENT_PET_REVISION);
        return 0;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "integration") {
        QCoreApplication app(argc, argv);
        return pet::integrationCommand(app.arguments());
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "autostart") {
        QCoreApplication app(argc, argv);
        app.setApplicationName("agent-pet"); // Selects the preferences directory.
        return pet::autostartCommand(app.arguments());
    }
    if (argc > 1 && (QString::fromLocal8Bit(argv[1]) == "hook" || QString::fromLocal8Bit(argv[1]) == "emit")) {
        // Hooks run inside agent clients and must stay silent; Qt diagnostics
        // (for example the non-UTF-8 locale warning) would leak into them.
        if (QString::fromLocal8Bit(argv[1]) == "hook")
            qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
        QCoreApplication app(argc, argv);
        app.setApplicationName("agent-pet");
        return pet::eventCommand(app.arguments());
    }
    {
        QCoreApplication startup(argc, argv); startup.setApplicationName("agent-pet");
        if (pet::updates::prepareStartup(startup.arguments())) return 0;
    }
    pet::platform::bootstrapGui();
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("agent-pet");
    app.setApplicationVersion(AGENT_PET_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Agent Pet animation and desktop controls");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"state", "Initial animation state (default: starting)", "state", "starting"});
    parser.addOption({"preview", "Open the developer animation preview"});
    parser.addOption({"settings", "Open desktop settings"});
    parser.addOption({"no-persist", "Do not read or write preferences (testing)"});
    parser.addOption({"smoke-test", "Exercise playback, input recovery and shutdown; exit after about 20 seconds"});
    // Used by `agent-pet hook` when autostart is enabled; not meant to be typed.
    parser.addOption({"autostarted", "Launched by an agent hook: exit silently if a pet is already running"});
    parser.addOption({"launch-event", "Normalized event to apply once listening", "json"});
    parser.addOption({"update-health", "Internal update startup acknowledgement", "token"});
    parser.process(app);
    const bool autostarted = parser.isSet("autostarted");
    // The launch event is validated like any datagram; an invalid one is dropped.
    const auto launchData = parser.value("launch-event").toUtf8();
    pet::Event launchEvent; QString launchError;
    const bool hasLaunchEvent = parser.isSet("launch-event") && pet::Event::parse(launchData, launchEvent, launchError);
    try {
        // Take the single-instance lock before any window or tray icon appears.
        std::unique_ptr<pet::Receiver> receiver;
        if (!parser.isSet("smoke-test")) {
            receiver = std::make_unique<pet::Receiver>();
            QString receiverError;
            if (!receiver->start(receiverError)) {
                if (!autostarted) { std::fprintf(stderr, "%s\n", qPrintable(receiverError)); return 1; }
                if (hasLaunchEvent) forwardLaunchEvent(launchData);
                return 0;
            }
        }
        pet::PetWindow window(nullptr, {}, !parser.isSet("smoke-test") && !parser.isSet("no-persist"));
        if (!window.player().select(parser.value("state"), true)) {
            std::fprintf(stderr, "%s\n", qPrintable(window.player().error()));
            return 1;
        }
        pet::Monitor monitor(window, pet::platform::createFocusService());
        monitor.locked = pet::platform::createScreenLockQuery(&app); // The application outlives the monitor.
        if (receiver) monitor.listen(std::move(receiver));
        std::unique_ptr<pet::updates::Controller> updates;
        if (!parser.isSet("smoke-test") && !parser.isSet("no-persist")) {
            updates = std::make_unique<pet::updates::Controller>();
            // Only a pending approval or input request is lost by a restart; working and idle
            // sessions reappear with their next hook event.
            updates->sessionsActive = [&monitor] { return monitor.sessions().unresolvedAttention() > 0; };
            window.setUpdates(updates.get());
            QObject::connect(updates.get(), &pet::updates::Controller::restartRequested, &window, &pet::PetWindow::requestQuit);
            updates->start();
        }
        window.show();
        const QString health = parser.value("update-health");
        if (QRegularExpression("^[0-9a-f-]{36}$").match(health).hasMatch()) {
            QTimer::singleShot(2000, &window, [&window, health] {
                if (window.quitting() || !window.player().error().isEmpty()) return;
                QSaveFile file(pet::updates::dataDirectory() + "/health-" + health);
                if (file.open(QIODevice::WriteOnly)) { file.write("ready"); file.commit(); }
            });
        }
        if (hasLaunchEvent) monitor.apply(launchEvent, QDateTime::currentMSecsSinceEpoch());
        if (parser.isSet("preview")) window.showPreview();
        if (parser.isSet("settings")) window.showSettings();
        if (parser.isSet("smoke-test")) {
            std::printf("Platform: %s; Qt: %s\n", qPrintable(app.platformName()), qVersion());
            std::fflush(stdout);
            QTimer::singleShot(500, &window, [&] { window.player().select("thinking"); window.setPetSize(320); });
            QTimer::singleShot(1000, &window, [&] { window.setOnTop(false); window.setClickThrough(true); });
            // Coarse timers may fire up to 5% early or late: the 15-second recovery
            // armed at 1 s can land as late as ~17 s, so check precisely after that.
            QTimer::singleShot(18000, Qt::PreciseTimer, &window, [&] {
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
