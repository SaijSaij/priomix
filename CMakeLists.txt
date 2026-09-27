# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.25)
project(priomix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PRIOMIX_BUILD_DAEMON "Build priomix-daemon" ON)
option(PRIOMIX_BUILD_GUI "Build priomix-gui" ON)
option(PRIOMIX_BUILD_TESTS "Build unit tests" ON)

include(GNUInstallDirs)

set(PRIOMIX_DBUS_XML ${CMAKE_CURRENT_SOURCE_DIR}/common/io.github.saijsaij.Priomix.xml)

if(PRIOMIX_BUILD_DAEMON)
    add_subdirectory(daemon)
    add_subdirectory(systemd)
endif()

if(PRIOMIX_BUILD_GUI)
    add_subdirectory(gui)
endif()

if(PRIOMIX_BUILD_TESTS AND PRIOMIX_BUILD_DAEMON)
    enable_testing()
    add_subdirectory(tests)
endif()
