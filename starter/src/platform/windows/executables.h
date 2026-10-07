#pragma once
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace pet::platform {
// Windows builds two executables from one source: agent-pet.exe, a GUI program that opens no
// console window, and agent-pet-cli.exe, a console program for hooks and commands, whose
// standard streams work in every shell and agent. Each maps a path to its sibling, if present.
inline QString siblingExecutable(const QString &path, const QString &from, const QString &to) {
    const QFileInfo info(path);
    if (info.fileName().compare(from, Qt::CaseInsensitive) != 0) return path;
    const auto sibling = info.dir().filePath(to);
    return QFileInfo(sibling).isFile() ? sibling : path;
}
inline QString guiExecutable(const QString &path) { return siblingExecutable(path, "agent-pet-cli.exe", "agent-pet.exe"); }
inline QString consoleExecutable(const QString &path) { return siblingExecutable(path, "agent-pet.exe", "agent-pet-cli.exe"); }
}
