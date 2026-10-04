#pragma once
#include "sessions/alerts.h"
#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace pet {
// One compact bubble beside the pet. It reports; replies happen in the agent's own host.
class AlertBubble : public QWidget {
    Q_OBJECT
public:
    explicit AlertBubble(QWidget *parent = nullptr);
    void present(const AlertText &text, int more);
    void place(const QRect &pet, const QVector<QRect> &screens);
    // Right of the pet, else left, else above; always clamped to the pet's screen.
    static QPoint placement(const QRect &pet, QSize bubble, const QRect &screen);
    QString title() const { return title_->text(); }
    QString label() const { return label_->text(); }
    QString footer() const { return more_->text(); }
signals:
    void nextRequested();
    void dismissRequested();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QLabel *title_, *label_, *more_;
    QPushButton *next_, *dismiss_;
};
}
