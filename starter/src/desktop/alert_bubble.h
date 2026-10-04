#pragma once
#include "sessions/alerts.h"
#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace pet {
// One compact, single-line toast beside the pet: "● Needs approval  fcis-web  +2  Open  ×".
// Provider, session ID and path live in the tooltip. Clicking it opens the agent's
// own terminal or editor; replies happen there.
class AlertBubble : public QWidget {
    Q_OBJECT
public:
    explicit AlertBubble(QWidget *parent = nullptr);
    void present(const AlertText &text, const QString &kind, int more);
    void place(const QRect &pet, const QVector<QRect> &screens);
    // Right of the pet, else left, else above; always clamped to the pet's screen.
    static QPoint placement(const QRect &pet, QSize bubble, const QRect &screen);
    QString title() const { return title_->text(); }
    QString label() const { return label_; }
    QString footer() const { return more_->isVisible() ? more_->text() : QString(); }
signals:
    void focusRequested();
    void listRequested();
    void dismissRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QLabel *title_, *name_;
    QPushButton *more_, *open_, *dismiss_;
    QString label_;
    QColor accent_;
};
}
