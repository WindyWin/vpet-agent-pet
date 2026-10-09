cmake_minimum_required(VERSION 3.22)
project(AgentPet VERSION 0.15.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(AGENT_PET_PORTABLE_CORE "Build portable libraries and tests only (no application or native services)" OFF)
if(NOT AGENT_PET_PORTABLE_CORE AND NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$")
    message(FATAL_ERROR "The full Agent Pet application supports Linux, macOS and Windows. Use -DAGENT_PET_PORTABLE_CORE=ON for core libraries/tests.")
endif()
if(WIN32)
    # Wide Win32 APIs, without windows.h's min/max macros; sources are UTF-8.
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)
if(NOT AGENT_PET_PORTABLE_CORE)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network LinguistTools)
    if(NOT APPLE AND NOT WIN32)
        find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
        find_package(LibArchive REQUIRED)
        find_package(X11 REQUIRED)
    endif()
endif()
qt_standard_project_setup()
include(GNUInstallDirs)

# Release builds pass -DAGENT_PET_REVISION=<git commit>; local builds say "local".
set(AGENT_PET_REVISION "local" CACHE STRING "Source revision shown in About")
configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)
# The cue vocabulary (docs/pets.md), embedded so the catalog needs no resource file to read it.
file(READ src/animation/cues.json PET_CUES)
configure_file(src/animation/cues.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/cues.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/animation/cues.json)

# Capture/codecs link independently of focus coordination and native activation.
qt_add_library(pet_host_capture STATIC src/hosts/context.cpp src/hosts/registry.cpp
    src/hosts/adapters/konsole.cpp src/hosts/adapters/tmux.cpp src/hosts/adapters/herdr.cpp src/hosts/adapters/generic.cpp)
target_include_directories(pet_host_capture PUBLIC src)
target_link_libraries(pet_host_capture PUBLIC Qt6::Core)
qt_add_library(pet_hosts STATIC src/hosts/focus_service.cpp
    src/hosts/adapters/tmux_focus.cpp src/hosts/adapters/herdr_focus.cpp src/platform/desktop/window_match.cpp)
target_include_directories(pet_hosts PUBLIC src)
target_link_libraries(pet_hosts PUBLIC pet_host_capture)
qt_add_library(pet_core STATIC
    src/sessions/state.cpp src/sessions/alerts.cpp src/sessions/presence.cpp src/sessions/recap.cpp
    src/settings/preferences.cpp src/providers/adapters.cpp)
target_include_directories(pet_core PUBLIC src)
target_link_libraries(pet_core PUBLIC Qt6::Core pet_host_capture)
# Which of the pet's competing behaviors may show (docs/adr/0031-behavior-runtime.md); no timers, no Player.
qt_add_library(pet_behavior STATIC src/behavior/runtime.cpp)
target_include_directories(pet_behavior PUBLIC src)
target_link_libraries(pet_behavior PUBLIC Qt6::Core)
qt_add_library(pet_animation STATIC
    src/animation/catalog.cpp src/animation/pet_library.cpp src/animation/player.cpp src/animation/ambient.cpp src/animation/activity.cpp
    src/animation/mood.cpp src/animation/easter_eggs.cpp src/animation/stage.cpp src/animation/plugins.cpp src/animation/event_rules.cpp)
target_include_directories(pet_animation PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pet_animation PUBLIC Qt6::Gui pet_behavior)

if(NOT AGENT_PET_PORTABLE_CORE)
# Every pet folder under assets/ (docs/pets.md): one stable pack per frame folder, so additions never repack
# unrelated artwork, and the small index (artwork.rcc) of every pet's metadata, catalog, preview and packs.
include(cmake/pets.cmake)
# Pets that ship with the app; any other pet under assets/ downloads on demand from the `pets` release.
set(AGENT_PET_BUNDLED_PETS vpet CACHE STRING "Pets whose artwork packs are installed (the rest download on demand)")
add_pets(pet_artwork SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets" OUTPUT "${CMAKE_CURRENT_BINARY_DIR}" INSTALL
    BUNDLED ${AGENT_PET_BUNDLED_PETS})
file(GLOB pet_notices CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "licenses/*")
list(APPEND pet_notices THIRD_PARTY_NOTICES.md LICENSE NOTICE)
# Translations. English is the source language; each .ts compiles to a .qm embedded at :/i18n/ in every
# binary that links pet_i18n. `cmake --build build --target update_translations` refreshes the .ts files
# from all sources (both platforms', so the result does not depend on where it runs).
qt_add_library(pet_i18n STATIC src/i18n/language.cpp)
target_include_directories(pet_i18n PUBLIC src)
target_link_libraries(pet_i18n PUBLIC Qt6::Core)
set(pet_ts_files translations/agent-pet_vi.ts)
file(GLOB_RECURSE pet_translated_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/*.cpp" "src/*.h")
list(SORT pet_translated_sources)
qt_add_lupdate(pet_i18n TS_FILES ${pet_ts_files} SOURCES ${pet_translated_sources} OPTIONS -locations none -no-ui-lines -no-obsolete)
qt_add_lrelease(pet_i18n TS_FILES ${pet_ts_files} QM_FILES_OUTPUT_VARIABLE pet_qm_files)
qt_add_resources(pet_i18n pet_translations PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${pet_qm_files})
qt_add_library(pet_ui STATIC
    src/desktop/pet_window.cpp
    src/desktop/pet_picker.cpp src/desktop/pet_downloader.cpp src/desktop/plugin_list.cpp
    src/desktop/touch.cpp src/desktop/wander.cpp src/desktop/wellness.cpp src/desktop/alert_bubble.cpp src/desktop/monitor.cpp
    src/desktop/session_list.cpp src/updates/controller.cpp)
target_include_directories(pet_ui PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pet_ui PUBLIC Qt6::Widgets pet_animation pet_events pet_updates pet_i18n Qt6::Network PRIVATE pet_native)
qt_add_resources(pet_ui pet_assets PREFIX "/" FILES ${pet_notices})
add_dependencies(pet_ui pet_artwork)
# Headless platform services shared by the hook and the pet: POSIX event transport, hook input,
# detached launch and the QProcess command runner, plus process inspection (/proc on Linux,
# sysctl on macOS) and login registration (XDG autostart on Linux, launchd on macOS). Windows
# has its own: a named-pipe transport, Toolhelp processes, CreateProcess and the Run key.
if(WIN32)
    set(pet_platform_sources src/platform/windows/event_transport.cpp src/platform/windows/hook_input.cpp
        src/platform/windows/hook_command.cpp src/platform/windows/launch.cpp src/platform/windows/login.cpp
        src/platform/windows/process.cpp src/platform/windows/headless.cpp src/platform/windows/session_identity.cpp)
elseif(APPLE)
    set(pet_platform_sources src/platform/posix/commands.cpp src/platform/posix/event_transport.cpp
        src/platform/posix/hook_input.cpp src/platform/posix/hook_command.cpp src/platform/posix/launch.cpp
        src/platform/macos/process.cpp src/platform/macos/headless.cpp src/platform/macos/login.cpp src/platform/macos/session_identity.cpp)
else()
    set(pet_platform_sources src/platform/posix/commands.cpp src/platform/posix/event_transport.cpp
        src/platform/posix/hook_input.cpp src/platform/posix/hook_command.cpp src/platform/posix/launch.cpp
        src/platform/linux/process.cpp src/platform/linux/headless.cpp src/platform/linux/startup.cpp src/platform/linux/session_identity.cpp)
endif()
qt_add_library(pet_platform STATIC ${pet_platform_sources})
target_include_directories(pet_platform PUBLIC src)
target_link_libraries(pet_platform PUBLIC Qt6::Core)
if(WIN32)
    target_link_libraries(pet_platform PRIVATE user32) # The hook reads the agent's console window.
endif()
# Native desktop services and their composition (platform/native.h). On Linux: X11 and KWin backends,
# Konsole's D-Bus tab selection, pointer queries and the screen-lock watcher; only this target links
# X11 and D-Bus. On macOS: Core Graphics pointer and screen-lock queries, no window backend yet.
# On Windows: a Win32 window backend, pointer and screen-lock queries.
if(WIN32)
    qt_add_library(pet_native STATIC src/platform/windows/native.cpp)
    target_link_libraries(pet_native PUBLIC pet_hosts PRIVATE pet_platform Qt6::Gui user32)
elseif(APPLE)
    qt_add_library(pet_native STATIC src/platform/macos/native.cpp)
    target_link_libraries(pet_native PUBLIC pet_hosts PRIVATE pet_platform Qt6::Gui "-framework CoreGraphics" "-framework CoreFoundation")
else()
    qt_add_library(pet_native STATIC src/platform/linux/native.cpp
        src/platform/desktop/x11/connection.cpp src/platform/desktop/x11/pointer.cpp src/platform/desktop/x11/x11_desktop.cpp
        src/platform/desktop/kwin/kwin_desktop.cpp src/platform/desktop/screensaver/screen_lock.cpp
        src/hosts/adapters/konsole_dbus.cpp)
    target_link_libraries(pet_native PUBLIC pet_hosts PRIVATE pet_platform Qt6::Gui Qt6::DBus X11::X11)
endif()
target_include_directories(pet_native PUBLIC src)
# Headless code shared by the pet and its display-free commands (hook, integration, autostart).
qt_add_library(pet_events STATIC src/ipc/local.cpp src/ipc/session_store.cpp src/ipc/autostart.cpp src/providers/integrations.cpp)
target_include_directories(pet_events PUBLIC src)
target_link_libraries(pet_events PUBLIC pet_core PRIVATE pet_platform)
if(WIN32)
    # Verified setup programs install Windows upgrades.
    qt_add_library(pet_update_platform STATIC src/platform/windows/update_install.cpp src/platform/windows/update_layout.cpp)
    target_link_libraries(pet_update_platform PUBLIC Qt6::Core)
elseif(APPLE)
    # Releases are announced and downloaded manually; nothing is extracted or installed in place.
    qt_add_library(pet_update_platform STATIC src/platform/macos/update_install.cpp src/platform/macos/update_layout.cpp)
    target_link_libraries(pet_update_platform PUBLIC Qt6::Core)
else()
    qt_add_library(pet_update_platform STATIC src/platform/linux/update_install.cpp src/platform/linux/update_layout.cpp)
    target_link_libraries(pet_update_platform PUBLIC Qt6::Core PRIVATE LibArchive::LibArchive)
endif()
target_include_directories(pet_update_platform PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
qt_add_library(pet_updates STATIC src/updates/release.cpp src/updates/components.cpp)
target_include_directories(pet_updates PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pet_updates PUBLIC Qt6::Core PRIVATE pet_update_platform)
qt_add_executable(agent-pet src/main.cpp)
target_link_libraries(agent-pet PRIVATE pet_ui pet_events pet_native pet_i18n)
set(pet_cli_target agent-pet) # The executable tests run as hooks and commands.
if(WIN32)
    # agent-pet.exe opens no console window; agent-pet-cli.exe, the same program as a console
    # application, runs hooks and commands with working standard streams.
    set(AGENT_PET_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/agent-pet.ico")
    configure_file(packaging/windows/agent-pet.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/agent-pet.rc" @ONLY)
    target_sources(agent-pet PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/agent-pet.rc")
    set_target_properties(agent-pet PROPERTIES WIN32_EXECUTABLE ON)
    qt_add_executable(agent-pet-cli src/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/agent-pet.rc")
    target_link_libraries(agent-pet-cli PRIVATE pet_ui pet_events pet_native pet_i18n)
    set(pet_cli_target agent-pet-cli)
    qt_add_executable(agent-pet-updater src/platform/windows/update_helper.cpp)
    target_link_libraries(agent-pet-updater PRIVATE pet_updates pet_events pet_i18n)
    set_target_properties(agent-pet-updater PROPERTIES WIN32_EXECUTABLE ON)
    # scripts/package_windows.py assembles the installation directory and the setup program.
elseif(APPLE)
    # A plain executable, so local builds find artwork.rcc beside it; scripts/package_macos.py assembles the
    # application bundle from it.
    set_target_properties(agent-pet PROPERTIES MACOSX_BUNDLE OFF)
else()
    qt_add_executable(agent-pet-updater src/updates/helper.cpp)
    target_link_libraries(agent-pet-updater PRIVATE pet_updates pet_events pet_i18n)
    set_target_properties(agent-pet-updater PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS agent-pet-updater RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set_target_properties(agent-pet PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS agent-pet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES THIRD_PARTY_NOTICES.md LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet)
    install(FILES packaging/agent-pet.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES packaging/agent-pet.desktop.in DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet)
    install(FILES packaging/tui.sh packaging/dialogrc DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet) # Prompts for install.sh and uninstall.sh
    install(PROGRAMS packaging/install.sh packaging/uninstall.sh DESTINATION .)
    install(FILES packaging/INSTALL.txt DESTINATION .)
    install(DIRECTORY licenses DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet)
    # scripts/package.py copies only the required plugins and their library closure.
endif() # Platform installation

endif() # Application

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(provider-tests tests/provider_tests.cpp)
    target_link_libraries(provider-tests PRIVATE pet_core Qt6::Test)
    target_compile_definitions(provider-tests PRIVATE PROVIDER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/providers.json")
    add_test(NAME providers COMMAND provider-tests)
    qt_add_executable(event-tests tests/event_tests.cpp)
    target_link_libraries(event-tests PRIVATE pet_core Qt6::Test)
    target_compile_definitions(event-tests PRIVATE FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/concurrent.json")
    add_test(NAME events COMMAND event-tests)
    qt_add_executable(alert-tests tests/alert_tests.cpp)
    target_link_libraries(alert-tests PRIVATE pet_core pet_hosts Qt6::Test)
    add_test(NAME alerts COMMAND alert-tests)
    # Focus contracts with fake adapters and backends; needs no desktop, D-Bus or terminal.
    qt_add_executable(focus-tests tests/focus_tests.cpp)
    target_link_libraries(focus-tests PRIVATE pet_hosts pet_core Qt6::Test)
    add_test(NAME focus COMMAND focus-tests)
    # The behavior runtime's policy with a hand-set clock and a fake presentation.
    qt_add_executable(behavior-tests tests/behavior_tests.cpp)
    target_link_libraries(behavior-tests PRIVATE pet_behavior Qt6::Test)
    add_test(NAME behavior COMMAND behavior-tests)
    if(NOT AGENT_PET_PORTABLE_CORE)
        if(WIN32)
            qt_add_executable(session-process-fixture tests/session_process_fixture.cpp)
            target_link_libraries(session-process-fixture PRIVATE Qt6::Core)
            target_compile_definitions(event-tests PRIVATE SESSION_PROCESS_PATH=\"$<TARGET_FILE:session-process-fixture>\")
            add_dependencies(event-tests session-process-fixture)
        endif()
        # PET_TEST_NATIVE sections run with every platform's services; PET_TEST_POSIX and PET_TEST_WINDOWS ones with one family.
        if(WIN32)
            set(pet_test_platform PET_TEST_WINDOWS)
        else()
            set(pet_test_platform PET_TEST_POSIX)
        endif()
        foreach(target provider-tests event-tests alert-tests focus-tests)
            target_link_libraries(${target} PRIVATE pet_events pet_platform)
            target_compile_definitions(${target} PRIVATE PET_TEST_NATIVE ${pet_test_platform} APP_PATH="$<TARGET_FILE:${pet_cli_target}>")
        endforeach()
        add_dependencies(provider-tests ${pet_cli_target})
        add_dependencies(event-tests ${pet_cli_target})
        qt_add_executable(prototype-tests tests/prototype_tests.cpp)
        target_link_libraries(prototype-tests PRIVATE pet_ui Qt6::Test)
        # The slowest suite runs as shards, so ctest -j spreads it over the cores (prototype-1 … prototype-N).
        set(AGENT_PET_PROTOTYPE_SHARDS 4 CACHE STRING "Parallel CTest shards for prototype-tests")
        foreach(shard RANGE 1 ${AGENT_PET_PROTOTYPE_SHARDS})
            add_test(NAME prototype-${shard} COMMAND prototype-tests --shard ${shard}/${AGENT_PET_PROTOTYPE_SHARDS})
            set_tests_properties(prototype-${shard} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endforeach()
        qt_add_executable(i18n-tests tests/i18n_tests.cpp)
        target_link_libraries(i18n-tests PRIVATE pet_i18n Qt6::Test)
        target_compile_definitions(i18n-tests PRIVATE TS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/translations/agent-pet_vi.ts")
        add_test(NAME i18n COMMAND i18n-tests)
        # Pet packs: the catalog and its core state contract, the pet library over fixture pets, choosing a pet.
        add_pets(pet_test_artwork SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pets" OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test-pets")
        qt_add_executable(pets-tests tests/pets_tests.cpp)
        target_link_libraries(pets-tests PRIVATE pet_ui Qt6::Test)
        target_compile_definitions(pets-tests PRIVATE PET_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}"
            PET_INDEX="${CMAKE_CURRENT_BINARY_DIR}/artwork.rcc" FIXTURE_INDEX="${CMAKE_CURRENT_BINARY_DIR}/test-pets/artwork.rcc"
            RCC="$<TARGET_FILE:Qt6::rcc>")
        add_dependencies(pets-tests pet_test_artwork)
        add_test(NAME pets COMMAND pets-tests)
        set_tests_properties(pets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        # Plugin packs: scanning, merging fragments into a catalog, the library, preferences and Settings' list.
        qt_add_executable(plugin-tests tests/plugin_tests.cpp)
        target_link_libraries(plugin-tests PRIVATE pet_ui Qt6::Test)
        target_compile_definitions(plugin-tests PRIVATE PET_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}"
            FIXTURE_INDEX="${CMAKE_CURRENT_BINARY_DIR}/test-pets/artwork.rcc")
        add_dependencies(plugin-tests pet_test_artwork)
        add_test(NAME plugins COMMAND plugin-tests)
        set_tests_properties(plugins PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME pet-scaffold COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_new_pet.py)
    endif()
    if(NOT AGENT_PET_PORTABLE_CORE AND NOT APPLE AND NOT WIN32)
        qt_add_executable(update-tests tests/update_tests.cpp)
        target_link_libraries(update-tests PRIVATE pet_ui Qt6::Test)
        add_test(NAME updates COMMAND update-tests)
        set_tests_properties(updates PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME update-install COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_update.py $<TARGET_FILE:agent-pet-updater>)
        qt_add_executable(startup-tests tests/startup_tests.cpp)
        target_link_libraries(startup-tests PRIVATE pet_events Qt6::Test)
        target_compile_definitions(startup-tests PRIVATE APP_PATH="$<TARGET_FILE:agent-pet>")
        add_dependencies(startup-tests agent-pet)
        add_test(NAME startup COMMAND startup-tests)
        # Explicitly invoked desktop test: it moves the real pointer on X11/XWayland.
        if(TARGET X11::Xtst)
            qt_add_executable(desktop-tests tests/desktop_tests.cpp)
            target_link_libraries(desktop-tests PRIVATE pet_ui pet_native Qt6::Test X11::X11 X11::Xtst)
        endif()
    endif() # Linux tests
    if(NOT AGENT_PET_PORTABLE_CORE AND WIN32)
        qt_add_executable(windows-update-tests tests/windows_update_tests.cpp)
        target_link_libraries(windows-update-tests PRIVATE pet_updates Qt6::Test)
        add_test(NAME windows-updates COMMAND windows-update-tests -o -,txt)
    endif()
endif()
