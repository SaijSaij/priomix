# SPDX-License-Identifier: GPL-3.0-only
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets DBus)
find_package(KF6GlobalAccel REQUIRED)

set(CMAKE_AUTOMOC ON)

# Proxy class generated from the shared interface definition.
set_source_files_properties(${PRIOMIX_DBUS_XML} PROPERTIES
    CLASSNAME PriomixInterface
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/device_info.hpp
    NO_NAMESPACE ON)
set(PRIOMIX_GUI_DBUS_SOURCES)
qt_add_dbus_interface(PRIOMIX_GUI_DBUS_SOURCES ${PRIOMIX_DBUS_XML} priomix_interface)

add_executable(priomix-gui
    dbus_client.cpp
    device_info.cpp
    global_shortcuts.cpp
    main.cpp
    main_window.cpp
    settings_dialog.cpp
    ${PRIOMIX_GUI_DBUS_SOURCES})
target_include_directories(priomix-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                               ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(priomix-gui PRIVATE
    PRIOMIX_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII)
target_compile_options(priomix-gui PRIVATE -Wall -Wextra)
target_link_libraries(priomix-gui PRIVATE Qt6::Widgets Qt6::DBus KF6::GlobalAccel)

install(TARGETS priomix-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES io.github.saijsaij.Priomix.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
