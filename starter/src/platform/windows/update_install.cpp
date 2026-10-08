#include "platform/contracts/update_install.h"
#include "i18n/contexts.h"
namespace pet::updates {
// Archive extraction and directory exchange are unused: the Windows helper runs verified Setup upgrades.
static bool unsupported(QString &error) { error = Updater::tr("Automatic installation is not available on Windows."); return false; }
bool extractArchive(const QString &, const QString &, QString &error) { return unsupported(error); }
bool exchangeDirectories(const QString &, const QString &, QString &error) { return unsupported(error); }
bool recoverInstallation(const QString &, QString &) { return true; } // No update ever starts.
}
