#pragma once
#include <QString>
namespace pet::packs {
// Strict ZIP32 reader. Extracts into a new, private directory only.
bool extract(const QString &archive, const QString &destination, QString &error);
bool portablePath(const QString &path);
}
