#include "components.h"
#include "installer.h"
#include "release.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
namespace pet::updates {
namespace {
bool safePath(const QString &path) {
    const auto parts = path.split('/');
    return !path.isEmpty() && path.size() <= 4096 && !path.contains(QChar::Null)
        && !path.contains('\\') && !path.contains(':') && !parts.contains("")
        && !parts.contains(".") && !parts.contains("..")
        && !path.startsWith(".agent-pet-");
}
bool validDigest(const QString &digest) {
    return QRegularExpression("^sha256:[0-9a-f]{64}$").match(digest).hasMatch();
}
bool regularFile(const QString &prefix, const QString &path) {
    QString current = prefix;
    const auto parts = path.split('/');
    for (int i = 0; i < parts.size(); ++i) {
        current += '/' + parts[i];
        const QFileInfo info(current);
        if (info.isSymLink() || (i + 1 < parts.size() ? !info.isDir() : !info.isFile())) return false;
    }
    return true;
}
bool matches(const ComponentFile &file, const QString &prefix) {
    const QString path = prefix + '/' + file.path;
    const QFileInfo info(path);
    QString error;
    const bool executable = bool(info.permissions() & (QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther));
    return regularFile(prefix, file.path) && info.size() == file.size && executable == file.executable
        && verifiedFile(path, file.digest, MaxArchive * 2, error);
}
}
bool readComponents(const QString &path, const QString &digest, const QString &version,
                    const QString &architecture, Components &result, QString &error) {
    result = {};
    auto invalid = [&] { result = {}; error = "Invalid update component manifest."; return false; };
    if (!verifiedFile(path, digest, MaxComponentsManifest, error)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return invalid();
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto object = document.object();
    const int format = object["format"].toInt();
    if ((format != 1 && format != 2) || object["version"].toString() != version
        || object["architecture"].toString() != architecture) return invalid();
    const auto entries = object["components"].toArray();
    if ((format == 1 && entries.size() != 3) || entries.size() < 3 || entries.size() > 4096) return invalid();
    QSet<QString> names, paths;
    qint64 total = 0;
    for (const auto &value : entries) {
        const auto item = value.toObject();
        Component component;
        component.name = item["name"].toString(); component.archive = item["archive"].toString();
        component.digest = item["digest"].toString(); component.size = item["size"].toInteger(-1);
        const bool artworkPack = format == 2 && QRegularExpression("^artwork-[0-9a-f]{64}$").match(component.name).hasMatch();
        if ((!QStringList{"app", "runtime", "artwork"}.contains(component.name) && !artworkPack) || names.contains(component.name)
            || component.archive != "agent-pet-" + version + "-linux-" + architecture + '-' + component.name + ".tar.gz"
            || !validDigest(component.digest) || component.size <= 0 || component.size > MaxArchive) return invalid();
        names.insert(component.name);
        for (const auto &fileValue : item["files"].toArray()) {
            const auto entry = fileValue.toObject();
            ComponentFile content{entry["path"].toString(), entry["digest"].toString(), entry["size"].toInteger(-1), entry["executable"].toBool()};
            if (!safePath(content.path) || paths.contains(content.path) || !validDigest(content.digest)
                || !entry["executable"].isBool() || content.size < 0 || content.size > MaxArchive * 2
                || (total += content.size) > 2LL * 1024 * 1024 * 1024 || paths.size() >= 20000) return invalid();
            paths.insert(content.path); component.files.append(content);
        }
        if (component.files.isEmpty()) return invalid();
        if (artworkPack && (component.files.size() != 1
            || component.files.first().path != "share/agent-pet/" + component.name + ".rcc")) return invalid();
        result.entries.append(component);
    }
    // A complete target, including the helper for the following update.
    if (!names.contains("app") || !names.contains("runtime") || !names.contains("artwork")
        || !paths.contains("bin/agent-pet") || !paths.contains("bin/agent-pet-updater")
        || !paths.contains("share/agent-pet/artwork.rcc")) return invalid();
    // A file cannot also be another file's parent directory.
    for (const auto &path : paths) {
        QString parent = path;
        while (parent.contains('/')) {
            parent.truncate(parent.lastIndexOf('/'));
            if (paths.contains(parent)) return invalid();
        }
    }
    return true;
}
bool componentMatches(const Component &component, const QString &prefix) {
    for (const auto &file : component.files) if (!matches(file, prefix)) return false;
    return true;
}
bool componentsAvailable(const Components &components, const QString &prefix, const QString &cache) {
    for (const auto &component : components.entries) {
        QString error;
        if (!componentMatches(component, prefix)
            && !verifiedArchive(cache + '/' + component.archive, component.digest, error)) return false;
    }
    return true;
}
bool assembleComponents(const Components &components, const QString &prefix, const QString &cache,
                        const QString &destination, QString &error) {
    QSet<QString> expected;
    for (const auto &component : components.entries) {
        for (const auto &file : component.files) expected.insert(file.path);
        if (componentMatches(component, prefix)) {
            for (const auto &file : component.files) {
                const QString target = destination + '/' + file.path;
                // Independent copies keep rollback files immutable when the new app runs.
                if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(prefix + '/' + file.path, target)) {
                    error = "Cannot reuse installed files; check free disk space."; return false;
                }
            }
        } else {
            const QString archive = cache + '/' + component.archive;
            if (!verifiedArchive(archive, component.digest, error) || !extractArchive(archive, destination, error)) return false;
        }
    }
    // Verify the final tree, including copied bytes, before executing anything.
    for (const auto &component : components.entries) {
        if (!componentMatches(component, destination)) { error = "Update files do not match the component manifest."; return false; }
    }
    QDirIterator files(destination, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        if (!expected.contains(QDir(destination).relativeFilePath(files.next()))) {
            error = "Update contains an undeclared file."; return false;
        }
    }
    return true;
}
}
