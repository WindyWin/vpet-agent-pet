#pragma once
#include <QPoint>
#include <QRect>

class QWidget;

namespace pet {
// Places a top-level window of the pet (the pet itself, its bubbles) at global coordinates.
// Normally that is QWidget::move. Under native Wayland the compositor places windows, so the window is a
// layer-shell surface moved by margins (platform/contracts/native_window.h); Qt's own idea of its position
// is kept in step, which popups and mouse coordinates depend on.
namespace overlay {
// Before the window is first shown and after a window-flag change recreated it. No-op off layer-shell.
void prepare(QWidget *window, bool aboveWindows, bool takesKeyboard);
// `rehome`: may the surface change screens now (it is recreated, which ends a mouse grab).
void place(QWidget *window, QPoint global, bool rehome = true);
}
}
