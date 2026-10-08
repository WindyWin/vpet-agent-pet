#pragma once
#include "animation/pet_library.h"
#include <QWidget>

class QButtonGroup;
class QLabel;

namespace pet {
// Settings' Character row: a checkable preview tile per pet. A choice applies on the next start, so the
// picker takes the saved choice and the running pet, and notes the restart while they differ. Hidden with
// fewer than two pets.
class PetPicker : public QWidget {
    Q_OBJECT
public:
    PetPicker(const QVector<PetInfo> &pets, const QString &saved, const QString &running, QWidget *parent = nullptr);
    QString selected() const { return selected_; }
signals:
    void chosen(const QString &id); // A tile other than the selected one was clicked.
private:
    void showNote();
    QVector<PetInfo> pets_;
    QString selected_, running_;
    QButtonGroup *group_;
    QLabel *note_;
};
}
