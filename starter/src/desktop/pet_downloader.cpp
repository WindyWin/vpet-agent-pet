#include "pet_downloader.h"
#include <QDebug>
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>

namespace pet {
PetDownloader::PetDownloader(PetLibrary &library, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), library_(library), network_(network ? network : &ownedNetwork_) {}
PetDownloader::~PetDownloader() {
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
}
QUrl PetDownloader::blobUrl(const QString &sha256) {
    return QUrl("https://github.com/WindyWin/vpet-agent-pet/releases/download/pets/" + sha256 + ".rcc");
}
bool PetDownloader::allowedRedirect(const QUrl &url) {
    static const QStringList hosts{"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
    return url.scheme() == "https" && hosts.contains(url.host());
}
bool PetDownloader::start(const QString &id) {
    if (!pet_.isEmpty()) return false;
    const auto info = library_.info(id);
    if (info.id.isEmpty()) return false;
    pet_ = id; name_ = info.name;
    queue_ = library_.missing(id);
    index_ = 0; retried_ = false; done_ = received_ = total_ = 0;
    for (const auto &pack : std::as_const(queue_)) total_ += pack.bytes;
    // Reported from the event loop, as a download's progress and end would be.
    QTimer::singleShot(0, this, [this, id] {
        if (pet_ != id || reply_) return;
        if (!queue_.isEmpty() && !QDir().mkpath(library_.store())) {
            end(tr("Cannot save %1. Check free disk space.").arg(name_));
            return;
        }
        emit progressed(pet_, 0, total_);
        next();
    });
    return true;
}
void PetDownloader::cancel() {
    if (pet_.isEmpty()) return;
    if (reply_) { reply_->disconnect(this); reply_->abort(); reply_->deleteLater(); reply_ = nullptr; }
    end(tr("Download cancelled."));
}
void PetDownloader::next() {
    if (index_ < queue_.size()) { fetch(); return; }
    QString error;
    if (!library_.markDownloaded(pet_, &error)) {
        qWarning().noquote() << "Cannot finish downloading pet" << pet_ << ":" << error;
        end(tr("Cannot save %1. Check free disk space.").arg(name_));
        return;
    }
    end({});
}
void PetDownloader::fetch() {
    const auto pack = queue_.at(index_);
    received_ = 0;
    writeFailed_ = false;
    hash_.reset();
    file_ = std::make_unique<QSaveFile>(library_.blobPath(pack.sha256));
    if (!file_->open(QIODevice::WriteOnly)) { end(tr("Cannot save %1. Check free disk space.").arg(name_)); return; }
    QNetworkRequest request(blobUrl(pack.sha256));
    request.setTransferTimeout(idleTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    auto *reply = network_->get(request);
    reply_ = reply;
    reply->setReadBufferSize(256 * 1024);
    QTimer::singleShot(packTimeoutMs, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl &url) {
        if (allowedRedirect(url)) emit reply->redirectAllowed();
        else reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, pack] {
        const auto bytes = reply->readAll();
        received_ += bytes.size();
        // More than the leaf says is never the pack; stop reading at once.
        if (received_ > pack.bytes) { reply->abort(); return; }
        if (file_->write(bytes) != bytes.size()) { writeFailed_ = true; reply->abort(); return; } // Typically a full disk.
        hash_.addData(bytes);
        emit progressed(pet_, done(), total_);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, pack] {
        reply_ = nullptr;
        reply->deleteLater();
        const bool transferred = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const bool intact = received_ == pack.bytes && hash_.result().toHex() == pack.sha256.toLatin1();
        if (writeFailed_) {
            file_->cancelWriting();
            file_.reset();
            end(tr("Cannot save %1. Check free disk space.").arg(name_));
            return;
        }
        if (!transferred || !intact) {
            file_->cancelWriting();
            file_.reset();
            // A complete answer with the wrong bytes, or too many of them, is tried once more; anything else is the network.
            const bool corrupt = (transferred && !intact) || received_ > pack.bytes;
            if (corrupt && !retried_) { retried_ = true; fetch(); return; }
            qWarning().noquote() << "Pet download failed:" << reply->url().toString() << reply->errorString()
                                 << (corrupt ? "(checksum mismatch)" : "");
            end(corrupt ? tr("The download of %1 did not match its checksum. Try again later.").arg(name_)
                        : tr("Could not download %1. Check your connection and try again.").arg(name_));
            return;
        }
        if (!file_->commit()) { file_.reset(); end(tr("Cannot save %1. Check free disk space.").arg(name_)); return; }
        file_.reset();
        done_ += pack.bytes; received_ = 0; retried_ = false; ++index_;
        emit progressed(pet_, done_, total_);
        next();
    });
}
void PetDownloader::end(const QString &error) {
    if (file_) { file_->cancelWriting(); file_.reset(); }
    const auto id = std::exchange(pet_, QString());
    queue_.clear();
    emit finished(id, error);
}
}
