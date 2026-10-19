#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <memory>

class QNetworkReply;
namespace pet::packs {
inline constexpr qint64 MaxIndex = 2 * 1024 * 1024;
inline constexpr qint64 MaxDownload = 544LL * 1024 * 1024;
QString officialSource();
bool httpsUrl(const QUrl &url);
bool newerPackVersion(const QString &candidate, const QString &installed);
struct Entry {
    QJsonObject metadata;
    QUrl preview, download;
    QString description, setup, digest;
    qint64 bytes = 0;
    QString key() const;
};
bool parseIndex(const QByteArray &bytes, QVector<Entry> &entries, QString &error);
QString compatibility(const Entry &entry, const QString &appVersion, const QStringList &pets);
bool matches(const Entry &entry, const QJsonObject &metadata);
// Only validated indexes replace the last good cache. Removing a source never touches packs.
class SourceStore {
public:
    explicit SourceStore(QString folder);
    QStringList urls(QString &error) const;
    bool add(const QString &url, QString &error);
    bool remove(const QString &url, QString &error);
    QVector<Entry> cached(const QString &url, QString &error) const;
    bool cache(const QString &url, const QByteArray &bytes, QString &error);
private:
    QString folder_;
    QString cachePath(const QString &url) const;
    bool save(const QStringList &urls, QString &error);
};
// One bounded transfer. The temporary file is valid during completed(), then removed.
// Manager injection permits deterministic transport tests without live servers.
class Download : public QObject {
    Q_OBJECT
public:
    explicit Download(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~Download() override;
    void start(const QUrl &url, qint64 limit, qint64 expected = -1, const QString &digest = {});
    void cancel();
    bool busy() const { return !reply_.isNull(); }
signals:
    void progress(qint64 bytes, qint64 total);
    void completed(const QString &path);
    void failed(const QString &error);
private:
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QTemporaryFile> file_;
    QTimer timer_;
    qint64 limit_ = 0, expected_ = -1;
    QString digest_, failure_;
    void drain();
    void stop(const QString &error);
};
}
