cmake_minimum_required(VERSION 3.22)
project(AgentPet VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(X11 REQUIRED)
qt_standard_project_setup()
include(GNUInstallDirs)

# Embed the selected pack: development and installed binaries use identical paths.
file(GLOB_RECURSE pet_resources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "assets/vpet/vup/*.png" "licenses/*")
list(APPEND pet_resources assets/vpet/animations.json THIRD_PARTY_NOTICES.md)
qt_add_library(pet_ui STATIC
    src/animation/player.cpp src/desktop/pet_window.cpp
    src/desktop/drag_monitor.cpp src/settings/preferences.cpp)
target_include_directories(pet_ui PUBLIC src)
target_link_libraries(pet_ui PUBLIC Qt6::Widgets PRIVATE X11::X11)
qt_add_resources(pet_ui pet_assets PREFIX "/" FILES ${pet_resources})
qt_add_executable(agent-pet src/main.cpp)
target_link_libraries(agent-pet PRIVATE pet_ui)
set_target_properties(agent-pet PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS agent-pet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet)
install(DIRECTORY licenses DESTINATION ${CMAKE_INSTALL_DATADIR}/agent-pet)
# scripts/package.py copies only the required plugins and their library closure.

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(prototype-tests tests/prototype_tests.cpp)
    target_link_libraries(prototype-tests PRIVATE pet_ui Qt6::Test)
    add_test(NAME prototype COMMAND prototype-tests)
    set_tests_properties(prototype PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Explicitly invoked desktop test: it moves the real pointer on X11/XWayland.
    if(TARGET X11::Xtst)
        qt_add_executable(desktop-tests tests/desktop_tests.cpp)
        target_link_libraries(desktop-tests PRIVATE pet_ui Qt6::Test X11::X11 X11::Xtst)
    endif()
endif()
