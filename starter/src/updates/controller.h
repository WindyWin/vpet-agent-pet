#pragma once
#include "release.h"
#include "components.h"
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <functional>
class QWidget;
class QDialog;
class QNetworkReply;
namespace pet::updates {
// Returns true when startup was handed to the updater or another launch.
bool prepareStartup(const QStringList &arguments);
class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(QObject *parent = nullptr, QNetworkAccessManager *transport = nullptr,
                        QString prefix = installedPrefix(), QString directory = dataDirectory());
    QWidget *settings(QWidget *parent);
    void showSettings(QWidget *parent);
    void start();
    void check(bool manual = false);
    void download();
    void install();
    // Installs a downloaded update in fully automatic mode unless an agent is waiting for the user.
    void autoInstall();
    QString indicator() const;
    std::function<bool()> sessionsActive;
signals:
    void changed();
    void restartRequested();
private:
    void fetch(const Release &target, const QString &path, std::function<void()> complete,
               std::function<void()> fallback = {});
    void downloadFull(const Release &target);
    void downloadComponent(const Release &target, const Components &components, int index);
    void finishDownload(const Release &target, bool components);
    bool save();
    void status(QString text);
    void cancel();
    QString directory_, prefix_, message_;
    QJsonObject state_;
    Release release_;
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> reply_;
    QPointer<QDialog> dialog_;
    bool ready_ = false, writable_ = true, downloading_ = false;
};
}
