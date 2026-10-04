#pragma once
#include <QCache>
#include <QMap>
#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <QVector>

namespace pet {
struct Frame { QString path; int durationMs; };
struct Animation { QStringList sequences; QString mode; QString after; };

class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(QObject *parent = nullptr, const QString &resourceRoot = ":/");
    // Normal changes finish the current held state's exit; urgent changes cut immediately.
    bool select(const QString &state, bool interrupt = false);
    void beginDrag();
    void endDrag();
    void setRenderSize(int physicalPixels);
    void setPaused(bool paused);
    void advance(); // A single deterministic frame step, also used by the timer and preview.
    QString state() const { return state_; }
    QString requestedState() const { return pending_.isEmpty() ? state_ : pending_; }
    QString sequence() const { return sequence_; }
    QString phase() const;
    QString error() const { return error_; }
    QStringList states() const { return animations_.keys(); }
    const QPixmap &pixmap() const { return pixmap_; }
    int frameIndex() const { return index_; }
    int frameCount() const { return sequences_.value(sequence_).size(); }
    int frameDuration() const { return duration_; }
    int cacheKiB() const { return cache_.totalCost(); }
    static constexpr int cacheLimitKiB = 8192;
    bool paused() const { return paused_; }
    bool stopped() const { return stopped_; }
    bool isDragging() const { return dragActive_; }
    bool valid() const { return !animations_.isEmpty(); }
    void clearError() { error_.clear(); }
signals:
    void changed();
    void failed(const QString &message);
    void completed(const QString &state);
private:
    bool load(const QString &resourceRoot);
    void enter(const QString &state);
    void enterSequence(int phase);
    void display();
    void fail(const QString &message);
    QString resumeTarget() const;
    QMap<QString, QVector<Frame>> sequences_;
    QMap<QString, Animation> animations_;
    QCache<QString, QPixmap> cache_{cacheLimitKiB};
    QPixmap pixmap_;
    QTimer timer_;
    QString state_, sequence_, pending_, previous_ = "idle", dragResume_ = "idle", error_;
    int index_ = 0, phase_ = 0, duration_ = 0, renderSize_ = 240;
    bool paused_ = false, stopped_ = false, dragActive_ = false;
};
}
