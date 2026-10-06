#pragma once
#include "sessions/alerts.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace pet {
// Small popup beside the pet listing running sessions, most urgent first. Clicking
// a row brings that session's terminal or editor forward. Closes on outside click or Esc.
class SessionList : public QWidget {
    Q_OBJECT
public:
    static constexpr int maxRows = 10;
    explicit SessionList(QWidget *parent = nullptr);
    void present(const QVector<SessionRow> &rows);
    void place(const QRect &pet, const QVector<QRect> &screens);
    int rowCount() const { return buttons_.size(); }
    QString rowText(int row) const { return buttons_.value(row) ? buttons_[row]->text() : QString(); }
    void activateRow(int row) { if (buttons_.value(row)) buttons_[row]->click(); }
    static QColor stateColor(const QString &state);
signals:
    void focusRequested(const QString &session);
protected:
    void changeEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
private:
    void retranslate();
    QVBoxLayout *rows_;
    QLabel *header_, *empty_, *overflow_;
    QVector<QPushButton *> buttons_;
    QString shown_;
    bool presented_ = false;
};
}
