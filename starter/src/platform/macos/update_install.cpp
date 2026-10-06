#include "platform/contracts/update_install.h"
#include "i18n/contexts.h"
namespace pet::updates {
// Never reached while managedInstallPrefix() is empty; replacing a signed application bundle needs its own design.
static bool unsupported(QString &error) { error = Updater::tr("Automatic installation is not available on macOS."); return false; }
bool extractArchive(const QString &, const QString &, QString &error) { return unsupported(error); }
bool exchangeDirectories(const QString &, const QString &, QString &error) { return unsupported(error); }
bool recoverInstallation(const QString &, QString &) { return true; } // No update ever starts.
}
