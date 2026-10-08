#include "plugin_list.h"
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

namespace pet {
PluginList::PluginList(const QVector<PluginPack> &packs, const QStringList &enabled, const QString &running, const QString &folder,
                       QWidget *parent)
    : QWidget(parent), packs_(packs), enabled_(enabled), running_(running), list_(new QListWidget(this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *intro = new QLabel(tr("Plugin packs add animations to a pet and new ways to play its reactions. Put each pack in "
                                "its own folder in %1. Changes apply the next time Agent Pet starts.")
                                 .arg(QDir::toNativeSeparators(folder)), this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::PlainText);
    intro->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(intro);
    list_->setAccessibleName(tr("Plugin packs"));
    list_->setWordWrap(true);
    list_->setAlternatingRowColors(true);
    list_->setSpacing(2);
    layout->addWidget(list_);
    for (int row = 0; row < packs_.size(); ++row) {
        const auto &pack = packs_[row];
        auto *item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, row);
        // A pack that cannot be used cannot be chosen, but its saved choice stays for when it is fixed.
        if (pack.status == PluginPack::Invalid) item->setFlags(Qt::ItemIsEnabled);
        else item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled_.contains(pack.id) && pack.status != PluginPack::Invalid ? Qt::Checked : Qt::Unchecked);
        auto tip = QDir::toNativeSeparators(pack.folder);
        if (!pack.url.isEmpty()) tip += "\n" + pack.url;
        item->setToolTip(tip);
        label(item);
    }
    auto *empty = new QLabel(tr("No plugin packs are installed."), this);
    layout->addWidget(empty);
    list_->setVisible(!packs_.isEmpty());
    empty->setVisible(packs_.isEmpty());
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const auto &pack = packs_[item->data(Qt::UserRole).toInt()];
        const bool checked = item->checkState() == Qt::Checked;
        if (checked == enabled_.contains(pack.id)) return; // Only the label changed.
        if (checked) enabled_.append(pack.id);
        else enabled_.removeAll(pack.id);
        label(item);
        emit changed(enabled_);
    });
    auto *buttons = new QHBoxLayout;
    auto *open = new QPushButton(tr("Open plugins &folder"), this);
    connect(open, &QPushButton::clicked, this, [folder] {
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    buttons->addWidget(open);
    buttons->addStretch();
    layout->addLayout(buttons);
}
QString PluginList::status(const PluginPack &pack, bool checked) const {
    if (pack.status == PluginPack::Invalid) return tr("Cannot be used: %1").arg(pack.error);
    if (!checked) return pack.status == PluginPack::Applied ? tr("Unloads the next time Agent Pet starts") : tr("Off");
    switch (pack.status) {
    case PluginPack::Applied: return tr("Loaded");
    case PluginPack::Rejected: return tr("Not loaded: %1").arg(pack.error);
    default: break;
    }
    if (pack.pet != running_) return tr("For the pet %1: loads when it runs").arg(pack.pet);
    return tr("Loads the next time Agent Pet starts");
}
void PluginList::label(QListWidgetItem *item) {
    const auto &pack = packs_[item->data(Qt::UserRole).toInt()];
    QString text = pack.version.isEmpty() ? pack.name : pack.name + " " + pack.version;
    if (!pack.license.isEmpty()) text += "\n" + tr("by %1 · license: %2").arg(pack.author, pack.license);
    else if (!pack.author.isEmpty()) text += "\n" + tr("by %1").arg(pack.author);
    text += "\n" + status(pack, item->checkState() == Qt::Checked);
    const QSignalBlocker quiet(list_); // Relabeling is not a choice.
    item->setText(text);
    item->setData(Qt::AccessibleTextRole, text);
}
}
