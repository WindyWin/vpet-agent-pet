# Platform services

Small contracts under `contracts/` keep native handles and dependencies out of
session state, host capture, animation and hook orchestration. The full application
currently selects Linux implementations at build time; this is not a new OS port.

| Service | Contract / composition | Linux implementation |
| --- | --- | --- |
| Process inspection and GUI bootstrap | `headless.h`, `contracts/process.h` | `linux/headless.cpp`, `linux/process.cpp` |
| Event delivery and instance ownership | `contracts/event_transport.h` | `linux/event_transport.cpp` |
| Deadline-bounded hook input / parent PID | `contracts/hook_input.h` | `linux/hook_input.cpp` |
| Detached launch and login entries | `contracts/startup.h` | `linux/startup.cpp` |
| Hook command formatting and ownership | `contracts/hook_command.h` | `linux/hook_command.cpp` |
| Asset names, architecture, install paths, process checks | `contracts/update_layout.h` | `linux/update_layout.cpp` |
| Archive extraction, directory exchange and rollback | `contracts/update_install.h` | `linux/update_install.cpp` |
| Focus, native pointer/window operations, screen lock | `native.h`, desktop contracts | `linux/native.cpp`, `desktop/` |

`Receiver` owns an injected `EventTransport` (or the selected platform factory's
transport by default). The transport delivers bounded raw datagrams; `Receiver`
validates normalized events. The Linux implementation retains mode-0700 endpoint
directories, mode-0600 sockets, instance locking, 8192-byte frames and 64-message
receive batches. Hook input retains its 150 ms deadline. Startup event/preference
policy remains in `ipc/autostart.cpp`; configuration merging remains in
`providers/integrations.cpp`. POSIX quoting and owned-command recognition move
together so removal recognizes exactly the command grammar that installation emits.

Other headless and update seams use build-selected functions, declared without
native types. Qt file, settings, process and network APIs remain direct where
portable. Update hashes, release/manifest validation and download orchestration
remain under `updates/`; the platform owns tar extraction and Linux installation
mechanics. Release selection, manifest validation, cache cleanup and relaunch use
the same layout/naming contract. The installed layout and package formats do not
change.

## Build registration

`CMakeLists.txt` is the platform build selection point:

- `pet_host_capture`: capture, v1 codecs and registry; Qt Core only.
- `pet_hosts`: focus coordination, portable activation adapters and matching;
  depends on capture and contracts, not native implementations.
- `pet_core`: state, alerts, preferences and provider normalization; Qt Core and capture.
- `pet_animation`: portable playback logic; Qt Gui, no Widgets or native backend.
- `pet_platform_linux`: headless Linux services; Qt Core only.
- `pet_events`: headless orchestration composed with Linux services.
- `pet_update_platform`: Linux installation and layout; private libarchive dependency.
- `pet_updates`: shared update validation and assembly, composed with update services.
- `pet_native`: desktop implementations and Konsole activation; private X11/D-Bus.
- `pet_ui`: Qt Widgets presentation, composed with application services.

`AGENT_PET_PORTABLE_CORE=ON` builds only the first four libraries and the portable
provider, event, alert and focus tests. Native test sections are explicitly gated
by `PET_TEST_LINUX`; IPC, startup, updater helper and UI/prototype tests remain in
the full Linux profile. No artwork packing or application installation is performed
by the core profile. The project does not request X11, D-Bus, Widgets, Network or libarchive in
this profile. Qt Gui is still needed for images and playback; a system Qt build
may itself depend on D-Bus or other native libraries. The core test executables
link only Qt Core/Test and portable project libraries. CI checks both profiles,
with X11, libarchive, Widgets and Network discovery disabled for portable core.

A future OS backend implements the contracts it needs and gets an explicit CMake
selection branch. Unsupported full-application configurations currently fail at
configure time, rather than substituting a nonfunctional transport. Keep platform
function implementations mutually exclusive; do not add a global service locator.
Packaging backends belong beside `scripts/package.py` and `packaging/`; Linux
packaging stays unchanged until another OS implementation is tested end to end.

For a desktop backend, implement `DesktopBackend`, register it in
`linux/native.cpp`'s `createFocusService()` and add sources/private dependencies to
`pet_native`. Runtime capabilities select the usable backend, preserving the X11
then KWin fallback. See [session focus](../../docs/architecture.md#session-focus)
and [host registration](../hosts/README.md). The test-only `test-term` adapter in
`tests/focus_tests.cpp` demonstrates capture, event validation, unchanged session
reduction, selection and activation with a fake backend, without session/UI edits.
