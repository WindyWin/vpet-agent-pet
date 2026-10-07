# Platform services

Small contracts under `contracts/` keep native handles and dependencies out of
session state, host capture, animation and hook orchestration. The full application
selects Linux, macOS or Windows implementations at build time. Code Linux and macOS
share is under `posix/`; Windows has its own under `windows/`.

| Service | Contract / composition | Linux | macOS | Windows |
| --- | --- | --- | --- | --- |
| Process inspection and GUI bootstrap | `headless.h`, `contracts/process.h` | `linux/headless.cpp`, `linux/process.cpp` (`/proc`) | `macos/headless.cpp`, `macos/process.cpp` (sysctl, libproc) | `windows/headless.cpp`, `windows/process.cpp` (Toolhelp) |
| Event delivery and instance ownership | `contracts/event_transport.h` | `posix/event_transport.cpp` | same | `windows/event_transport.cpp` (named pipe) |
| Deadline-bounded hook input / parent PID | `contracts/hook_input.h` | `posix/hook_input.cpp` | same | `windows/hook_input.cpp` |
| Host CLI runner (tmux, herdr) | `contracts/command.h` | `posix/commands.cpp` | same | none (no tmux or herdr) |
| Detached launch, pet executable | `contracts/startup.h` | `posix/launch.cpp` | same | `windows/launch.cpp` (CreateProcess) |
| Login entries | `contracts/startup.h` | `linux/startup.cpp` (XDG autostart) | `macos/login.cpp` (launchd agent) | `windows/login.cpp` (HKCU Run key) |
| Hook handler, executable and ownership | `contracts/hook_command.h` | `posix/hook_command.cpp` | same | `windows/hook_command.cpp` (exec form, cmd.exe) |
| Asset names, architecture, install paths, process checks | `contracts/update_layout.h` | `linux/update_layout.cpp` | `macos/update_layout.cpp` (no managed install) | `windows/update_layout.cpp` (no managed install) |
| Archive extraction, directory exchange and rollback | `contracts/update_install.h` | `linux/update_install.cpp` | `macos/update_install.cpp` (unsupported) | `windows/update_install.cpp` (unsupported) |
| Focus, native pointer/window operations, screen lock | `native.h`, desktop contracts | `linux/native.cpp`, `desktop/` | `macos/native.cpp` (Core Graphics; no window backend) | `windows/native.cpp` (Win32 window backend) |

`Receiver` owns an injected `EventTransport` (or the selected platform factory's
transport by default). The transport delivers bounded raw datagrams; `Receiver`
validates normalized events. The POSIX implementation retains mode-0700 endpoint
directories, mode-0600 sockets, instance locking, 8192-byte frames and 64-message
receive batches. The endpoint is under `$XDG_RUNTIME_DIR`; macOS, which has none,
uses the per-user temporary directory (`confstr(_CS_DARWIN_USER_TEMP_DIR)`, the
same for every process of the user) and raises the socket buffers above its
2 KiB default datagram limit. Windows uses a per-user message-mode named pipe instead,
one event per connection, with the same 8192-byte limit; its first instance is the
instance lock, and senders check the pipe's owner. Hook input retains its 150 ms deadline. Startup event/preference
policy remains in `ipc/autostart.cpp`; configuration merging remains in
`providers/integrations.cpp`. Handler generation and owned-handler recognition move
together so removal recognizes exactly what installation emits: POSIX shell quoting, or on
Windows Claude Code's exec form and Codex's unquoted `cmd.exe` command.

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
- `pet_platform`: headless services, `posix/` plus `linux/` or `macos/`, or `windows/`; Qt Core only.
- `pet_events`: headless orchestration composed with platform services.
- `pet_update_platform`: installation and layout; on Linux with a private libarchive
  dependency, on macOS and Windows layout only.
- `pet_updates`: shared update validation and assembly, composed with update services.
- `pet_native`: desktop implementations; on Linux Konsole activation and private
  X11/D-Bus, on macOS Core Graphics, on Windows Win32.
- `pet_ui`: Qt Widgets presentation, composed with application services.

`AGENT_PET_PORTABLE_CORE=ON` builds only the first four libraries and the portable
provider, event, alert and focus tests. Native test sections (IPC, hook commands,
process services) are gated by `PET_TEST_NATIVE` and run in the full Linux, macOS and
Windows profiles, as do the UI/prototype tests; `PET_TEST_POSIX` sections (socket
permissions, shell quoting, the command runner) and `PET_TEST_WINDOWS` ones (hook
handlers run as each agent runs them) run on one family. Startup, update and
updater-helper tests are Linux only. No artwork packing or application installation is performed
by the core profile. The project does not request X11, D-Bus, Widgets, Network or libarchive in
this profile. Qt Gui is still needed for images and playback; a system Qt build
may itself depend on D-Bus or other native libraries. The core test executables
link only Qt Core/Test and portable project libraries. CI checks both profiles on
Linux, macOS and Windows, with X11, libarchive, Widgets and Network discovery disabled for
portable core on Linux.

A future OS backend implements the contracts it needs and gets an explicit CMake
selection branch. Unsupported full-application configurations currently fail at
configure time, rather than substituting a nonfunctional transport. Keep platform
function implementations mutually exclusive; do not add a global service locator.
Packaging backends belong beside `scripts/package.py` and `packaging/`:
`scripts/package_macos.py` and `packaging/macos/` build the macOS application
bundle, and `scripts/package_windows.py` and `packaging/windows/` the Windows zip and
setup program; the Linux package is unchanged.

For a desktop backend, implement `DesktopBackend`, register it in the platform's
`createFocusService()` (`linux/native.cpp`, `macos/native.cpp` or `windows/native.cpp`) and add
sources/private dependencies to `pet_native`. Runtime capabilities select the usable backend, preserving the X11
then KWin fallback. See [session focus](../../docs/adr/0017-session-focus.md)
and [host registration](../hosts/README.md). The test-only `test-term` adapter in
`tests/focus_tests.cpp` demonstrates capture, event validation, unchanged session
reduction, selection and activation with a fake backend, without session/UI edits.
