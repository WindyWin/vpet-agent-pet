# Host adapters

Where agent sessions run and how Open returns to them: the neutral `HostContext`
and its protocol v1 conversion (`context.*`), the registry of built-in hosts with
their capture, target codecs and labels (`registry.*`, `adapters/<host>.cpp`), and
the focus service with each host's tab or pane selection (`focus_service.*`,
`adapters/*_focus.cpp`; Konsole's D-Bus half is `adapters/konsole_dbus.cpp`).
Capture links only Qt Core, so `hook` stays headless. See
[session focus](../../docs/adr/0017-session-focus.md), including how to add a host.
