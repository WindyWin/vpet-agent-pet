#pragma once
#include "animation/plugins.h"
#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace pet {
// Settings' Plugins tab: each pack in the plugin folder with a checkbox, its author, license and what became of it
// at startup. Choices apply on the next start, so each line says what the next start will do while the choice
// differs from what is running. A pack whose plugin.json cannot be used shows why and cannot be checked; its saved
// choice is kept for when it is fixed.
class PluginList : public QWidget {
    Q_OBJECT
public:
    // `packs` as PetLibrary::plugins() lists them, `enabled` the saved choice, `running` the pet on screen and `folder`
    // where packs are installed.
    PluginList(const QVector<PluginPack> &packs, const QStringList &enabled, const QString &running, const QString &folder,
               QWidget *parent = nullptr);
    QStringList enabled() const { return enabled_; }
    // What a pack's line says it is doing or will do.
    QString status(const PluginPack &pack, bool checked) const;
signals:
    void changed(const QStringList &enabled); // A checkbox was toggled.
private:
    void label(QListWidgetItem *item);
    QVector<PluginPack> packs_;
    QStringList enabled_;
    QString running_;
    QListWidget *list_;
};
}
