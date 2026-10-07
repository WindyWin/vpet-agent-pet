#include "local.h"
#include "autostart.h"
#include "providers/adapters.h"
#include "hosts/registry.h"
#include "platform/contracts/hook_input.h"
#include "platform/headless.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>
#include <cstdio>

namespace pet {
Receiver::Receiver(QObject *parent) : Receiver(platform::createEventTransport(), parent) {}
Receiver::Receiver(std::unique_ptr<platform::EventTransport> transport, QObject *parent)
    : QObject(parent), transport_(std::move(transport)) {
    transport_->received = [this](const QByteArray &data) {
        Event event; QString error;
        if (Event::parse(data, event, error) && received) received(event);
    };
}
Receiver::~Receiver() = default;
bool Receiver::start(QString &error) { return transport_->start(error); }
bool sendEvent(const QByteArray &data, QString &error) { return platform::sendDatagram(data, error); }
int eventCommand(const QStringList &args) {
    const bool hook = args.value(1) == "hook";
    QString error;
    auto fail = [&] { if (!hook) std::fprintf(stderr, "%s\n", qPrintable(error)); return hook ? 0 : 1; };
    QString provider;
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--provider" && i + 1 < args.size() && provider.isEmpty()) provider = args[++i];
        else if (hook && args[i] == "--registration" && args.value(i + 1) == "agent-pet-v1") ++i;
        else { error = "Usage: agent-pet hook --provider claude|codex, or agent-pet emit [--provider claude|codex]; hook reads provider JSON; emit reads normalized JSON"; return fail(); }
    }
    if ((hook && provider.isEmpty()) || (!provider.isEmpty() && provider != "claude" && provider != "codex")) {
        error = "Expected provider claude or codex"; return fail();
    }
    const int inputLimit = hook ? 1024 * 1024 : 8192;
    QByteArray data;
    if (!platform::readHookInput(inputLimit, data, error)) return fail();
    auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { error = "Expected normalized JSON event"; return fail(); }
    auto object = doc.object();
    if (hook) {
        object = normalizeHook(provider, object, QDateTime::currentMSecsSinceEpoch());
        if (object.isEmpty()) return 0;
        const auto processes = platform::createProcessServices();
        const auto ancestors = processes->ancestors(platform::parentProcessId());
        const auto host = hosts::toV1(hosts::Registry::builtin().capture(QProcessEnvironment::systemEnvironment(), ancestors,
                                                                         processes->names(ancestors)));
        for (auto it = host.begin(); it != host.end(); ++it) object[it.key()] = it.value();
    }
    if (!provider.isEmpty()) {
        if (object.contains("provider") && object.value("provider").toString() != provider) { error = "Provider mismatch"; return fail(); }
        object["provider"] = provider;
    }
    // A generated ID identifies this delivery only; adapter-level retries need a stable ID.
    if (!object.contains("event_id")) object["event_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!object.contains("timestamp_ms")) object["timestamp_ms"] = QDateTime::currentMSecsSinceEpoch();
    data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    Event event;
    if (!Event::parse(data, event, error)) return fail();
    if (!sendEvent(data, error)) {
        // No pet is listening. A session start may launch one, which applies this event.
        if (hook) autostartPet(data, event.kind, {}, QProcessEnvironment::systemEnvironment(),
                               petExecutable(), launchDetached);
        return fail();
    }
    return 0;
}
}
