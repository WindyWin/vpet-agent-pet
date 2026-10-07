#include "platform/contracts/update_layout.h"
#include "controller.h"
#include "components.h"
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
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <algorithm>
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
static QString pendingFile(const QString &directory, const QJsonObject &pending) {
    return directory + (pending["kind"].toString() == "components" ? "/components.json" : "/package.tar.gz");
}
static QString applyCommand(const QJsonObject &pending) {
    return pending["kind"].toString() == "components" ? "--apply-components" : "--apply";
}
static void pruneComponents(const QString &directory, const QStringList &keep = {}) {
    const auto patterns = platform::obsoleteComponentPatterns();
    for (const auto &name : QDir(directory).entryList(patterns, QDir::Files))
        if (!keep.contains(name)) QFile::remove(directory + '/' + name);
}
static bool pendingReady(const QString &directory, const QString &prefix, const QJsonObject &pending) {
    QString error;
    if (pending["kind"].toString() != "components")
        return verifiedArchive(pendingFile(directory, pending), pending["digest"].toString(), error);
    Components components;
    return readComponents(pendingFile(directory, pending), pending["digest"].toString(),
                          pending["version"].toString(), pet::platform::updateArchitecture(), components, error)
        && componentsAvailable(components, prefix, directory);
}
static QString helper(const QString &prefix) { return prefix + '/' + pet::platform::updaterRelativePath(); }
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
        QProcess::startDetached(prefix + '/' + pet::platform::applicationRelativePath(), args.mid(1)); return true;
    }
    const auto state = readObject(dataDirectory() + "/state.json");
    const auto pending = readObject(dataDirectory() + "/pending.json");
    // Agent-triggered startup carries a session event: deliver it immediately.
    if (state["format"].toInt() != 1 || state["mode"].toInt() < 2 || args.contains("--autostarted") || pending.isEmpty()) return false;
    if (!newer(pending["version"].toString(), AGENT_PET_VERSION)) { QFile::remove(dataDirectory() + "/pending.json"); return false; }
    QStringList command{applyCommand(pending), prefix, pendingFile(dataDirectory(), pending), pending["digest"].toString(),
                        pending["version"].toString(), QString::number(QCoreApplication::applicationPid())};
    command << args.mid(1);
    return QProcess::startDetached(helper(prefix), command);
}
Controller::Controller(QObject *parent, QNetworkAccessManager *transport, QString prefix, QString directory)
    : QObject(parent), directory_(std::move(directory)), prefix_(std::move(prefix)), network_(transport ? transport : &ownedNetwork_) {
    QDir().mkpath(directory_);
    state_ = readObject(directory_ + "/state.json");
    if (QFile::exists(directory_ + "/state.json") && (state_.isEmpty() || state_["format"].toInt() != 1
        || state_["mode"].toInt(-1) < 0 || state_["mode"].toInt(-1) > 3)) {
        writable_ = false; message_ = tr("Update settings could not be read; automatic updates are disabled."); state_ = {};
    }
    if (state_.isEmpty()) state_ = {{"format", 1}, {"mode", 3}, {"enabled", true}};
    QString cacheError;
    Release cached;
    if (parseRelease(state_["release"].toObject(), pet::platform::updateArchitecture(), cached, cacheError)
        && newer(cached.version, AGENT_PET_VERSION) && state_["skipped"].toString() != cached.version) {
        release_ = cached; message_ = tr("Version %1 is available.").arg(cached.version);
    }
    const auto pending = readObject(directory_ + "/pending.json");
    if (newer(pending["version"].toString(), AGENT_PET_VERSION)) {
        release_.version = pending["version"].toString();
        if (pending["kind"].toString() != "components") release_.digest = pending["digest"].toString();
        release_.page = QUrl("https://github.com/WindyWin/vpet-agent-pet/releases/tag/v" + release_.version);
        ready_ = pendingReady(directory_, prefix_, pending);
        if (ready_) message_ = tr("Update %1 is ready to install.").arg(release_.version);
    }
    QFile result(directory_ + "/result.txt");
    if (result.open(QIODevice::ReadOnly)) message_ = QString::fromUtf8(result.read(4096)) + "\n" + message_;
}
bool Controller::save() {
    if (writable_ && writeObject(directory_ + "/state.json", state_)) return true;
    message_ = tr("Cannot save update settings. Check permissions and disk space."); emit changed(); return false;
}
void Controller::status(QString text) { message_ = std::move(text); emit changed(); }
bool Controller::waiting() const { return !release_.version.isEmpty() && state_["skipped"].toString() != release_.version; }
QString Controller::indicator() const {
    if (!waiting()) return tr("Updates…");
    //: %1 = version
    return ready_ ? tr("Update ready — %1…").arg(release_.version) : tr("Update available — %1…").arg(release_.version);
}
void Controller::start() {
    QTimer::singleShot(15000, this, [this] { check(); autoInstall(); });
    auto *timer = new QTimer(this); timer->setInterval(60 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [this] { check(); }); timer->start();
    // Retry a ready update if its session checkpoint could not be saved.
    auto *retry = new QTimer(this); retry->setInterval(5 * 60 * 1000);
    connect(retry, &QTimer::timeout, this, [this] { autoInstall(); }); retry->start();
}
void Controller::check(bool manual) {
    if (reply_ || downloading_ || !writable_) return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto last = state_["attempt"].toInteger();
    if (!manual && (!state_["enabled"].toBool(true) || (last <= now && now - last < 86400))) return;
    state_["attempt"] = now; if (!save()) return;
    if (manual) status(tr("Checking for updates…"));
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
            if (manual) status(tr("Could not check for updates. Try again later.")); else emit changed(); return;
        }
        state_["checked"] = QDateTime::currentSecsSinceEpoch(); save();
        const auto object = QJsonDocument::fromJson(*bytes).object();
        Release candidate; QString error;
        if (!parseRelease(object, pet::platform::updateArchitecture(), candidate, error)) {
            if (manual) status(error); else emit changed(); return;
        }
        state_["release"] = object; save();
        if (!newer(candidate.version, AGENT_PET_VERSION)) { if (manual) status(tr("You’re up to date.")); else emit changed(); return; }
        if (!manual && candidate.version == state_["skipped"].toString()) { emit changed(); return; }
        if (manual) { state_.remove("skipped"); save(); }
        if (release_.version != candidate.version) ready_ = false;
        release_ = candidate;
        status(ready_ ? tr("Update %1 is ready to install.").arg(release_.version)
                      : release_.digest.isEmpty() ? tr("Version %1 is available. Use manual download; this release has no verification digest.").arg(release_.version)
                      : tr("Version %1 is available.").arg(release_.version));
        if (!ready_ && state_["mode"].toInt() > 0 && !prefix_.isEmpty() && !release_.digest.isEmpty()) download();
    });
    emit changed();
}
void Controller::cancel() { if (reply_) reply_->abort(); }
void Controller::download() {
    if (reply_ || downloading_ || ready_ || prefix_.isEmpty() || release_.digest.isEmpty() || release_.download.isEmpty()) return;
    QFile::remove(directory_ + "/pending.json");
    const Release target = release_;
    beginProgress(0, 0, false); // The manifest is tiny and its size is not part of the total.
    if (!target.componentsDownload.isEmpty()) {
        Release manifest;
        manifest.download = target.componentsDownload; manifest.digest = target.componentsDigest; manifest.size = target.componentsSize;
        fetch(manifest, directory_ + "/components.json", [this, target] {
            Components components; QString error;
            if (!readComponents(directory_ + "/components.json", target.componentsDigest, target.version,
                                pet::platform::updateArchitecture(), components, error)) {
                downloadFull(target); return;
            }
            QStringList keep;
            for (const auto &component : components.entries) keep.append(component.archive);
            pruneComponents(directory_, keep);
            QFile::remove(directory_ + "/package.tar.gz");
            // Only what is missing counts, so the bar covers the whole update once and never restarts per file.
            QList<Component> queue; qint64 total = 0; QString ignored;
            for (const auto &component : components.entries) {
                if (componentMatches(component, prefix_) || verifiedArchive(directory_ + '/' + component.archive, component.digest, ignored)) continue;
                queue.append(component); total += component.size;
            }
            beginProgress(total, queue.size(), false);
            downloadComponent(target, queue, 0);
        }, [this, target] { downloadFull(target); });
    } else {
        downloadFull(target);
    }
}
void Controller::downloadFull(const Release &target) {
    pruneComponents(directory_);
    QFile::remove(directory_ + "/components.json");
    beginProgress(target.size, 1, true); // Also the fallback after components: the total is now the whole package.
    fetch(target, directory_ + "/package.tar.gz", [this, target] { finishDownload(target, false); });
}
void Controller::downloadComponent(const Release &target, const QList<Component> &queue, int index) {
    if (index == queue.size()) { finishDownload(target, true); return; }
    const auto component = queue[index];
    file_ = index + 1;
    Release asset;
    asset.download = target.componentsDownload.resolved(QUrl(component.archive));
    asset.digest = component.digest; asset.size = component.size;
    fetch(asset, directory_ + '/' + component.archive, [this, target, queue, index] { downloadComponent(target, queue, index + 1); },
          [this, target] { downloadFull(target); });
}
void Controller::finishDownload(const Release &target, bool components) {
    auto pending = target.json();
    if (components) { pending["kind"] = "components"; pending["digest"] = target.componentsDigest; }
    downloading_ = false; percent_ = -1;
    if (!writeObject(directory_ + "/pending.json", pending)) { status(tr("Cannot save the pending update.")); return; }
    ready_ = true; status(tr("Update %1 is ready. The pet will restart and restore running sessions.").arg(target.version));
    autoInstall();
}
void Controller::beginProgress(qint64 total, int files, bool full) {
    done_ = 0; total_ = total; files_ = files; file_ = files > 0 ? 1 : 0; full_ = full; percent_ = -1;
}
void Controller::reportProgress(qint64 current) {
    if (total_ <= 0) { percent_ = -1; status(tr("Downloading update…")); return; }
    percent_ = int(std::min(done_ + current, total_) * 100 / total_);
    if (files_ > 1) {
        //: %1 = percent of the whole update, %2 = number of the file being downloaded, %3 = how many files
        status(tr("Downloading update… %1% (file %2 of %3)").arg(percent_).arg(file_).arg(files_));
    } else if (full_) {
        //: %1 = percent done
        status(tr("Downloading full update… %1%").arg(percent_));
    } else {
        //: %1 = percent done
        status(tr("Downloading update… %1%").arg(percent_));
    }
}
void Controller::fetch(const Release &target, const QString &path, std::function<void()> complete,
                       std::function<void()> fallback) {
    downloading_ = true;
    auto file = std::make_shared<QSaveFile>(path);
    if (!file->open(QIODevice::WriteOnly)) { downloading_ = false; percent_ = -1; status(tr("Cannot save the download. Check free disk space.")); return; }
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
    connect(reply, &QNetworkReply::downloadProgress, this, [this, target](qint64 bytes, qint64) { reportProgress(std::min(bytes, target.size)); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, received, target, path, complete, fallback] {
        reply_ = nullptr; reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
            || *received != target.size || !file->commit()) {
            file->cancelWriting();
            if (fallback && reply->error() != QNetworkReply::OperationCanceledError) { fallback(); return; }
            downloading_ = false; percent_ = -1; status(tr("Download stopped or failed. You can retry.")); return;
        }
        QString error;
        if (!verifiedArchive(path, target.digest, error)) {
            QFile::remove(path);
            if (fallback) { fallback(); return; }
            downloading_ = false; percent_ = -1; status(error); return;
        }
        done_ += target.size;
        complete();
    });
    reportProgress(0);
}
void Controller::install() { installReady(false); }
void Controller::installReady(bool automatic) {
    if (!ready_ || prefix_.isEmpty() || reply_ || downloading_) return;
    if (prepareRestart && !prepareRestart()) { status(tr("Could not save running sessions. Free disk space or check permissions, then retry the update.")); return; }
    if (automatic) {
        state_["autoInstalled"] = release_.version;
        if (!save()) { state_.remove("autoInstalled"); return; }
    }
    const auto pending = readObject(directory_ + "/pending.json");
    QStringList args{applyCommand(pending), prefix_, pendingFile(directory_, pending), pending["digest"].toString(), release_.version,
                     QString::number(QCoreApplication::applicationPid())};
    if (!QProcess::startDetached(helper(prefix_), args)) { status(tr("Could not start the update installer.")); return; }
    emit restartRequested();
}
void Controller::autoInstall() {
    if (state_["mode"].toInt() != 3 || !ready_ || reply_ || downloading_ || prefix_.isEmpty()) return;
    const QString version = release_.version;
    // One attempt per version: a rolled-back update must not restart the pet in a loop.
    if (state_["skipped"].toString() == version || state_["autoInstalled"].toString() == version) return;
    installReady(true);
}
void Controller::showSettings(QWidget *parent) {
    if (dialog_) { dialog_->show(); dialog_->raise(); return; }
    auto *dialog = new QDialog(parent); dialog_ = dialog;
    dialog->setWindowTitle(tr("Agent Pet updates")); dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog); layout->addWidget(settings(dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Later / Close"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons); dialog->resize(460, 500); dialog->show();
}
void Controller::retranslate(QWidget *parent) {
    auto *dialog = dialog_.data(); if (!dialog) return;
    const auto position = dialog->pos();
    dialog_.clear(); dialog->close(); // Deleted later, so a new one is built rather than the old one raised.
    QTimer::singleShot(0, parent, [this, parent, position] { showSettings(parent); dialog_->move(position); });
}
QWidget *Controller::settings(QWidget *parent) {
    auto *box = new QGroupBox(tr("Updates"), parent); auto *layout = new QVBoxLayout(box);
    auto *version = new QLabel(tr("Installed version: %1").arg(AGENT_PET_VERSION), box); layout->addWidget(version);
    auto *enabled = new QCheckBox(tr("Check automatically once a day"), box); enabled->setChecked(state_["enabled"].toBool(true)); layout->addWidget(enabled);
    auto *mode = new QComboBox(box); mode->addItems({tr("Notify only"), tr("Download automatically"), tr("Install automatically on next launch"), tr("Download and install automatically (restore running sessions)")});
    mode->setCurrentIndex(state_["mode"].toInt()); layout->addWidget(mode);
    enabled->setEnabled(writable_); mode->setEnabled(writable_ && !prefix_.isEmpty());
    if (prefix_.isEmpty()) { auto *hint = new QLabel(tr("This copy supports notifications and manual downloads. Install a release bundle to enable automatic updates."), box); hint->setWordWrap(true); layout->addWidget(hint); }
    auto *last = new QLabel(box); layout->addWidget(last);
    auto *message = new QLabel(box); message->setWordWrap(true); message->setTextFormat(Qt::PlainText); layout->addWidget(message);
    auto *bar = new QProgressBar(box); bar->setRange(0, 100); bar->setVisible(false); layout->addWidget(bar);
    auto *checkButton = new QPushButton(tr("Check now"), box); layout->addWidget(checkButton);
    auto *notes = new QPushButton(tr("View release / manual download"), box); layout->addWidget(notes);
    auto *downloadButton = new QPushButton(tr("Download update"), box); layout->addWidget(downloadButton);
    auto *installButton = new QPushButton(tr("Restart and update"), box); layout->addWidget(installButton);
    auto *skip = new QPushButton(tr("Skip this version"), box); layout->addWidget(skip);
    auto *cancelButton = new QPushButton(tr("Cancel download / check"), box); layout->addWidget(cancelButton);
    const auto refresh = [=] {
        const auto checked = state_["checked"].toInteger();
        last->setText(checked ? tr("Last checked: %1").arg(QDateTime::fromSecsSinceEpoch(checked).toLocalTime().toString("yyyy-MM-dd hh:mm")) : tr("Not checked yet"));
        message->setText(message_);
        bar->setVisible(downloading_); bar->setRange(0, percent_ < 0 ? 0 : 100); if (percent_ >= 0) bar->setValue(percent_); // 0-0 = busy
        checkButton->setEnabled(!reply_ && !downloading_ && writable_);
        notes->setEnabled(!release_.page.isEmpty());
        downloadButton->setEnabled(!reply_ && !downloading_ && !ready_ && !prefix_.isEmpty() && !release_.digest.isEmpty() && !release_.download.isEmpty());
        installButton->setEnabled(ready_ && !reply_ && !downloading_ && !prefix_.isEmpty()); skip->setEnabled(!release_.version.isEmpty() && !reply_ && !downloading_);
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
        QFile::remove(directory_ + "/pending.json"); QFile::remove(directory_ + "/package.tar.gz");
        QFile::remove(directory_ + "/components.json");
        pruneComponents(directory_);
        status(tr("This version will be skipped. Check now to see it again.")); });
    return box;
}
}
