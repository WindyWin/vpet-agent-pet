#pragma once
#include <QFileInfo>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>

namespace pet {
// Apply the same bounds to downloaded, cached and archive previews before decoding.
inline QPixmap packPreview(const QString &path) {
    const QFileInfo file(path);
    if (!file.isFile() || file.isSymLink() || file.size() > 4 * 1024 * 1024) return {};
    QImageReader reader(path);
    const auto size = reader.size();
    if (reader.format() != "png" || size.isEmpty() || size.width() > 512 || size.height() > 512) return {};
    const auto image = reader.read();
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image).scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
inline QPixmap packFolderPreview(const QString &folder) {
    QString name = "preview.png";
    QFile metadata(folder + "/pet.json");
    if (metadata.exists()) {
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 64 * 1024) return {};
        name = QJsonDocument::fromJson(metadata.readAll()).object()["preview"].toString();
        if (name.isEmpty() || QFileInfo(name).fileName() != name || name.contains('\\')) return {};
    }
    return packPreview(folder + '/' + name);
}
}
