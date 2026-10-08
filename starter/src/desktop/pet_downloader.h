#pragma once
#include "animation/pet_library.h"
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <memory>

class QNetworkReply;
class QSaveFile;

namespace pet {
// Downloads a pet that is not bundled (docs/pets.md): only the packs its hash tree lists that are neither installed
// nor in the store, one at a time, from the long-lived `pets` release, where each pack is named by its SHA-256.
// Each arrives in a temporary file and enters the store only once its size and digest match the leaf the
// installed index carries; a mismatch is fetched once more, then the download fails. Network rules match the
// updater's: TLS verification, a size limit per pack, an idle timeout, and HTTPS redirects only to GitHub's asset
// hosts. One pet downloads at a time; the pet that runs is never touched.
class PetDownloader : public QObject {
    Q_OBJECT
public:
    static constexpr int idleTimeoutMs = 30000;
    static constexpr int packTimeoutMs = 10 * 60 * 1000;
    // Uses `network` when given (tests), else its own.
    explicit PetDownloader(PetLibrary &library, QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~PetDownloader() override;
    static QUrl blobUrl(const QString &sha256);
    static bool allowedRedirect(const QUrl &url); // HTTPS to github.com or its release asset hosts.
    // Starts downloading `id`; false while another download runs or when the pet needs nothing. A pet that needs
    // nothing is stamped complete at once.
    bool start(const QString &id);
    void cancel(); // Ends with finished(id, "Download cancelled.").
    QString pet() const { return pet_; } // The pet downloading; empty when idle.
    qint64 done() const { return done_ + received_; }
    qint64 total() const { return total_; }
signals:
    void progressed(const QString &id, qint64 done, qint64 total);
    // `error` is empty when the pet is complete, else a translated reason; the store keeps every verified pack.
    void finished(const QString &id, const QString &error);
private:
    void next();
    void fetch();
    void end(const QString &error);
    PetLibrary &library_;
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QVector<PetPack> queue_;
    QString pet_, name_;
    int index_ = 0;
    bool retried_ = false;
    qint64 done_ = 0, received_ = 0, total_ = 0;
};
}
