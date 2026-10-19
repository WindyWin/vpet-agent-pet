#pragma once
#include "sources.h"
#include "installer.h"
#include <functional>
namespace pet::packs {
// Background source work is independent of the app-binary download/install mode.
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QString data, QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~Updater() override;
    static Updater *active();
    std::function<bool()> checksEnabled = [] { return false; };
    void start();
    void check(qint64 now = 0);
    void policyChanged();
    void cancel();
    bool automatic(const QString &source) const;
    bool setAutomatic(const QString &source, bool value, QString &error);
    QString message() const { return message_; }
    bool busy() const { return transfer_.busy(); }
    static bool sameConsent(const QJsonObject &old, const QJsonObject &next);
    // All paths, including automatic work, pass the transactional installer.
    bool stage(const Entry &entry, const QString &source, const QString &revision,
               const QString &archive, QString &error);
signals:
    void changed();
    void progress(qint64 bytes, qint64 total);
private:
    QString data_, source_, revision_, message_;
    SourceStore store_;
    QNetworkAccessManager owned_;
    Download transfer_;
    QStringList sources_;
    QVector<Entry> queue_;
    Entry current_;
    bool catalog_ = false, stopped_ = false;
    int generation_ = 0;
    void nextSource();
    void nextPack();
    void received(const QString &path);
    void status(const QString &text);
};
}
