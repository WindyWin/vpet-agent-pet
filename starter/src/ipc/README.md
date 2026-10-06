# Event orchestration

`local.cpp` reads hook input through platform services, normalizes provider events,
captures host metadata, validates v1 events and delivers them. `Receiver` parses
raw data delivered by an injected `EventTransport`; native socket and instance-lock
ownership live in `platform/linux/event_transport.cpp`.

`autostart.cpp` keeps event-kind and preference policy and the headless startup CLI.
Detached launch, desktop-session detection and XDG login entries are platform
services. See [platform boundaries](../platform/README.md) and
[protocol, policies and verification](../../docs/events.md).
