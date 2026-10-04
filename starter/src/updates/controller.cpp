#include "controller.h"
#include "version.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QLockFile>
#include <QNetworkReply>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <memory>
namespace pet::updates {
static QJsonObject readObject(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
static bool writeObject(const QString &path, const QJsonObject &object) {
    QSaveFile file(path); const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
static QString helper(const QString &prefix) { return prefix + "/bin/agent-pet-updater"; }
bool prepareStartup(const QStringList &args) {
    if (args.contains("--smoke-test") || args.contains("--no-persist")) return false;
    const QString prefix = installedPrefix(); if (prefix.isEmpty()) return false;
    const QString journal = prefix + ".update-transaction.json";
    const int healthIndex = args.indexOf("--update-health");
    if (healthIndex >= 0) {
        const QString token = args.value(healthIndex + 1);
        // Only the process launched by the current installer bypasses its lock.
        return token.isEmpty() || readObject(journal)["token"].toString() != token;
    }
    QLockFile lock(prefix + ".update-lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return true;
    lock.unlock();
    if (QFile::exists(journal)) {
        if (QProcess::execute(helper(prefix), {"--recover", prefix}) != 0) return true;
        QProcess::startDetached(prefix + "/bin/agent-pet", args.mid(1)); return true;
    }
    const auto state = readObject(dataDirectory() + "/state.json");
    const auto pending = readObject(dataDirectory() + "/pending.json");
    // Agent-triggered startup carries a session event: deliver it immediately.
    if (state["format"].toInt() != 1 || state["mode"].toInt() != 2 || args.contains("--autostarted") || pending.isEmpty()) return false;
    if (!newer(pending["version"].toString(), AGENT_PET_VERSION)) { QFile::remove(dataDirectory() + "/pending.json"); return false; }
    QStringList command{"--apply", prefix, dataDirectory() + "/package.tar.gz", pending["digest"].toString(),
                        pending["version"].toString(), QString::number(QCoreApplication::applicationPid())};
    command << args.mid(1);
    return QProcess::startDetached(helper(prefix), command);
}
Controller::Controller(QObject *parent, QNetworkAccessManager *transport, QString prefix, QString directory)
    : QObject(parent), directory_(std::move(directory)), prefix_(std::move(prefix)), network_(transport ? transport : &ownedNetwork_) {
    QDir().mkpath(directory_);
    state_ = readObject(directory_ + "/state.json");
    if (QFile::exists(directory_ + "/state.json") && (state_.isEmpty() || state_["format"].toInt() != 1
        || state_["mode"].toInt(-1) < 0 || state_["mode"].toInt(-1) > 2)) {
        writable_ = false; message_ = "Update settings could not be read; automatic updates are disabled."; state_ = {};
    }
    if (state_.isEmpty()) state_ = {{"format", 1}, {"mode", 0}, {"enabled", true}};
    QString cacheError;
    Release cached;
    if (parseRelease(state_["release"].toObject(), QSysInfo::buildCpuArchitecture(), cached, cacheError)
        && newer(cached.version, AGENT_PET_VERSION) && state_["skipped"].toString() != cached.version) {
        release_ = cached; message_ = "Version " + cached.version + " is available.";
    }
    const auto pending = readObject(directory_ + "/pending.json");
    if (newer(pending["version"].toString(), AGENT_PET_VERSION)) {
        release_.version = pending["version"].toString(); release_.digest = pending["digest"].toString();
        release_.page = QUrl("https://github.com/WindyWin/vpet-agent-pet/releases/tag/v" + release_.version);
        QString error;
        ready_ = verifiedArchive(directory_ + "/package.tar.gz", release_.digest, error);
        if (ready_) message_ = "Update " + release_.version + " is ready to install.";
    }
    QFile result(directory_ + "/result.txt");
    if (result.open(QIODevice::ReadOnly)) message_ = QString::fromUtf8(result.read(4096)) + "\n" + message_;
}
bool Controller::save() {
    if (writable_ && writeObject(directory_ + "/state.json", state_)) return true;
    message_ = "Cannot save update settings. Check permissions and disk space."; emit changed(); return false;
}
void Controller::status(QString text) { message_ = std::move(text); emit changed(); }
QString Controller::indicator() const {
    if (release_.version.isEmpty() || state_["skipped"].toString() == release_.version) return "Updates…";
    return ready_ ? "Update ready — " + release_.version + "…" : "Update available — " + release_.version + "…";
}
void Controller::start() {
    QTimer::singleShot(15000, this, [this] { check(); });
    auto *timer = new QTimer(this); timer->setInterval(60 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [this] { check(); }); timer->start();
}
void Controller::check(bool manual) {
    if (reply_ || !writable_) return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto last = state_["attempt"].toInteger();
    if (!manual && (!state_["enabled"].toBool(true) || (last <= now && now - last < 86400))) return;
    state_["attempt"] = now; if (!save()) return;
    if (manual) status("Checking for updates…");
    QNetworkRequest request(QUrl("https://api.github.com/repos/WindyWin/vpet-agent-pet/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Agent-Pet/" AGENT_PET_VERSION);
    request.setTransferTimeout(20000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto *reply = network_->get(request); reply_ = reply; reply->setReadBufferSize(1024 * 1024);
    QTimer::singleShot(30000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll()); if (bytes->size() > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, manual] {
        reply_ = nullptr; reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            if (manual) status("Could not check for updates. Try again later."); else emit changed(); return;
        }
        state_["checked"] = QDateTime::currentSecsSinceEpoch(); save();
        const auto object = QJsonDocument::fromJson(*bytes).object();
        Release candidate; QString error;
        if (!parseRelease(object, QSysInfo::buildCpuArchitecture(), candidate, error)) {
            if (manual) status(error); else emit changed(); return;
        }
        state_["release"] = object; save();
        if (!newer(candidate.version, AGENT_PET_VERSION)) { if (manual) status("You’re up to date."); else emit changed(); return; }
        if (!manual && candidate.version == state_["skipped"].toString()) { emit changed(); return; }
        if (manual) { state_.remove("skipped"); save(); }
        if (release_.version != candidate.version) ready_ = false;
        release_ = candidate;
        status(ready_ ? "Update " + release_.version + " is ready to install." : "Version " + release_.version + " is available." + (release_.digest.isEmpty() ? " Use manual download; this release has no verification digest." : ""));
        if (!ready_ && state_["mode"].toInt() > 0 && !prefix_.isEmpty() && !release_.digest.isEmpty()) download();
    });
    emit changed();
}
void Controller::cancel() { if (reply_) reply_->abort(); }
void Controller::download() {
    if (reply_ || ready_ || prefix_.isEmpty() || release_.digest.isEmpty() || release_.download.isEmpty()) return;
    QFile::remove(directory_ + "/pending.json");
    auto file = std::make_shared<QSaveFile>(directory_ + "/package.tar.gz");
    if (!file->open(QIODevice::WriteOnly)) { status("Cannot save the download. Check free disk space."); return; }
    const Release target = release_;
    QNetworkRequest request(target.download); request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    auto *reply = network_->get(request); reply_ = reply; reply->setReadBufferSize(256 * 1024);
    QTimer::singleShot(10 * 60 * 1000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    auto received = std::make_shared<qint64>(0);
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, received, target] {
        const auto bytes = reply->readAll(); *received += bytes.size();
        if (*received > target.size || file->write(bytes) != bytes.size()) reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytes, qint64 total) {
        status(total > 0 ? QString("Downloading update… %1%").arg(bytes * 100 / total) : "Downloading update…");
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, received, target] {
        reply_ = nullptr; reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
            || *received != target.size || !file->commit()) {
            file->cancelWriting(); status("Download stopped or failed. You can retry."); return;
        }
        QString error;
        if (!verifiedArchive(directory_ + "/package.tar.gz", target.digest, error)) {
            QFile::remove(directory_ + "/package.tar.gz"); status(error); return;
        }
        if (!writeObject(directory_ + "/pending.json", target.json())) { status("Cannot save the pending update."); return; }
        ready_ = true; status("Update " + target.version + " is ready. Restart when your sessions are finished.");
    });
    status("Downloading update…");
}
void Controller::install() {
    if (!ready_ || prefix_.isEmpty() || reply_) return;
    if (sessionsActive && sessionsActive()) { status("Finish your agent sessions before restarting to update."); return; }
    QStringList args{"--apply", prefix_, directory_ + "/package.tar.gz", release_.digest, release_.version,
                     QString::number(QCoreApplication::applicationPid())};
    if (!QProcess::startDetached(helper(prefix_), args)) { status("Could not start the update installer."); return; }
    emit restartRequested();
}
void Controller::showSettings(QWidget *parent) {
    if (dialog_) { dialog_->show(); dialog_->raise(); return; }
    auto *dialog = new QDialog(parent); dialog_ = dialog;
    dialog->setWindowTitle("Agent Pet updates"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog); layout->addWidget(settings(dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText("Later / Close");
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons); dialog->resize(460, 500); dialog->show();
}
QWidget *Controller::settings(QWidget *parent) {
    auto *box = new QGroupBox("Updates", parent); auto *layout = new QVBoxLayout(box);
    auto *version = new QLabel("Installed version: " AGENT_PET_VERSION, box); layout->addWidget(version);
    auto *enabled = new QCheckBox("Check automatically once a day", box); enabled->setChecked(state_["enabled"].toBool(true)); layout->addWidget(enabled);
    auto *mode = new QComboBox(box); mode->addItems({"Notify only", "Download automatically", "Install automatically on next launch"});
    mode->setCurrentIndex(state_["mode"].toInt()); layout->addWidget(mode);
    enabled->setEnabled(writable_); mode->setEnabled(writable_ && !prefix_.isEmpty());
    if (prefix_.isEmpty()) { auto *hint = new QLabel("This copy supports notifications and manual downloads. Install a release bundle to enable automatic updates.", box); hint->setWordWrap(true); layout->addWidget(hint); }
    auto *last = new QLabel(box); layout->addWidget(last);
    auto *message = new QLabel(box); message->setWordWrap(true); message->setTextFormat(Qt::PlainText); layout->addWidget(message);
    auto *checkButton = new QPushButton("Check now", box); layout->addWidget(checkButton);
    auto *notes = new QPushButton("View release / manual download", box); layout->addWidget(notes);
    auto *downloadButton = new QPushButton("Download update", box); layout->addWidget(downloadButton);
    auto *installButton = new QPushButton("Restart and update", box); layout->addWidget(installButton);
    auto *skip = new QPushButton("Skip this version", box); layout->addWidget(skip);
    auto *cancelButton = new QPushButton("Cancel download / check", box); layout->addWidget(cancelButton);
    const auto refresh = [=] {
        const auto checked = state_["checked"].toInteger();
        last->setText(checked ? "Last checked: " + QDateTime::fromSecsSinceEpoch(checked).toLocalTime().toString("yyyy-MM-dd hh:mm") : "Not checked yet");
        message->setText(message_); checkButton->setEnabled(!reply_ && writable_);
        notes->setEnabled(!release_.page.isEmpty());
        downloadButton->setEnabled(!reply_ && !ready_ && !prefix_.isEmpty() && !release_.digest.isEmpty() && !release_.download.isEmpty());
        installButton->setEnabled(ready_ && !reply_ && !prefix_.isEmpty()); skip->setEnabled(!release_.version.isEmpty() && !reply_);
        cancelButton->setEnabled(bool(reply_));
    };
    connect(this, &Controller::changed, box, refresh); refresh();
    connect(enabled, &QCheckBox::toggled, this, [this](bool value) { state_["enabled"] = value; save(); });
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int value) { state_["mode"] = value; if (save() && value > 0 && state_["skipped"].toString() != release_.version) download(); });
    connect(checkButton, &QPushButton::clicked, this, [this] { check(true); });
    connect(notes, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(release_.page); });
    connect(downloadButton, &QPushButton::clicked, this, &Controller::download);
    connect(installButton, &QPushButton::clicked, this, &Controller::install);
    connect(cancelButton, &QPushButton::clicked, this, &Controller::cancel);
    connect(skip, &QPushButton::clicked, this, [this] { state_["skipped"] = release_.version; save(); ready_ = false;
        QFile::remove(directory_ + "/pending.json"); QFile::remove(directory_ + "/package.tar.gz"); status("This version will be skipped. Check now to see it again."); });
    return box;
}
}
