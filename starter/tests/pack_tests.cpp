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
    void rejectedJobDoesNotBlockLaterChanges() {
        QTemporaryDir data; QVERIFY(data.isValid()); QString error;
        const auto fixture = QString(PET_SOURCE) + "/tests/fixtures/local-pets/local-cat";
        const auto target = data.path() + "/packs/pets/local-cat";
        QVERIFY(copyTree(fixture, target));
        packs::Candidate c; c.staging = std::make_unique<QTemporaryDir>(data.path() + "/stage-XXXXXX");
        c.folder = c.staging->path() + "/local-cat";
        QVERIFY(copyTree(fixture, c.folder));
        QVERIFY(packs::validate(c.folder, c.metadata, c.terms, error));
        QVERIFY(packs::install(c, error, data.path()));
        const auto job = data.path() + "/pack-pending/pet-local-cat";
        QFile manifest(job + "/payload/local-cat/manifest.json");
        QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write("{}"); manifest.close();
        // Recovery must restore the old version before discarding the invalid payload.
        QVERIFY(QDir().rename(target, job + "/previous"));
        const auto later = data.path() + "/packs/pets/z-later";
        QVERIFY(QDir().mkpath(later));
        QVERIFY(packs::remove("pet", "z-later", error, data.path()));
        QVERIFY(!packs::applyPending(error, data.path()));
        QVERIFY2(error.contains("pet-local-cat:"), qPrintable(error));
        QVERIFY(!QFileInfo::exists(job));
        QVERIFY(!QFileInfo::exists(later));
        QJsonObject metadata; QString terms;
        QVERIFY(packs::validate(target, metadata, terms, error));
        QVERIFY(packs::applyPending(error, data.path()));
        QVERIFY(error.isEmpty());
        QVERIFY(packs::remove("pet", "local-cat", error, data.path()));
        QVERIFY(packs::applyPending(error, data.path()));
    }
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
        QVERIFY(!QFileInfo::exists(job));
        QVERIFY(packs::applyPending(error, data.path())); // rejection cannot wedge later starts
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
