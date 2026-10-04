#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include "sessions/state.h"
namespace pet {
class Receiver : public QObject {
public:
    explicit Receiver(QObject *parent = nullptr) : QObject(parent) {}
    ~Receiver() override;
    bool start(QString &error);
    std::function<void(const Event &)> received;
private:
    int socket_ = -1, lock_ = -1;
    QByteArray path_;
};
// Sends one normalized event datagram to the running pet.
bool sendEvent(const QByteArray &data, QString &error);
// These commands use QCoreApplication only and require no display server.
int eventCommand(const QStringList &args);
}
