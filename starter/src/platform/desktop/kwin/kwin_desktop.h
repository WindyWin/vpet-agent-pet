#pragma once
#include "i18n/contexts.h"
#include "platform/contracts/desktop.h"

namespace pet::platform::kwin {
// KDE Plasma 6's KWin scripting API, which also reaches native Wayland windows:
// https://develop.kde.org/docs/plasma/kwin/api/
// A temporary script is loaded per click and its callback reports the focus result,
// so success is Confirmed. Unsupported where KWin's scripting service is absent.
class KWinDesktop : public DesktopBackend {
public:
    static constexpr int callbackTimeoutMs = 1500;
    QString id() const override { return "kwin"; }
    QString requirement() const override { return Focus::tr("Wayland focus requires KDE Plasma 6."); }
    Outcome activate(const WindowRequest &request) override;
};
}
