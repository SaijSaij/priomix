# SPDX-License-Identifier: GPL-3.0-only
find_package(PkgConfig REQUIRED)
find_package(sdbus-c++ 2.0 REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=1.4)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
# ---- HeadsetControl --------------------------------------------------------------------
# Upstream only ships a static library without pkg-config/CMake files. The Arch package
# (headsetcontrol 4.0.0-1) contains "slim" LTO objects whose LTO sections were stripped,
# i.e. an archive without any code. AUTO therefore checks whether the system library
# actually links and otherwise builds the library from source (same release).
set(PRIOMIX_HEADSETCONTROL "AUTO" CACHE STRING "HeadsetControl library: AUTO, SYSTEM or BUNDLED")
set_property(CACHE PRIOMIX_HEADSETCONTROL PROPERTY STRINGS AUTO SYSTEM BUNDLED)
set(PRIOMIX_HEADSETCONTROL_GIT_TAG "4.0.0" CACHE STRING "HeadsetControl release to bundle")

pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

set(_hsc_mode ${PRIOMIX_HEADSETCONTROL})
if(NOT _hsc_mode STREQUAL "BUNDLED")
    find_library(HEADSETCONTROL_LIBRARY NAMES headsetcontrol)
    find_path(HEADSETCONTROL_INCLUDE_DIR NAMES headsetcontrol.hpp PATH_SUFFIXES headsetcontrol)
    if(HEADSETCONTROL_LIBRARY AND HEADSETCONTROL_INCLUDE_DIR)
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_INCLUDES ${HEADSETCONTROL_INCLUDE_DIR})
        set(CMAKE_REQUIRED_LIBRARIES ${HEADSETCONTROL_LIBRARY} ${HIDAPI_LINK_LIBRARIES})
        check_cxx_source_compiles("
            #include <headsetcontrol.hpp>
            int main() { return static_cast<int>(headsetcontrol::version().size()); }"
            PRIOMIX_SYSTEM_HEADSETCONTROL_LINKS)
        unset(CMAKE_REQUIRED_INCLUDES)
        unset(CMAKE_REQUIRED_LIBRARIES)
    endif()
    if(PRIOMIX_SYSTEM_HEADSETCONTROL_LINKS)
        set(_hsc_mode "SYSTEM")
    elseif(_hsc_mode STREQUAL "SYSTEM")
        message(FATAL_ERROR "System libheadsetcontrol is missing or does not link")
    else()
        set(_hsc_mode "BUNDLED")
    endif()
endif()

if(_hsc_mode STREQUAL "SYSTEM")
    message(STATUS "HeadsetControl: using system library ${HEADSETCONTROL_LIBRARY}")
    add_library(priomix-headsetcontrol INTERFACE)
    target_include_directories(priomix-headsetcontrol INTERFACE ${HEADSETCONTROL_INCLUDE_DIR})
    target_link_libraries(priomix-headsetcontrol INTERFACE ${HEADSETCONTROL_LIBRARY}
                                                           PkgConfig::HIDAPI)
else()
    # Offline builds (e.g. makepkg) can point FETCHCONTENT_SOURCE_DIR_HEADSETCONTROL to a
    # local checkout.
    message(STATUS "HeadsetControl: building bundled ${PRIOMIX_HEADSETCONTROL_GIT_TAG}")
    include(FetchContent)
    FetchContent_Declare(headsetcontrol
        GIT_REPOSITORY https://github.com/Sapd/HeadsetControl.git
        GIT_TAG ${PRIOMIX_HEADSETCONTROL_GIT_TAG}
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(headsetcontrol)
    add_library(priomix-headsetcontrol INTERFACE)
    target_link_libraries(priomix-headsetcontrol INTERFACE headsetcontrol_lib)
endif()

# D-Bus adaptor generated from the shared interface definition.
find_program(SDBUS_XML2CPP sdbus-c++-xml2cpp REQUIRED)
set(PRIOMIX_ADAPTOR ${CMAKE_CURRENT_BINARY_DIR}/priomix_adaptor.h)
add_custom_command(
    OUTPUT ${PRIOMIX_ADAPTOR}
    COMMAND ${SDBUS_XML2CPP} ${PRIOMIX_DBUS_XML} --adaptor=${PRIOMIX_ADAPTOR}
    DEPENDS ${PRIOMIX_DBUS_XML}
    COMMENT "Generating D-Bus adaptor from io.github.saijsaij.Priomix.xml"
    VERBATIM)

# Everything except main.cpp, shared with the unit tests.
add_library(priomix-core STATIC
    chatmix_engine.cpp
    config_store.cpp
    daemon.cpp
    dbus_service.cpp
    device_cache.cpp
    device_id.cpp
    event_timer.cpp
    headset_monitor.cpp
    json.cpp
    logging.cpp
    pipewire_registry.cpp
    power_state.cpp
    priority_engine.cpp
    ${PRIOMIX_ADAPTOR})
target_include_directories(priomix-core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(priomix-core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(priomix-core
    PUBLIC
        SDBusCpp::sdbus-c++
        PkgConfig::PIPEWIRE
        PkgConfig::SYSTEMD
    PRIVATE
        priomix-headsetcontrol)

add_executable(priomix-daemon main.cpp)
target_compile_options(priomix-daemon PRIVATE -Wall -Wextra)
target_link_libraries(priomix-daemon PRIVATE priomix-core)
install(TARGETS priomix-daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
