#include "desktop/overlay.h"
#include "platform/contracts/native_window.h"
#include <QGuiApplication>
#include <climits>
#include <QScreen>
#include <QWidget>
#include <QWindow>

namespace pet::overlay {
static QScreen *screenFor(const QRect &window, QScreen *current) {
    if (auto *at = QGuiApplication::screenAt(window.center())) return at;
    // Past every screen (dragged off, or hiding behind an edge): the nearest keeps it.
    QScreen *nearest = current ? current : QGuiApplication::primaryScreen();
    int best = INT_MAX;
    for (auto *screen : QGuiApplication::screens()) {
        const auto g = screen->geometry();
        const int dx = qMax(0, qMax(g.left() - window.center().x(), window.center().x() - g.right()));
        const int dy = qMax(0, qMax(g.top() - window.center().y(), window.center().y() - g.bottom()));
        if (dx + dy < best) { best = dx + dy; nearest = screen; }
    }
    return nearest;
}
void prepare(QWidget *window, bool aboveWindows, bool takesKeyboard) {
    if (!platform::layerShellActive()) return;
    window->winId(); // Creates the QWindow the layer properties belong to; nothing is shown.
    // Some windows come out of an early create() without the alpha channel they asked for, and would be
    // drawn on black; creating the surface again honors the request.
    auto *handle = window->windowHandle();
    if (window->testAttribute(Qt::WA_TranslucentBackground) && handle->format().alphaBufferSize() == 0) {
        handle->destroy();
        handle->create();
    }
    platform::layerAttach(handle, aboveWindows, takesKeyboard);
}
void place(QWidget *window, QPoint global, bool rehome) {
    if (!platform::layerShellActive()) { window->move(global); return; }
    if (!window->windowHandle()) window->winId();
    auto *handle = window->windowHandle();
    if (!handle) { window->move(global); return; }
    QScreen *current = handle->screen();
    QScreen *target = screenFor(QRect(global, window->size()), current);
    if (target && target != current && (rehome || !window->isVisible())) {
        platform::layerSetScreen(handle, target);
        current = target;
    }
    // After the screen: Qt keeps a window's place relative to its screen, so a screen change would shift it.
    window->move(global);
    if (current) platform::layerPlace(handle, global - current->geometry().topLeft());
}
}
