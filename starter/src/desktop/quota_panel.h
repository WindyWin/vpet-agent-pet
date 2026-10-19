#pragma once
#include "quota/service.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
namespace pet {
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
