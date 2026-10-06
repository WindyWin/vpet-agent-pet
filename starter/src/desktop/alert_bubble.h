#pragma once
#include "sessions/alerts.h"
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace pet {
// One compact, single-line toast beside the pet: "● Needs approval  abc-web  +2  Open  ×".
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
    void changeEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void retranslate(); // Labels set once; the alert itself is presented again on the next refresh.
    QLabel *title_, *name_;
    QPushButton *more_, *open_, *dismiss_;
    QString label_;
    QColor accent_;
};

// A short speech bubble for the pet's own remarks (bedtime, Monday blues): the alert bubble's look
// without buttons. It dismisses itself after `ms`, or when clicked. A remark with `details`, such as the
// daily recap, shows them on the first click instead and stays up a little longer.
class NoteBubble : public QWidget {
    Q_OBJECT
public:
    static constexpr int defaultMs = 7000;
    explicit NoteBubble(QWidget *parent = nullptr);
    void say(const QString &text, const QRect &pet, const QVector<QRect> &screens, const QString &details = {},
             int ms = defaultMs);
    QString text() const { return label_->text(); }
    bool hasDetails() const { return !details_.isEmpty(); }
signals:
    void clicked(); // A click that hides it, before it hides.
    void outdated(); // The language changed while it showed; it hid, since its words were in the old one.
protected:
    void changeEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void present(const QString &text, int ms);
    QLabel *label_;
    QTimer hide_;
    QString details_;
    QRect pet_;
    QVector<QRect> screens_;
};
}
