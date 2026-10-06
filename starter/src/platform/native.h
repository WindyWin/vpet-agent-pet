#pragma once
#include "hosts/focus_service.h"
#include <QObject>
#include <functional>
#include <memory>

namespace pet::platform {
// This build's native services, assembled once by the application. Register a new
// desktop backend or host activation in the platform's implementation of these.
std::unique_ptr<hosts::FocusService> createFocusService();
// Whether the session's screen is locked. Any watcher it needs is a child of owner,
// which must outlive every call.
std::function<bool()> createScreenLockQuery(QObject *owner);
}
