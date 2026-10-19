#pragma once
#include "platform/contracts/hook_command.h"
#include <QJsonObject>
#include <QStringList>
namespace pet {

bool mergeIntegration(const QJsonObject &input, const QString &provider, const QString &executable,
                      bool enable, QJsonObject &output, int &owned, QString &error);
// Default hook configuration or managed plugin file; honors provider directory overrides.
QString integrationConfigPath(const QString &provider);
// `interactions` selects the opt-in Claude Code interaction hooks (docs/adr/0049-claude-interaction-hooks.md) instead of the
// content-free status hooks. The two registrations are independent: each operation touches only its own entries.
struct IntegrationOptions {
    bool interactions = false;
    int questionWait = 20; // Seconds a Claude Code question waits for the pet before the terminal dialog opens.
};
// preview|inspect|enable|disable shared by the command line and settings window.
// An empty path selects integrationConfigPath(provider). Hooks run hookExecutable(application).
bool runIntegration(const QString &operation, const QString &provider, QString path, const QString &application,
                    QJsonObject &report, QString &error, const IntegrationOptions &options = {});
int integrationCommand(const QStringList &args);
}
