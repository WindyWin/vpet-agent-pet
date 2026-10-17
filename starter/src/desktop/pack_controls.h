#pragma once
#include <QString>
#include <functional>
class QWidget;
class QEvent;
namespace pet {
void importPack(QWidget *parent, const QString &path);
QWidget *packControls(QWidget *parent, const QString &kind, std::function<void(const QString &)> removed);
// Handles drag enter/drop for a widget accepting one local .agentpet file.
bool packDrop(QWidget *parent, QEvent *event);
}
