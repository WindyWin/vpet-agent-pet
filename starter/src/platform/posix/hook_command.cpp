#include "platform/contracts/hook_command.h"
#include <QRegularExpression>
namespace pet {
QString hookCommand(const QString &executable, const QString &provider) {
    auto escaped = executable;
    escaped.replace("'", "'\\''");
    return "'" + escaped + "' hook --provider " + provider + " --registration agent-pet-v1";
}
bool ownedHookHandler(const QJsonObject &handler, const QString &provider) {
    // Match our entire shell command grammar, not an executable basename or a
    // substring that could accidentally claim someone else's hook.
    static const QRegularExpression command(R"(^'(?:[^']|'\\'')*' hook --provider (claude|codex) --registration agent-pet-v1$)");
    const auto match = command.match(handler.value("command").toString());
    return handler.value("type").toString() == "command" && match.hasMatch() && match.captured(1) == provider;
}
}
