#pragma once
#include "animation/pet_library.h"
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QLabel;
class QProgressBar;
class QPushButton;

namespace pet {
// Settings' Character row: a checkable preview tile per pet. A choice applies on the next start, so the
// picker takes the saved choice and the running pet, and notes the restart while they differ. A pet that still
// has packs to download shows their size; choosing it asks for the download, whose progress the picker shows
// with a Cancel button, and becomes the choice once complete. Hidden with fewer than two pets.
class PetPicker : public QWidget {
    Q_OBJECT
public:
    PetPicker(const QVector<PetInfo> &pets, const QString &saved, const QString &running, QWidget *parent = nullptr);
    QString selected() const { return selected_; }
    // A download is running (`done` of `total` bytes): the tiles wait and Cancel shows.
    void downloading(const QString &id, qint64 done, qint64 total);
    // The download ended: complete (empty `error`) makes the pet the choice; otherwise the note says why.
    void downloaded(const QString &id, const QString &error);
signals:
    void chosen(const QString &id); // A tile other than the selected one was clicked, or its download completed.
    void downloadRequested(const QString &id); // A pet with packs still to download was clicked.
    void cancelRequested();
private:
    void showNote();
    void label(QAbstractButton *tile);
    QAbstractButton *tile(const QString &id) const;
    const PetInfo *find(const QString &id) const;
    QVector<PetInfo> pets_;
    QString selected_, running_;
    QButtonGroup *group_;
    QLabel *note_;
    QProgressBar *progress_;
    QPushButton *cancel_;
};
}
