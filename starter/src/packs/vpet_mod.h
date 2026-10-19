#pragma once
#include <QJsonObject>
#include <QString>
namespace pet::packs {
struct VPetOptions {
    QString id = "converted-pet", author, license, terms;
};
// Output must not exist. Reads data only; caller owns the private staging directory.
bool convertVPetMod(const QString &input, const QString &output, const VPetOptions &options,
                    QJsonObject &report, QString &error);
bool modPath(const QString &path);
}
