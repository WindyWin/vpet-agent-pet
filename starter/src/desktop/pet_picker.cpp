#include "pet_picker.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace pet {
PetPicker::PetPicker(const QVector<PetInfo> &pets, const QString &saved, const QString &running, QWidget *parent)
    : QWidget(parent), pets_(pets), running_(running), group_(new QButtonGroup(this)), note_(new QLabel(this)),
      progress_(new QProgressBar(this)), cancel_(new QPushButton(this)) {
    setAccessibleName(tr("Pet character"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tiles = new QHBoxLayout;
    layout->addLayout(tiles);
    // A saved choice this build does not know (from a newer version) shows the pet that runs instead.
    const bool known = std::any_of(pets.begin(), pets.end(), [&](const PetInfo &pet) { return pet.id == saved && pet.error.isEmpty(); });
    selected_ = known ? saved : running;
    if (!known && saved != running) {
        const auto *missing = find(saved);
        unavailable_ = tr("Pet %1 is unavailable: %2. Using %3 for this run.")
            .arg(saved, missing ? missing->error : tr("The pet is not installed."), running);
    }
    int index = 0;
    for (const auto &pet : pets) {
        auto *tile = new QToolButton(this);
        tile->setCheckable(true);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        // A missing preview leaves a tile with just the name.
        if (const QPixmap preview(pet.error.isEmpty() ? pet.preview : QString()); !preview.isNull())
            tile->setIcon(QIcon(preview.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        tile->setIconSize({96, 96});
        tile->setAccessibleName(pet.name);
        tile->setProperty("pet", pet.id);
        tile->setProperty("petIndex", index++);
        tile->setEnabled(pet.error.isEmpty());
        tile->setStyleSheet("QToolButton:checked { border: 2px solid palette(highlight); border-radius: 6px; }");
        connect(tile, &QToolButton::toggled, this, [this, tile] { label(tile); });
        tile->setChecked(pet.id == selected_ && pet.error.isEmpty());
        label(tile);
        if (tile->isChecked()) setFocusProxy(tile);
        group_->addButton(tile);
        tiles->addWidget(tile);
    }
    tiles->addStretch();
    note_->setTextFormat(Qt::PlainText); // The pet's name is shown as written.
    note_->setWordWrap(true);
    layout->addWidget(note_);
    auto *download = new QHBoxLayout;
    progress_->setRange(0, 100);
    progress_->setAccessibleName(tr("Pet download progress"));
    cancel_->setText(tr("Cancel download"));
    download->addWidget(progress_, 1);
    download->addWidget(cancel_);
    layout->addLayout(download);
    progress_->hide(); cancel_->hide();
    connect(cancel_, &QPushButton::clicked, this, &PetPicker::cancelRequested);
    connect(group_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *tile) {
        const auto id = tile->property("pet").toString();
        if (const auto *pet = find(id); pet && pet->download > 0) {
            // Stays on the current choice until the download completes.
            if (auto *current = this->tile(selected_)) current->setChecked(true);
            emit downloadRequested(id);
            return;
        }
        if (id == selected_) return;
        selected_ = id;
        unavailable_.clear();
        showNote();
        emit chosen(id);
    });
    showNote();
    if (pets.size() < 2 && unavailable_.isEmpty()) hide();
}
void PetPicker::showNote() {
    if (!unavailable_.isEmpty()) { note_->setText(unavailable_); note_->show(); return; }
    const auto *pet = find(selected_);
    note_->setVisible(selected_ != running_ && pet);
    if (pet) note_->setText(tr("%1 will appear the next time Agent Pet starts.").arg(pet->name));
}
const PetInfo *PetPicker::find(const QString &id) const {
    const auto pet = std::find_if(pets_.begin(), pets_.end(), [&](const PetInfo &pet) { return pet.id == id; });
    return pet == pets_.end() ? nullptr : &*pet;
}
QAbstractButton *PetPicker::tile(const QString &id) const {
    for (auto *button : group_->buttons()) if (button->property("pet").toString() == id) return button;
    return nullptr;
}
// Names are shown as written (`&&` is a literal `&`, not a mnemonic); the checked tile also carries a check mark,
// and a pet still to download its size.
void PetPicker::label(QAbstractButton *tile) {
    const auto *pet = &pets_.at(tile->property("petIndex").toInt());
    const auto name = QString(pet->name).replace('&', "&&");
    const auto size = QLocale().formattedDataSize(pet->download);
    //: A pet tile's label; %1 = the pet's name. Marks the chosen pet.
    tile->setText(tile->isChecked() ? tr("✓ %1").arg(name) : pet->download > 0
                  //: A pet tile's label; %1 = the pet's name. Marks a pet that still has to be downloaded.
                  ? tr("⬇ %1").arg(name) : name);
    if (!pet->folder.isEmpty()) {
        tile->setText(tile->text() + "\n" + tr("Local"));
        tile->setToolTip(tr("Local · by %1 · %2").arg(pet->author.toHtmlEscaped(), pet->license.toHtmlEscaped())
                        + (pet->error.isEmpty() ? QString() : "<br>" + tr("Cannot be used: %1").arg(pet->error.toHtmlEscaped())));
        return;
    }
    // Tooltips detect rich text, so the author is escaped.
    tile->setToolTip(pet->download > 0 ? tr("by %1 · %2 to download").arg(pet->author.toHtmlEscaped(), size)
                                       : tr("by %1").arg(pet->author.toHtmlEscaped()));
}
void PetPicker::downloading(const QString &id, qint64 done, qint64 total) {
    const auto *pet = find(id);
    if (!pet) return;
    for (auto *button : group_->buttons()) button->setEnabled(false);
    progress_->setValue(total > 0 ? int(qBound(qint64(0), done * 100 / total, qint64(100))) : 0);
    progress_->show(); cancel_->show();
    note_->setText(tr("Downloading %1…").arg(pet->name));
    note_->show();
}
void PetPicker::downloaded(const QString &id, const QString &error) {
    for (auto *button : group_->buttons())
        button->setEnabled(pets_.at(button->property("petIndex").toInt()).error.isEmpty());
    progress_->hide(); cancel_->hide();
    const auto pet = std::find_if(pets_.begin(), pets_.end(), [&](const PetInfo &pet) { return pet.id == id; });
    if (pet == pets_.end()) return;
    if (!error.isEmpty()) {
        note_->setText(error);
        note_->show();
        return;
    }
    pet->download = 0;
    auto *done = tile(id);
    if (done) { label(done); done->setChecked(true); }
    if (selected_ != id) { selected_ = id; unavailable_.clear(); emit chosen(id); }
    showNote();
}
}
