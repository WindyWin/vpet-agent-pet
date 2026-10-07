#include "pet_picker.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace pet {
PetPicker::PetPicker(const QVector<PetInfo> &pets, const QString &saved, const QString &running, QWidget *parent)
    : QWidget(parent), pets_(pets), running_(running), group_(new QButtonGroup(this)), note_(new QLabel(this)) {
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
        tile->setToolTip(tr("by %1").arg(pet.author.toHtmlEscaped())); // Tooltips detect rich text.
        tile->setProperty("pet", pet.id);
        tile->setStyleSheet("QToolButton:checked { border: 2px solid palette(highlight); border-radius: 6px; }");
        // Names are shown as written (`&&` is a literal `&`, not a mnemonic); the checked tile also carries a check mark.
        const auto label = [tile, name = QString(pet.name).replace('&', "&&")](bool checked) {
            tile->setText(checked ? "✓ " + name : name);
        };
        connect(tile, &QToolButton::toggled, tile, label);
        tile->setChecked(pet.id == selected_);
        label(tile->isChecked());
        if (tile->isChecked()) setFocusProxy(tile);
        group_->addButton(tile);
        tiles->addWidget(tile);
    }
    tiles->addStretch();
    note_->setTextFormat(Qt::PlainText); // The pet's name is shown as written.
    note_->setWordWrap(true);
    layout->addWidget(note_);
    connect(group_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *tile) {
        const auto id = tile->property("pet").toString();
        if (id == selected_) return;
        selected_ = id;
        showNote();
        emit chosen(id);
    });
    showNote();
    if (pets.size() < 2) hide();
}
void PetPicker::showNote() {
    const auto pet = std::find_if(pets_.begin(), pets_.end(), [this](const PetInfo &pet) { return pet.id == selected_; });
    note_->setVisible(selected_ != running_ && pet != pets_.end());
    if (pet != pets_.end()) note_->setText(tr("%1 will appear the next time Agent Pet starts.").arg(pet->name));
}
}
