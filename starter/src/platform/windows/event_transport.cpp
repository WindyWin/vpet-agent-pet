#include "platform/contracts/event_transport.h"
#include <QCryptographicHash>
#include <QObject>
#include <QTimer>
#include <QWinEventNotifier>
#include <array>
#include <vector>
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

// Windows has no local datagram sockets. A message-mode named pipe carries one event per
// connection: hooks connect, write one message and close; the pet keeps a few overlapped
// instances listening. The pipe is created as the first instance of its name, which is the
// single-instance lock, and only this user may open it.
namespace pet::platform {
static constexpr int maxDatagram = 8192;
static constexpr int instanceCount = 8; // Connections that can wait while the pet is busy.

namespace {
// The process token's user, with its SID kept alive in the buffer.
class CurrentUser {
public:
    CurrentUser() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
        DWORD size = 0;
        GetTokenInformation(token, ::TokenUser, nullptr, 0, &size);
        buffer_.resize(size);
        if (!size || !GetTokenInformation(token, ::TokenUser, buffer_.data(), size, &size)) buffer_.clear();
        CloseHandle(token);
    }
    PSID sid() const { return buffer_.empty() ? nullptr : reinterpret_cast<const TOKEN_USER *>(buffer_.data())->User.Sid; }
    QString text() const {
        LPWSTR string = nullptr;
        if (!sid() || !ConvertSidToStringSidW(sid(), &string)) return {};
        const auto result = QString::fromWCharArray(string);
        LocalFree(string);
        return result;
    }
private:
    std::vector<BYTE> buffer_;
};
// Per user, so other users' pets and hooks never meet. XDG_RUNTIME_DIR, normally unset on Windows,
// selects a separate endpoint as it does on POSIX, so tests do not disturb a running pet.
std::wstring pipeName(const QString &sid) {
    auto name = "\\\\.\\pipe\\agent-pet-" + sid + "-events";
    const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) name += '-' + QCryptographicHash::hash(runtime.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return name.toStdWString();
}
// Another user can create a pipe with our name first. Its owner is then not us, and we
// never send to it; SECURITY_IDENTIFICATION also stops it from impersonating the hook.
bool ownedBy(HANDLE pipe, PSID user) {
    PSID owner = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    const bool owned = owner && user && EqualSid(owner, user);
    LocalFree(descriptor);
    return owned;
}

class PipeEventTransport final : public QObject, public EventTransport {
public:
    ~PipeEventTransport() override;
    bool start(QString &error) override;
private:
    struct Instance {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        OVERLAPPED overlapped{};
        QWinEventNotifier *notifier = nullptr;
        bool reading = false;
        std::array<char, maxDatagram + 1> buffer{};
    };
    void listen(Instance &instance);
    void read(Instance &instance);
    void completed(Instance &instance);
    std::array<Instance, instanceCount> instances_;
};

PipeEventTransport::~PipeEventTransport() {
    for (auto &instance : instances_) {
        if (instance.notifier) instance.notifier->setEnabled(false);
        if (instance.pipe != INVALID_HANDLE_VALUE) {
            DWORD bytes = 0;
            if (CancelIoEx(instance.pipe, &instance.overlapped)) GetOverlappedResult(instance.pipe, &instance.overlapped, &bytes, TRUE);
            CloseHandle(instance.pipe);
        }
        if (instance.overlapped.hEvent) CloseHandle(instance.overlapped.hEvent);
    }
}
bool PipeEventTransport::start(QString &error) {
    const CurrentUser user;
    const auto sid = user.text();
    if (sid.isEmpty()) { error = "Cannot identify the current user"; return false; }
    // Owned by and open only to this user; remote clients are rejected below.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto sddl = ("O:" + sid + "D:P(A;;GA;;;" + sid + ")").toStdWString();
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        error = "Cannot create the event endpoint's permissions"; return false;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const auto name = pipeName(sid);
    for (int i = 0; i < instanceCount; ++i) {
        auto &instance = instances_[size_t(i)];
        instance.pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | (i == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                         instanceCount, 0, 2 * (maxDatagram + 1), 0, &attributes);
        if (instance.pipe == INVALID_HANDLE_VALUE) {
            const auto code = GetLastError();
            LocalFree(descriptor);
            error = i == 0 && (code == ERROR_ACCESS_DENIED || code == ERROR_PIPE_BUSY)
                ? "Another Agent Pet monitor is running, or its endpoint is unavailable" : "Cannot create the event endpoint";
            return false;
        }
        instance.overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!instance.overlapped.hEvent) { LocalFree(descriptor); error = "Cannot create the event endpoint"; return false; }
        instance.notifier = new QWinEventNotifier(instance.overlapped.hEvent, this);
        connect(instance.notifier, &QWinEventNotifier::activated, this, [this, i] { completed(instances_[size_t(i)]); });
    }
    LocalFree(descriptor);
    for (auto &instance : instances_) listen(instance);
    return true;
}
// Waits for the next sender. A sender that arrived before ConnectNamedPipe is already connected.
void PipeEventTransport::listen(Instance &instance) {
    instance.reading = false;
    ResetEvent(instance.overlapped.hEvent);
    if (ConnectNamedPipe(instance.pipe, &instance.overlapped)) return; // Completion signals the event.
    const auto code = GetLastError();
    if (code == ERROR_PIPE_CONNECTED || code == ERROR_NO_DATA) { read(instance); return; }
    if (code == ERROR_IO_PENDING) return;
    // Nothing is pending, so no completion will come: try again shortly rather than spin or lose the instance.
    DisconnectNamedPipe(instance.pipe);
    QTimer::singleShot(250, this, [this, &instance] { listen(instance); });
}
void PipeEventTransport::read(Instance &instance) {
    instance.reading = true;
    ResetEvent(instance.overlapped.hEvent);
    if (ReadFile(instance.pipe, instance.buffer.data(), DWORD(instance.buffer.size()), nullptr, &instance.overlapped)
        || GetLastError() == ERROR_IO_PENDING || GetLastError() == ERROR_MORE_DATA)
        return; // Completion, including an oversized message's partial read, signals the event.
    DisconnectNamedPipe(instance.pipe);
    listen(instance);
}
void PipeEventTransport::completed(Instance &instance) {
    DWORD bytes = 0;
    const bool ok = GetOverlappedResult(instance.pipe, &instance.overlapped, &bytes, FALSE);
    if (!ok && GetLastError() == ERROR_IO_INCOMPLETE) return;
    if (!instance.reading) {
        if (ok) read(instance);
        else { DisconnectNamedPipe(instance.pipe); listen(instance); }
        return;
    }
    // An oversized message fails with ERROR_MORE_DATA and is dropped with the connection.
    if (ok && bytes <= DWORD(maxDatagram) && received) received(QByteArray(instance.buffer.data(), int(bytes)));
    DisconnectNamedPipe(instance.pipe);
    listen(instance);
}
}

bool sendDatagram(const QByteArray &data, QString &error) {
    const CurrentUser user;
    const auto sid = user.text();
    if (sid.isEmpty()) { error = "Cannot identify the current user"; return false; }
    const auto name = pipeName(sid);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    // Every instance can be momentarily busy with another sender; wait briefly once.
    for (int attempt = 0; attempt < 2 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe == INVALID_HANDLE_VALUE && (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name.c_str(), 50))) break;
    }
    if (pipe == INVALID_HANDLE_VALUE) { error = "Monitor unavailable or event queue full"; return false; }
    if (!ownedBy(pipe, user.sid())) { CloseHandle(pipe); error = "Event endpoint is not owned by this user"; return false; }
    // The pipe's buffer holds a whole message, so this write never waits for the pet.
    DWORD written = 0;
    const bool sent = WriteFile(pipe, data.constData(), DWORD(data.size()), &written, nullptr) && written == DWORD(data.size());
    CloseHandle(pipe);
    if (!sent) { error = "Monitor unavailable or event queue full"; return false; }
    return true;
}
std::unique_ptr<EventTransport> createEventTransport() { return std::make_unique<PipeEventTransport>(); }
}
