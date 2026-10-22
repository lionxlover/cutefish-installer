# SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)

project(installer
    VERSION 1.0
    DESCRIPTION "The system installer of Cutefish OS"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

option(BUILD_GUI "Build the window on cutefish-toolkit" ON)

# ---------------------------------------------------------------------------
# The installer core: the disk scan, the plan and its rules, the layouts the
# planner lays out, and the engine that walks the whole install. It leans on
# the C++ library and POSIX alone, so the command-line tool of this tool
# builds and runs on any distribution, the Cutefish desktop or not.
add_library(installer-core STATIC
    src/core/disks.cpp
    src/core/installengine.cpp
    src/core/installplan.cpp
    src/core/json.cpp
    src/core/partitionplan.cpp
    src/core/tables.cpp
    src/core/util.cpp
)
target_include_directories(installer-core PUBLIC ${PROJECT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# cutefish-installerctl: the tool behind the window, and the window's own way
# of running an install: it prints the progress as JSON events, and a shell
# or a script can run the same install from a plan's JSON.
add_executable(cutefish-installerctl src/cli/main.cpp)
target_link_libraries(cutefish-installerctl PRIVATE installer-core)

install(TARGETS cutefish-installerctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.cutefish.installer.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)

# ---------------------------------------------------------------------------
# The window, built on the Cutefish desktop's own UI toolkit. On a machine
# without the desktop, or with BUILD_GUI off, the command-line tool and its
# engine still build and pass their tests.
if(BUILD_GUI)
    find_package(CutefishToolkit QUIET)
    if(NOT CutefishToolkit_FOUND)
        message(STATUS "cutefish-toolkit was not found: building cutefish-installerctl only. "
                       "Build https://github.com/cutefishos/cutefish-toolkit first for the window.")
    else()
        add_executable(cutefish-installer
            src/main.cpp
            src/app/installerwindow.cpp
            src/app/pagesetup.cpp
            src/app/pagedisk.cpp
            src/app/pageuser.cpp
            src/app/pagerun.cpp
            src/app/jobrunner.cpp
            src/app/widgets.cpp
        )
        target_link_libraries(cutefish-installer PRIVATE installer-core Cutefish::Toolkit)

        install(TARGETS cutefish-installer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

        # The translated desktop entry, and the icon beside the theme's own:
        # system-software-install of cutefishos/icons, installed where every
        # desktop finds it whatever its theme is.
        cutefish_add_translations(cutefish-installer SOURCES src DESKTOP_FILES data/cutefish-installer.desktop)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-installer.desktop
                DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES data/icons/cutefish-installer.svg
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    endif()
endif()

# ---------------------------------------------------------------------------
# The engine's own tests, run with ctest. The command-line tool itself is
# run the way the window runs it, its events read back as they are.
if(BUILD_TESTING)
    add_executable(cutefish-installer-tests tests/test_core.cpp)
    target_link_libraries(cutefish-installer-tests PRIVATE installer-core)
    add_test(NAME core
             COMMAND cutefish-installer-tests $<TARGET_FILE:cutefish-installerctl>)
endif()
