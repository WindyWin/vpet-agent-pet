#include "packs/installer.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QTest>
#include <QTemporaryDir>
using namespace pet;
namespace {
bool copyTree(const QString &source, const QString &target) {
    if (!QDir().mkpath(target)) return false;
    QDirIterator it(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); const auto to = target + '/' + QDir(source).relativeFilePath(it.filePath());
        if (it.fileInfo().isDir()) { if (!QDir().mkpath(to)) return false; }
        else if (!QFile::copy(it.filePath(), to)) return false;
    }
    return true;
}
}
class PackTests : public QObject {
    Q_OBJECT
private slots:
    void pendingReplacementAndRecovery() {
        QTemporaryDir data; QVERIFY(data.isValid()); QString error;
        const auto target = data.path() + "/packs/pets/local-cat";
        QVERIFY(copyTree(QString(PET_SOURCE) + "/tests/fixtures/local-pets/local-cat", target));
        auto stage = [&] {
            auto c = std::make_unique<packs::Candidate>(); c->staging = std::make_unique<QTemporaryDir>(data.path() + "/stage-XXXXXX");
            c->folder = c->staging->path() + "/local-cat";
            if (!copyTree(target, c->folder) || !packs::validate(c->folder, c->metadata, c->terms, error)) return std::unique_ptr<packs::Candidate>();
            return c;
        };
        auto c = stage(); QVERIFY2(c != nullptr, qPrintable(error));
        QVERIFY2(packs::install(*c, error, data.path()), qPrintable(error));
        QVERIFY(QFileInfo::exists(target + "/manifest.json")); // running readers keep their files
        const auto job = data.path() + "/pack-pending/pet-local-cat";
        QFile manifest(job + "/payload/local-cat/manifest.json"); QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write("{}"); manifest.close();
        QVERIFY(!packs::applyPending(error, data.path()));
        QJsonObject metadata; QString terms;
        QVERIFY2(packs::validate(target, metadata, terms, error), qPrintable(error));
        QVERIFY(QDir(job).removeRecursively());
        c = stage(); QVERIFY(c != nullptr); QVERIFY(packs::install(*c, error, data.path()));
        // Simulate a crash after retaining the old directory but before committing the new one.
        QVERIFY(QDir().rename(target, job + "/previous"));
        QVERIFY2(packs::applyPending(error, data.path()), qPrintable(error));
        QVERIFY(packs::validate(target, metadata, terms, error));
        QVERIFY(!QFileInfo::exists(job));
        QVERIFY(packs::remove("pet", "local-cat", error, data.path()));
        QVERIFY(QFileInfo::exists(target));
        QVERIFY(packs::applyPending(error, data.path()));
        QVERIFY(!QFileInfo::exists(target));
    }
};
QTEST_GUILESS_MAIN(PackTests)
#include "pack_tests.moc"
