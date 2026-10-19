#pragma once
#include <QStringList>
#include <QVector>
namespace pet::packs {
struct WorkshopItem { QString id, path; };
QStringList steamRoots();
// Explicit roots make discovery testable without touching the user's Steam installation.
QVector<WorkshopItem> steamWorkshopItems(const QStringList &roots);
}
