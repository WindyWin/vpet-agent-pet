#pragma once
#include "platform/contracts/hook_command.h"
#include <QJsonObject>
#include <QStringList>
namespace pet {

bool mergeIntegration(const QJsonObject &input, const QString &provider, const QString &executable,
                      bool enable, QJsonObject &output, int &owned, QString &error);
// Default client configuration file, honoring CLAUDE_CONFIG_DIR and CODEX_HOME.
QString integrationConfigPath(const QString &provider);
// preview|inspect|enable|disable shared by the command line and settings window.
// An empty path selects integrationConfigPath(provider).
bool runIntegration(const QString &operation, const QString &provider, QString path, const QString &executable,
                    QJsonObject &report, QString &error);
int integrationCommand(const QStringList &args);
}
