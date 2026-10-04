#pragma once
#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <QVector>

namespace pet {
struct Frame { QString path; int durationMs; };
// M1 loops idle or the held thinking sequence. A/B/C transitions belong to M2.
class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr);
    void select(const QString &state);
    QString state() const { return state_; }
    const QPixmap &pixmap() const { return pixmap_; }
    int frameIndex() const { return index_; }
    int frameDuration() const { return timer_.interval(); }
signals:
    void changed();
private:
    void display();
    QVector<Frame> frames_;
    QString state_;
    QPixmap pixmap_;
    QTimer timer_;
    int index_ = 0;
};
}
