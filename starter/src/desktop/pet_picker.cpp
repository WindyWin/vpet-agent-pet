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
    const bool known = std::any_of(pets.begin(), pets.end(), [&](const PetInfo &pet) { return pet.id == saved; });
    selected_ = known ? saved : running;
    for (const auto &pet : pets) {
        auto *tile = new QToolButton(this);
        tile->setCheckable(true);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        // A missing preview leaves a tile with just the name.
        if (const QPixmap preview(pet.preview); !preview.isNull())
            tile->setIcon(QIcon(preview.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        tile->setIconSize({96, 96});
        tile->setAccessibleName(pet.name);
        tile->setProperty("pet", pet.id);
        tile->setStyleSheet("QToolButton:checked { border: 2px solid palette(highlight); border-radius: 6px; }");
        connect(tile, &QToolButton::toggled, this, [this, tile] { label(tile); });
        tile->setChecked(pet.id == selected_);
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
        showNote();
        emit chosen(id);
    });
    showNote();
    if (pets.size() < 2) hide();
}
void PetPicker::showNote() {
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
    const auto *pet = find(tile->property("pet").toString());
    if (!pet) return;
    const auto name = QString(pet->name).replace('&', "&&");
    const auto size = QLocale().formattedDataSize(pet->download);
    tile->setText(tile->isChecked() ? "✓ " + name : pet->download > 0 ? "⬇ " + name : name);
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
    for (auto *button : group_->buttons()) button->setEnabled(true);
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
    if (selected_ != id) { selected_ = id; emit chosen(id); }
    showNote();
}
}
