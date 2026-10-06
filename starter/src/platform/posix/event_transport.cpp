#include "platform/contracts/event_transport.h"
#include <QObject>
#include <QFile>
#include <QSocketNotifier>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
namespace pet::platform {
static constexpr int maxDatagram = 8192;
class UnixEventTransport final : public QObject, public EventTransport {
public:
    ~UnixEventTransport() override;
    bool start(QString &error) override;
private:
    int socket_ = -1, lock_ = -1;
    QByteArray path_;
};
// XDG_RUNTIME_DIR where the session provides one (and tests point it elsewhere). macOS has none, so it
// uses the per-user temporary directory, which launchd gives every process of the user, not just shells.
static QString runtimeRoot() {
    QString root = qEnvironmentVariable("XDG_RUNTIME_DIR");
#ifdef __APPLE__
    if (root.isEmpty()) {
        char directory[1024];
        const auto size = confstr(_CS_DARWIN_USER_TEMP_DIR, directory, sizeof(directory));
        if (size > 0 && size <= sizeof(directory)) root = QFile::decodeName(directory);
        while (root.size() > 1 && root.endsWith('/')) root.chop(1);
    }
#endif
    return root.isEmpty() ? QStringLiteral("/tmp") : root;
}
static QByteArray endpoint(QString &error) {
    const auto directory = QFile::encodeName(runtimeRoot() + "/agent-pet-" + QString::number(getuid()));
    if (mkdir(directory.constData(), 0700) != 0 && errno != EEXIST) { error = "Cannot create private runtime directory"; return {}; }
    struct stat st{};
    if (lstat(directory.constData(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) {
        error = "Runtime directory must be owned by this user with mode 0700"; return {};
    }
    auto path = directory + "/events.sock";
    if (path.size() >= int(sizeof(sockaddr_un::sun_path))) { error = "Runtime socket path too long"; return {}; }
    return path;
}
static sockaddr_un address(const QByteArray &path) {
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.constData(), path.size() + 1); return addr;
}
// A nonblocking, close-on-exec datagram socket; macOS lacks SOCK_NONBLOCK and SOCK_CLOEXEC.
static int datagramSocket() {
    const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) || fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)) { close(fd); return -1; }
#ifdef __APPLE__
    // macOS limits local datagrams to net.local.dgram.maxdgram (2 KiB) unless the buffers are raised.
    int size = maxDatagram + 1;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    size = 64 * (maxDatagram + 1);
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
#endif
    return fd;
}
UnixEventTransport::~UnixEventTransport() {
    if (socket_ >= 0) { close(socket_); if (!path_.isEmpty()) unlink(path_.constData()); }
    if (lock_ >= 0) close(lock_);
}
bool UnixEventTransport::start(QString &error) {
    const auto path = endpoint(error);
    if (path.isEmpty()) return false;
    const auto lockPath = path + ".lock";
    lock_ = open(lockPath.constData(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0 || flock(lock_, LOCK_EX | LOCK_NB)) { error = "Another Agent Pet monitor is running, or its lock is unavailable"; return false; }
    socket_ = datagramSocket();
    if (socket_ < 0) { error = "Cannot create local socket"; return false; }
    unlink(path.constData());
    const auto addr = address(path);
    if (bind(socket_, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr))) { error = "Cannot bind local socket"; return false; }
    path_ = path;
    chmod(path_.constData(), 0600);
    auto *notifier = new QSocketNotifier(socket_, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] {
        // Yield to the desktop after a bounded batch, even under a busy sender.
        for (int i = 0; i < 64; ++i) {
            char buffer[maxDatagram + 1];
            // An oversized datagram fills the buffer (Linux reports its full size with MSG_TRUNC) and is dropped.
#ifdef __linux__
            const auto size = recv(socket_, buffer, sizeof(buffer), MSG_DONTWAIT | MSG_TRUNC);
#else
            const auto size = recv(socket_, buffer, sizeof(buffer), MSG_DONTWAIT);
#endif
            if (size < 0) break;
            if (size > maxDatagram) continue;
            if (received) received(QByteArray(buffer, size));
        }
    });
    return true;
}
bool sendDatagram(const QByteArray &data, QString &error) {
    const auto path = endpoint(error); if (path.isEmpty()) return false;
    const int fd = datagramSocket();
    if (fd < 0) { error = "Cannot create sender"; return false; }
    const auto addr = address(path);
#ifdef __linux__
    const int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#else
    const int flags = MSG_DONTWAIT; // Datagram sockets do not raise SIGPIPE on macOS.
#endif
    const auto sent = sendto(fd, data.constData(), data.size(), flags, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    close(fd);
    if (sent != data.size()) { error = "Monitor unavailable or event queue full"; return false; }
    return true;
}
std::unique_ptr<EventTransport> createEventTransport() { return std::make_unique<UnixEventTransport>(); }
}
