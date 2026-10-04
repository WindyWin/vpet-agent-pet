#pragma once
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace pet {
struct Preferences {
    static constexpr int defaultSize = 240;
    static constexpr int recoveryMs = 15000;
    int size = defaultSize;
    QPoint position;
    bool hasPosition = false;
    bool onTop = true;
    bool muted = false; // Hides alert bubbles; the attention badge stays visible.
    bool sound = false; // Optional system beep for newly raised alerts.
    static QPoint visiblePosition(QPoint position, QSize size, const QVector<QRect> &screens);
};
class PreferencesStore {
public:
    explicit PreferencesStore(QString path = {});
    Preferences load();
    bool save(const Preferences &preferences);
    QString error() const { return error_; }
    QString path() const { return path_; }
private:
    QString path_, error_;
    bool writable_ = true;
};
}
