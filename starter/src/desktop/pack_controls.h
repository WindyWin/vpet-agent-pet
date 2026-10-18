#pragma once
#include <QString>
#include "packs/installer.h"
#include <functional>
class QWidget;
class QEvent;
namespace pet {
void reviewPack(QWidget *parent, const QString &path, std::unique_ptr<packs::Candidate> candidate, const QJsonObject &report = {});
void importPack(QWidget *parent, const QString &path);
QWidget *packControls(QWidget *parent, const QString &kind, std::function<void(const QString &, const QString &)> removed);
// Handles drag enter/drop for a widget accepting one local .agentpet file.
bool packDrop(QWidget *parent, QEvent *event);
}
