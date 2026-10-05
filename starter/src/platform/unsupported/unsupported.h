#pragma once
#include "platform/contracts/command.h"
#include "platform/contracts/desktop.h"
#include "platform/contracts/process.h"

namespace pet::platform::unsupported {
// Explicitly unavailable services: every operation reports Unsupported or Unknown.
// For tests and for builds without a native implementation.
class Desktop : public DesktopBackend {
public:
    explicit Desktop(QString id = "unsupported", QString requirement = {}) : id_(std::move(id)), requirement_(std::move(requirement)) {}
    QString id() const override { return id_; }
    QString requirement() const override { return requirement_; }
    Outcome activate(const WindowRequest &) override { return Outcome::Unsupported; }
private:
    QString id_, requirement_;
};
class Commands : public CommandRunner {
public:
    Outcome run(const Command &, QByteArray * = nullptr) override { return Outcome::Unsupported; }
};
class Processes : public ProcessServices {
public:
    QVector<qint64> ancestors(qint64, int = 16) const override { return {}; }
    QVector<ProcessInfo> terminalClients(const QString &) const override { return {}; }
};
}
