#pragma once
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <memory>
namespace pet::packs {
struct Candidate {
    std::unique_ptr<QTemporaryDir> staging;
    QJsonObject metadata;
    QString folder, terms, oldVersion, oldSource;
    // Set only by app code after consent; never read from pack contents.
    QJsonObject origin;
    bool automatic = false;
};
QString dataFolder();
QString sourceRevision(const QString &source, const QString &data = {});
QJsonObject packOrigin(const QString &kind, const QString &id, const QString &data = {});
QJsonObject installationOrigin(const Candidate &candidate, const QString &source, const QString &digest, const QString &data = {});
// Fully validate before presenting terms; staged data stays alive until consent/commit.
std::unique_ptr<Candidate> inspect(const QString &archive, QString &error, const QString &data = {});
bool validate(const QString &folder, QJsonObject &metadata, QString &terms, QString &error);
bool install(Candidate &candidate, QString &error, const QString &data = {});
bool remove(const QString &kind, const QString &id, QString &error, const QString &data = {});
// Called before any local pack is activated, with the runtime lock held.
bool applyPending(QString &error, const QString &data = {});
int command(const QStringList &args);
}
