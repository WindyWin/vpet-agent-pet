# Platform services

Native dependencies behind small contracts (`contracts/`): command execution,
process inspection, desktop window activation and observation, and optional
pointer and window position queries. `linux/` implements process and command
services and assembles this build's services (`native.cpp`, declared in
`native.h`); `desktop/x11` and `desktop/kwin` are the desktop backends; and
`unsupported/` reports every operation as unavailable, for tests. Only the
`pet_native` target links X11 and D-Bus. See
[session focus](../../docs/architecture.md#session-focus), including how to add a desktop backend.
