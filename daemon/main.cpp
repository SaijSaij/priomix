// SPDX-License-Identifier: GPL-3.0-only
//
// priomix-daemon: audio device priority daemon for PipeWire.
//
// Single threaded: one sd-event loop drives everything.
//   sd_event_new()
//     +- session bus (sdbus-c++ attachSdEventLoop)   our D-Bus service
//     +- system bus  (sdbus-c++ attachSdEventLoop)   UPower OnBattery
//     +- sd_event_add_io(pw_loop_get_fd())           PipeWire events
//     +- timers                                      headset polling, debouncing
//   sd_event_loop()

#include <systemd/sd-event.h>

#include <csignal>
#include <cstring>

#include "daemon.hpp"
#include "logging.hpp"

int main() {
    sd_event* event = nullptr;
    int r = sd_event_default(&event);
    if (r < 0) {
        PRIOMIX_LOG_ERROR("Cannot create event loop: %s", std::strerror(-r));
        return 1;
    }

    // Signals must be blocked before sd_event_add_signal() can handle them. A NULL
    // handler makes sd-event exit the loop.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    sd_event_add_signal(event, nullptr, SIGTERM, nullptr, nullptr);
    sd_event_add_signal(event, nullptr, SIGINT, nullptr, nullptr);

    int exit_code = 0;
    {
        priomix::Daemon daemon(event);
        if (!daemon.Start()) {
            exit_code = 1;
        } else {
            r = sd_event_loop(event);
            exit_code = r < 0 ? 1 : r;
            PRIOMIX_LOG_INFO("priomix-daemon shutting down");
            daemon.Shutdown();
        }
    }
    sd_event_unref(event);
    return exit_code;
}
