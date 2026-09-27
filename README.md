# priomix

Audio device priorities and ChatMix for PipeWire, built for KDE Plasma.

- **Separate priority lists** for game output, chat output and microphones. The first
  available, non-ignored device of each list wins; new devices are put on top.
- **Real power detection for wireless headsets** (e.g. SteelSeries Arctis Nova 7) via
  libheadsetcontrol: a plugged-in dongle with the headset switched off does not count as
  available. Polled every 5 s on AC and every 30 s on battery (UPower), configurable.
- **ChatMix**: two permanent virtual sinks, `priomix-game` (the default sink) and
  `priomix-chat` (pick it once in Discord & co.). Each one has a normal volume (tray,
  media keys, GUI) and a separate ChatMix dampening driven by the headset wheel or, without
  a wheel, by hotkeys / the GUI slider.

It consists of two programs that only talk over D-Bus (`io.github.saijsaij.Priomix`):

| Program          | Role                                                              |
|------------------|-------------------------------------------------------------------|
| `priomix-daemon` | systemd user service, owns all PipeWire, headset and config logic |
| `priomix-gui`    | Qt settings window, tray icon and global ChatMix hotkeys          |

## Building

Dependencies (Arch/CachyOS): `pipewire wireplumber sdbus-cpp hidapi systemd-libs qt6-base
kglobalaccel cmake ninja` (`gtest` for the tests, `upower` optional).

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

`PRIOMIX_HEADSETCONTROL` selects the HeadsetControl library: `AUTO` (default) uses the
system library if it actually links and otherwise builds HeadsetControl 4.0.0 from source
via CMake FetchContent. Arch's `headsetcontrol` package currently ships a static library
made of stripped slim-LTO objects (no code at all), so `AUTO` falls back to the bundled
build there. The `headsetcontrol` package is still useful for its udev rules, which give
your user access to the headset's HID interface.

Options: `PRIOMIX_BUILD_GUI`, `PRIOMIX_BUILD_TESTS`, `PRIOMIX_BUILD_DAEMON` (all `ON`).

## Installing and running

```bash
sudo cmake --install build
systemctl --user daemon-reload
systemctl --user enable --now priomix-daemon.service
priomix-gui
```

Or build a package with the included `PKGBUILD`.

`priomix-gui --minimized` starts directly into the tray; add it to Plasma's autostart to
have the hotkeys available after login. The hotkeys ("ChatMix: more game / more chat /
center") are assigned in System Settings → Keyboard → Shortcuts → priomix.

Debug output: `PRIOMIX_DEBUG=1 priomix-daemon`, or for the service
`systemctl --user edit priomix-daemon` with `Environment=PRIOMIX_DEBUG=1`.

## How it works

```
apps ──► priomix-game (Audio/Sink)           level 1: normal volume
            └► priomix-game.output (stream)  level 2: ChatMix dampening
                 └► target.object = object.serial of the winning physical device
```

- Both virtual sinks are `libpipewire-module-loopback` instances loaded into the daemon's
  own PipeWire context. The daemon sets `default.configured.audio.sink` to `priomix-game`
  once per start and afterwards only moves the loopback playback streams by writing
  `target.object` (type `Spa:Id`, value `object.serial`) into the `default` metadata, the
  same mechanism pavucontrol uses. No module reload, no audio gap.
- The playback streams use `node.dont-fallback` + `node.linger`, so WirePlumber never links
  them to the default sink (which would be `priomix-game` itself). While no device is
  available their dampening is set to 0, because WirePlumber keeps an existing link in that
  case.
- The microphone is switched through `default.configured.audio.source`.
- Everything runs single threaded on one `sd-event` loop: PipeWire's loop fd, the session
  bus (own service), the system bus (UPower) and all timers.

Stable device IDs: `usb:<vendor>:<product>`, `bt:<MAC>`, `alsa:<node name without
"alsa_">`, or `node:<node name>` for everything else. Two identical USB devices connected
at the same time are told apart by their bus path (best effort, stored as
`disambiguation_path`).

Files (owned by the daemon only):

- `~/.config/priomix-daemon/config.json` – priorities, ignore lists, volumes, intervals
- `~/.config/priomix-daemon/known_devices.json` – last seen display names (cache)

## License

GPL-3.0-only (the daemon links libheadsetcontrol, which is GPL-3.0).
