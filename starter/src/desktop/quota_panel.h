#pragma once
#include "quota/service.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
namespace pet {
// A donut showing how much of a limit is used; a negative value draws an empty dotted ring.
class RingGauge : public QWidget {
    Q_OBJECT
public:
    explicit RingGauge(QWidget *parent = nullptr);
    void set(double used, const QString &text, const QString &caption);
    double used() const { return used_; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    double used_ = -1;
    QString text_, caption_;
};
// Footer of the session popup; polling belongs to its visibility, not the pet's lifetime.
class QuotaPanel : public QWidget {
    Q_OBJECT
public:
    explicit QuotaPanel(QWidget *parent = nullptr);
    void setService(quota::Service *service);
signals:
    void sizeChanged();
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void changeEvent(QEvent *) override;
private:
    void render();
    QPointer<quota::Service> service_;
    QTimer timer_;
    QLabel *title_, *hint_;
    QPushButton *refresh_;
    QVBoxLayout *readings_;
};
}
