#include "platform/contracts/update_install.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <archive.h>
#include <archive_entry.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
namespace pet::updates {
bool extractArchive(const QString &path, const QString &destination, QString &error) {
    archive *reader = archive_read_new();
    archive_read_support_filter_gzip(reader); archive_read_support_format_tar(reader);
    auto fail = [&](QString message) { error = message; archive_read_free(reader); return false; };
    if (archive_read_open_filename(reader, QFile::encodeName(path).constData(), 65536) != ARCHIVE_OK)
        return fail("Cannot open package archive.");
    archive_entry *entry = nullptr;
    QString root;
    qint64 total = 0; int count = 0, status;
    while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        QString name = QString::fromUtf8(archive_entry_pathname(entry));
        while (name.endsWith('/')) name.chop(1);
        const auto parts = name.split('/');
        if (++count > 20000 || name.startsWith('/') || parts.contains("..") || parts.contains(".") || parts.contains("")
            || name.contains('\\') || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
            return fail("Package contains an unsafe path or link.");
        if (root.isEmpty()) root = parts.first();
        if (parts.first() != root) return fail("Package must have one root directory.");
        const auto type = archive_entry_filetype(entry);
        if (type != AE_IFDIR && type != AE_IFREG) return fail("Package contains a special file.");
        if (parts.size() == 1) { if (type != AE_IFDIR) return fail("Invalid package root."); continue; }
        const QString relative = parts.mid(1).join('/');
        if (relative == ".agent-pet-install" || relative == ".agent-pet-update-id") return fail("Reserved package path.");
        const QString target = destination + '/' + relative;
        if (type == AE_IFDIR) { if (!QDir().mkpath(target)) return fail("Cannot create package directory."); continue; }
        const qint64 size = archive_entry_size(entry);
        if (size < 0 || size > 1024LL * 1024 * 1024 || (total += size) > 2LL * 1024 * 1024 * 1024)
            return fail("Unpacked package is too large.");
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) return fail("Cannot create package directory.");
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Duplicate package file or insufficient disk space.");
        char buffer[65536]; la_ssize_t n; qint64 written = 0;
        while ((n = archive_read_data(reader, buffer, sizeof buffer)) > 0) {
            written += n;
            if (written > size || file.write(buffer, n) != n) return fail("Cannot write package; check free disk space.");
        }
        if (n < 0 || written != size || !file.flush()) return fail("Truncated package or disk write failure.");
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther
            | ((archive_entry_perm(entry) & 0111) ? QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther : QFile::Permissions{}));
    }
    if (status != ARCHIVE_EOF || root.isEmpty()) return fail("Invalid or incomplete package archive.");
    archive_read_free(reader); return true;
}
bool exchangeDirectories(const QString &first, const QString &second, QString &error) {
    if (syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(first).constData(), AT_FDCWD,
                QFile::encodeName(second).constData(), RENAME_EXCHANGE) == 0) return true;
    error = "Cannot switch update directories: " + QString::fromLocal8Bit(strerror(errno)); return false;
}
bool recoverInstallation(const QString &prefix, QString &error) {
    QFile journal(prefix + ".update-transaction.json");
    if (!journal.exists()) return true;
    if (!journal.open(QIODevice::ReadOnly)) { error = "Cannot read update recovery record."; return false; }
    const auto object = QJsonDocument::fromJson(journal.readAll()).object(); journal.close();
    const QString stage = object["stage"].toString(), token = object["token"].toString();
    if (!stage.startsWith(prefix + ".update-") || QFileInfo(stage).absolutePath() != QFileInfo(prefix).absolutePath()
        || token.isEmpty()) { error = "Invalid update recovery record."; return false; }
    QFile marker(prefix + "/.agent-pet-update-id");
    const bool exchanged = marker.open(QIODevice::ReadOnly) && marker.readAll() == token.toUtf8();
    marker.close();
    if (exchanged && !exchangeDirectories(prefix, stage, error)) return false;
    if (!QDir(stage).removeRecursively()) { error = "Could not remove update staging directory."; return false; }
    if (!journal.remove()) { error = "Could not remove update recovery record."; return false; }
    return true;
}
}
