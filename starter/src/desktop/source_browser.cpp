#include "source_browser.h"
#include "packs/installer.h"
#include "animation/pet_library.h"
#include "version.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace pet {
namespace {
QJsonObject installed(const packs::Entry &e) {
    const auto kind = e.metadata["kind"].toString(), id = e.metadata["id"].toString();
    QFile file(packs::dataFolder() + (kind == "pet" ? "/packs/pets/" : "/plugins/") + id + "/pack.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool pending(const packs::Entry &e) {
    return QFileInfo::exists(packs::dataFolder() + "/pack-pending/" + e.metadata["kind"].toString() + '-' + e.metadata["id"].toString());
}
QString compatible(const packs::Entry &e) {
    QStringList pets;
    for (const auto &p : PetLibrary::shared().pets()) if (p.error.isEmpty()) pets.append(p.id);
    return packs::compatibility(e, AGENT_PET_VERSION, pets);
}
QLabel *label(QWidget *parent) { auto *l = new QLabel(parent); l->setTextFormat(Qt::PlainText); l->setWordWrap(true); return l; }
}
SourceBrowser::SourceBrowser(QWidget *parent, const QString &sourceFolder) : QDialog(parent),
    store_(sourceFolder.isEmpty() ? packs::dataFolder() + "/sources" : sourceFolder),
    transfer_(&network_, this), previewTransfer_(&network_, this) {
    setAttribute(Qt::WA_DeleteOnClose); setWindowTitle(tr("Pack sources")); resize(760, 650);
    auto *layout = new QVBoxLayout(this); auto *row = new QHBoxLayout;
    sources_ = new QComboBox(this); sources_->setAccessibleName(tr("Source")); row->addWidget(sources_, 1);
    add_ = new QPushButton(tr("Add source…"), this); row->addWidget(add_);
    removeSource_ = new QPushButton(tr("Remove source"), this); row->addWidget(removeSource_);
    refresh_ = new QPushButton(tr("Refresh"), this); row->addWidget(refresh_); layout->addLayout(row);
    notice_ = label(this); notice_->setText(tr("Catalogs are cached for offline browsing. Refresh contacts the selected source; selecting a pack loads its preview. Third-party sources are unverified. Installing a plugin does not enable it or connect an external integration.")); layout->addWidget(notice_);
    list_ = new QListWidget(this); list_->setAccessibleName(tr("Available packs")); layout->addWidget(list_, 1);
    auto *info = new QHBoxLayout; preview_ = label(this); preview_->setFixedSize(128, 128); info->addWidget(preview_);
    details_ = new QPlainTextEdit(this); details_->setReadOnly(true); details_->setAccessibleName(tr("Pack details")); details_->setMaximumHeight(220); info->addWidget(details_, 1); layout->addLayout(info);
    row = new QHBoxLayout; install_ = new QPushButton(tr("Install…"), this); row->addWidget(install_);
    remove_ = new QPushButton(tr("Remove pack…"), this); row->addWidget(remove_);
    direct_ = new QPushButton(tr("Install from URL…"), this); row->addWidget(direct_); layout->addLayout(row);
    progress_ = new QProgressBar(this); progress_->hide(); layout->addWidget(progress_);
    status_ = label(this); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
    auto *buttons = new QDialogButtonBox(this); cancel_ = buttons->addButton(tr("Cancel download"), QDialogButtonBox::ActionRole); cancel_->setEnabled(false);
    close_ = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole); connect(close_, &QPushButton::clicked, this, &QDialog::close); layout->addWidget(buttons);
    connect(cancel_, &QPushButton::clicked, &transfer_, &packs::Download::cancel);
    connect(sources_, &QComboBox::currentIndexChanged, this, [this] { loadCatalog(); });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { select(); });
    connect(refresh_, &QPushButton::clicked, this, &SourceBrowser::refresh);
    connect(install_, &QPushButton::clicked, this, &SourceBrowser::download);
    connect(direct_, &QPushButton::clicked, this, &SourceBrowser::direct);
    connect(add_, &QPushButton::clicked, this, [this] {
        const auto input = QInputDialog::getText(this, tr("Add source"), tr("HTTPS catalog URL:")).trimmed(); if (input.isEmpty()) return;
        if (!confirm(tr("Third-party source"), tr("This source is not verified by Agent Pet. Its operator controls its catalog, downloads and updates. Only add sources you trust.\n\n%1").arg(input), tr("Add source"))) return;
        QString error; const auto url = QUrl(input).toString(QUrl::FullyEncoded);
        if (!store_.add(url, error)) { status_->setText(error); return; }
        loadSources(url);
    });
    connect(removeSource_, &QPushButton::clicked, this, [this] {
        const auto url = sources_->currentData().toString();
        if (!confirm(tr("Remove source"), tr("Remove %1 from the source list? Installed packs will be kept.").arg(url), tr("Remove source"))) return;
        QString error; if (!store_.remove(url, error)) status_->setText(error); else loadSources();
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const auto i = list_->currentRow(); if (i < 0 || i >= entries_.size()) return;
        const auto e = entries_[i];
        if (!confirm(tr("Remove pack"), tr("Remove %1 on the next start? Removing the selected pet switches to VPet.").arg(e.metadata["name"].toString()), tr("Remove pack"))) return;
        QString error;
        if (!packs::remove(e.metadata["kind"].toString(), e.metadata["id"].toString(), error)) status_->setText(error);
        else { emit packRemoved(e.metadata["kind"].toString(), e.metadata["id"].toString());
            loadCatalog(); status_->setText(tr("Pack change queued. Restart Agent Pet to apply it.")); }
    });
    connect(&transfer_, &packs::Download::completed, this, &SourceBrowser::received);
    connect(&transfer_, &packs::Download::failed, this, [this](const QString &error) { busy(false); status_->setText(error + '\n' + tr("Cached catalogs and installed packs are unchanged.")); });
    connect(&transfer_, &packs::Download::progress, this, [this](qint64 bytes, qint64 total) {
        progress_->setRange(0, total > 0 ? 1000 : 0); if (total > 0) progress_->setValue(int(bytes * 1000 / total));
        status_->setText(tr("Downloaded %1 MiB").arg(double(bytes) / (1024 * 1024), 0, 'f', 1));
    });
    connect(&previewTransfer_, &packs::Download::completed, this, [this](const QString &path) {
        QImageReader reader(path); const auto size = reader.size();
        if (reader.format() != "png" || size.isEmpty() || size.width() > 512 || size.height() > 512) { preview_->setText(tr("Preview unavailable")); return; }
        const auto image = reader.read();
        if (image.isNull()) preview_->setText(tr("Preview unavailable"));
        else preview_->setPixmap(QPixmap::fromImage(image).scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    connect(&previewTransfer_, &packs::Download::failed, this, [this] { preview_->setText(tr("Preview unavailable")); });
    loadSources();
}
void SourceBrowser::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() != QEvent::LanguageChange) return;
    setWindowTitle(tr("Pack sources")); sources_->setAccessibleName(tr("Source"));
    add_->setText(tr("Add source…")); removeSource_->setText(tr("Remove source"));
    refresh_->setText(tr("Refresh")); list_->setAccessibleName(tr("Available packs"));
    details_->setAccessibleName(tr("Pack details")); remove_->setText(tr("Remove pack…"));
    direct_->setText(tr("Install from URL…")); cancel_->setText(tr("Cancel download")); close_->setText(tr("Close"));
    notice_->setText(tr("Catalogs are cached for offline browsing. Refresh contacts the selected source; selecting a pack loads its preview. Third-party sources are unverified. Installing a plugin does not enable it or connect an external integration."));
    for (int i = 0; i < sources_->count(); ++i) {
        const auto url = sources_->itemData(i).toString();
        sources_->setItemText(i, url == packs::officialSource() ? tr("Official gallery") : tr("Third-party: %1").arg(url));
    }
    const auto row = list_->currentRow();
    if (!working_) { loadCatalog(); list_->setCurrentRow(row); }
    else select();
}
void SourceBrowser::reject() {
    closing_ = true; transfer_.cancel(); previewTransfer_.cancel(); QDialog::reject();
}
bool SourceBrowser::confirm(const QString &title, const QString &text, const QString &action) {
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this); box.setTextFormat(Qt::PlainText);
    auto *accept = box.addButton(action, QMessageBox::AcceptRole); auto *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel); box.exec(); return box.clickedButton() == accept;
}
void SourceBrowser::loadSources(const QString &selected) {
    QString error; const auto urls = store_.urls(error);
    sources_->blockSignals(true); sources_->clear();
    for (const auto &url : urls) sources_->addItem(url == packs::officialSource() ? tr("Official gallery") : tr("Third-party: %1").arg(url), url);
    sources_->setCurrentIndex(qMax(0, sources_->findData(selected))); sources_->blockSignals(false); loadCatalog();
    if (!error.isEmpty()) status_->setText(error);
}
void SourceBrowser::loadCatalog() {
    QString error; entries_ = store_.cached(sources_->currentData().toString(), error);
    list_->clear();
    for (const auto &e : entries_) {
        const auto m = installed(e); QString state = tr("Not installed");
        if (!m.isEmpty()) state = packs::newerPackVersion(e.metadata["version"].toString(), m["version"].toString()) ? tr("Update available") : tr("Installed");
        if (pending(e)) state = tr("Restart required");
        list_->addItem(e.metadata["name"].toString() + " — " + e.metadata["version"].toString() + " — " + state);
    }
    status_->setText(error.isEmpty() ? tr("Showing the last valid catalog. Refresh to check for new packs.") : error);
    select();
}
void SourceBrowser::select() {
    if (closing_) return;
    previewTransfer_.cancel(); preview_->clear(); details_->clear();
    const auto i = list_->currentRow(); const bool selected = i >= 0 && i < entries_.size();
    install_->setText(tr("Install…")); install_->setEnabled(false); remove_->setEnabled(false); removeSource_->setEnabled(!working_ && sources_->currentData().toString() != packs::officialSource());
    if (!selected) return;
    const auto &e = entries_[i]; const auto reason = compatible(e); const auto m = installed(e);
    const auto oldVersion = m["version"].toString(); const bool update = !m.isEmpty() && packs::newerPackVersion(e.metadata["version"].toString(), oldVersion);
    install_->setText(update ? tr("Update…") : tr("Install…"));
    install_->setEnabled(!working_ && !pending(e) && reason.isEmpty() && (m.isEmpty() || update));
    remove_->setEnabled(!working_ && !pending(e) && !m.isEmpty());
    details_->setPlainText(tr("%1 by %2\nLicence: %3 · Requires Agent Pet %4 · Schema %5\n%6\n%7")
        .arg(e.metadata["name"].toString(), e.metadata["author"].toString(), e.metadata["license"].toString(), e.metadata["minAppVersion"].toString())
        .arg(e.metadata["schema"].toInt()).arg(e.description, reason.isEmpty() ? tr("Compatible") : reason)
        + (e.setup.isEmpty() ? QString() : '\n' + tr("Setup required: %1").arg(e.setup))
        + (e.metadata["kind"] == "plugin" ? '\n' + tr("Target pet: %1").arg(e.metadata["pet"].toString()) : QString()));
    if (!working_) previewTransfer_.start(e.preview, 4 * 1024 * 1024);
}
void SourceBrowser::busy(bool value) {
    working_ = value; sources_->setEnabled(!value); list_->setEnabled(!value); add_->setEnabled(!value);
    refresh_->setEnabled(!value); direct_->setEnabled(!value); cancel_->setEnabled(value);
    progress_->setVisible(value); select();
}
void SourceBrowser::refresh() {
    source_ = sources_->currentData().toString(); mode_ = "index"; busy(true);
    transfer_.start(QUrl(source_), packs::MaxIndex);
}
void SourceBrowser::download() {
    const auto i = list_->currentRow(); if (i < 0 || i >= entries_.size()) return;
    downloading_ = entries_[i]; const auto &e = downloading_;
    if (!confirm(tr("Download pack"), tr("Download %1 (%2 MiB)?\nLicence: %3\nSource: %4\n%5\nYou will review the archive's terms before installing.")
        .arg(e.metadata["name"].toString()).arg(double(e.bytes) / (1024 * 1024), 0, 'f', 1)
        .arg(e.metadata["license"].toString(), sources_->currentData().toString(), sources_->currentData().toString() == packs::officialSource() ? tr("Official gallery") : tr("Unverified third-party source")), tr("Download"))) return;
    source_ = sources_->currentData().toString();
    mode_ = "pack"; busy(true); transfer_.start(e.download, packs::MaxDownload, e.bytes, e.digest);
}
void SourceBrowser::direct() {
    const auto url = QInputDialog::getText(this, tr("Install from URL"), tr("HTTPS .agentpet URL:")).trimmed(); if (url.isEmpty()) return;
    if (!confirm(tr("Unpinned third-party download"), tr("This direct download has no catalog digest. Its origin and contents are unverified; HTTPS alone does not establish trust.\n\n%1\n\nYou will review the pack and its terms before installing.").arg(url), tr("Download"))) return;
    source_ = url;
    mode_ = "direct"; busy(true); transfer_.start(QUrl(url), packs::MaxDownload);
}
void SourceBrowser::received(const QString &path) {
    QString error;
    if (mode_ == "index") {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) error = tr("Cannot read downloaded catalog.");
        else store_.cache(source_, file.readAll(), error);
        busy(false); loadCatalog(); if (!error.isEmpty()) status_->setText(error + '\n' + tr("Cached catalogs and installed packs are unchanged."));
        return;
    }
    cancel_->setEnabled(false); status_->setText(tr("Validating pack…"));
    auto candidate = packs::inspect(path, error);
    if (candidate && mode_ == "pack" && !packs::matches(downloading_, candidate->metadata)) { candidate.reset(); error = tr("Archive metadata does not match the catalog."); }
    if (!candidate) { busy(false); status_->setText(error); return; }
    const auto m = candidate->metadata;
    auto *consent = new QDialog(this); consent->setWindowTitle(tr("Review pack terms")); consent->resize(580, 520);
    auto *layout = new QVBoxLayout(consent); auto *summary = label(consent);
    summary->setText(tr("%1 by %2\nVersion: %3 · Licence: %4\nCompatible with this app.\nInstalled version: %5\nPlugins remain disabled unless already enabled. External integrations require separate setup.")
        .arg(m["name"].toString(), m["author"].toString(), m["version"].toString(), m["license"].toString(), candidate->oldVersion.isEmpty() ? tr("Not installed") : candidate->oldVersion) + '\n' + tr("Source: %1").arg(source_)); layout->addWidget(summary);
    auto *terms = new QPlainTextEdit(candidate->terms, consent); terms->setReadOnly(true); layout->addWidget(terms);
    auto *buttons = new QDialogButtonBox(consent); auto *accept = buttons->addButton(tr("Accept terms and install"), QDialogButtonBox::AcceptRole);
    auto *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole); connect(accept, &QPushButton::clicked, consent, &QDialog::accept); connect(cancel, &QPushButton::clicked, consent, &QDialog::reject); layout->addWidget(buttons);
    QPointer<SourceBrowser> self(this);
    const auto result = consent->exec();
    if (!self) return;
    consent->deleteLater();
    if (result == QDialog::Accepted) {
        if (packs::install(*candidate, error)) error = tr("Pack change queued. Restart Agent Pet to apply it.");
    } else error = tr("Installation cancelled. Installed packs are unchanged.");
    busy(false); loadCatalog(); status_->setText(error);
}
}
