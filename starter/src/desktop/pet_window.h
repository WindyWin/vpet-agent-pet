#pragma once
#include "animation/player.h"
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWidget>

namespace pet {
class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);
    Player &player() { return player_; }
    void setPetSize(int pixels);
    void setClickThrough(bool enabled);
    bool clickThrough() const { return clickThrough_; }
    void recover();
    void setOnTop(bool enabled);
protected:
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    Player player_;
    QMenu menu_;
    QSystemTrayIcon tray_;
    QTimer recoveryTimer_;
    QAction *clickAction_ = nullptr;
    QPoint dragOffset_;
    bool dragging_ = false;
    bool clickThrough_ = false;
};
}
