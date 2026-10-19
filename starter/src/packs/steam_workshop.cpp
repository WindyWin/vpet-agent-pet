#include "steam_workshop.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
namespace pet::packs {
namespace {
// Bounded Valve KeyValues subset: quoted strings, braces, escapes and // comments.
class Vdf {
public:
    QString text; int at = 0, tokens = 0; bool valid = true;
    QString token() {
        while (at < text.size()) {
            if (text[at].isSpace()) { ++at; continue; }
            if (text.mid(at, 2) == "//") { while (at < text.size() && text[at] != '\n') ++at; continue; }
            break;
        }
        if (++tokens > 100000) { valid = false; return {}; }
        if (at == text.size()) return {};
        const auto c = text[at++]; if (c == '{' || c == '}') return QString(c);
        if (c != '"') { valid = false; return {}; }
        QString value = QStringLiteral("");
        while (at < text.size()) {
            const auto c = text[at++]; if (c == '"') return value;
            if (c == '\\' && at < text.size() && (text[at] == '\\' || text[at] == '"')) value += text[at++]; else value += c;
        }
        valid = false; return {};
    }
    QJsonObject object(int depth = 0) {
        QJsonObject result; if (depth > 16) { valid = false; return {}; }
        while (valid) {
            const auto key = token();
            if (key == "}") { if (!depth) valid = false; return result; }
            if (key.isEmpty()) { if (depth) valid = false; return result; }
            if (key == "{") { valid = false; break; }
            const auto value = token();
            if (value == "{") result[key.toLower()] = object(depth + 1);
            else if (value == "}" || value.isNull()) valid = false;
            else result[key.toLower()] = value;
        }
        return {};
    }
};
}
QStringList steamRoots() {
    const auto home = QDir::homePath();
#ifdef Q_OS_WIN
    QSettings user("HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat);
    QSettings machine("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam", QSettings::NativeFormat);
    return {user.value("SteamPath").toString(), machine.value("InstallPath").toString(), qEnvironmentVariable("ProgramFiles(x86)") + "/Steam"};
#elif defined(Q_OS_MACOS)
    return {home + "/Library/Application Support/Steam"};
#else
    return {QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Steam", home + "/.local/share/Steam", home + "/.steam/steam", home + "/.steam/root", home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"};
#endif
}
QVector<WorkshopItem> steamWorkshopItems(const QStringList &roots) {
    QStringList libraries; QSet<QString> seen;
    auto add = [&](const QString &path) {
        if (path.isEmpty() || !QDir::isAbsolutePath(path) || libraries.size() >= 64) return;
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty() && !seen.contains(canonical)) { seen.insert(canonical); libraries.append(canonical); }
    };
    for (const auto &root : roots.mid(0, 64)) {
        add(root); QFile file(root + "/steamapps/libraryfolders.vdf");
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) continue;
        QStringDecoder decoder(QStringDecoder::Utf8); Vdf parser; parser.text = decoder(file.read(1024 * 1024 + 1));
        if (decoder.hasError() || parser.text.size() > 1024 * 1024) continue;
        const auto document = parser.object(); if (!parser.valid) continue;
        const auto folders = document["libraryfolders"].toObject();
        for (auto it = folders.begin(); it != folders.end(); ++it) {
            if (!QRegularExpression("^[0-9]+$").match(it.key()).hasMatch()) continue;
            add(it.value().isString() ? it.value().toString() : it.value().toObject()["path"].toString());
        }
    }
    QVector<WorkshopItem> result; QSet<QString> items;
    for (const auto &library : libraries) {
        const QDir directory(library + "/steamapps/workshop/content/1920960");
        QDirIterator entries(directory.path(), QDir::Dirs | QDir::NoDotAndDotDot);
        int visited = 0;
        while (entries.hasNext()) {
            if (++visited > 4096 || result.size() >= 4096) return result;
            entries.next(); const auto entry = entries.fileInfo();
            if (!QRegularExpression("^[0-9]+$").match(entry.fileName()).hasMatch() || entry.isSymLink() || entry.isJunction()) continue;
            const auto path = entry.canonicalFilePath();
            if (!path.isEmpty() && !items.contains(path)) { items.insert(path); result.append({entry.fileName(), path}); }
        }
    }
    return result;
}
}
