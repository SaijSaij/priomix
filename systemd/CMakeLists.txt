# SPDX-License-Identifier: GPL-3.0-only
configure_file(priomix-daemon.service.in ${CMAKE_CURRENT_BINARY_DIR}/priomix-daemon.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/priomix-daemon.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
