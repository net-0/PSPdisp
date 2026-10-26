# PSPdisp

PSPdisp turns a PSP into a second display of the computer: the PSP shows a virtual display, plays the desktop sound, and its buttons drive a virtual gamepad, keyboard or mouse.
It's a rewrite of [PSPdisp by JJS](http://jjs.at/software/pspdisp.html) (2008-2015), whose PSP app (`psp/source`) talks to a new desktop app.

## Desktop app (Linux)

Two programs, sharing their settings in `~/.config/pspdisp`, both running as the user:

- `pspdisp-daemon` streams to the PSP. It refuses to run as root.
- `pspdisp` is the desktop app, the settings and the status of the daemon. It can start the daemon.

### Requirements

- GCC 15 or later (C23 with `#embed`), and the development files of GLib, GTK 4, GUsb (with libusb), libpipewire, libturbojpeg, libxkbcommon (with its X11 part), libxcb, libwayland-client and [EVDI](https://github.com/DisplayLink/evdi).
- The EVDI kernel module.
- polkit, with the authentication agent of the desktop (KDE and GNOME have one).
- PipeWire, for the sound.
- On Arch: `sudo pacman -S gtk4 libgusb libpipewire libjpeg-turbo polkit libxkbcommon libxkbcommon-x11 libxcb wayland` and `evdi-dkms` (AUR).

### Building, installing and running

```sh
make build-desktop   # Both, in build/
sudo make install    # The programs, and what the daemon needs to run as the user (see below)
make run-ui          # The desktop app, its "Streaming" page starts the daemon
make run-daemon      # Or the daemon in a terminal
```

`make install` puts:

- `pspdisp` and `pspdisp-daemon` in `/usr/local/bin` (`PREFIX`).
- `60-pspdisp.rules` in `/etc/udev/rules.d`: it gives the PSP in USB mode and `/dev/uinput` (the virtual gamepad, keyboard and mouse) to the user at the computer. Plug the PSP again after installing it.
- `com.pspdisp.ui.svg` in `/usr/local/share/icons/hicolor/scalable/apps` and `com.pspdisp.ui.desktop` in `/usr/local/share/applications` (`DATADIR`): the icon of the desktop app and its entry in the menus.

`sudo make uninstall` removes them.

The daemon takes `--config-dir DIR` to use other settings, as the desktop app (only one daemon runs per user, whatever its settings).

### The EVDI device

The virtual display is an EVDI device, and only root can create one. Either:

- The module creates one when it loads, so the daemon never asks for the password (recommended):
  ```sh
  echo "options evdi initial_device_count=1" | sudo tee /etc/modprobe.d/evdi.conf
  echo evdi | sudo tee /etc/modules-load.d/evdi.conf   # Loaded at boot
  sudo modprobe -r evdi; sudo modprobe evdi            # Or reboot
  ```
- Or the daemon creates one when a PSP connects and none is free (e.g. a DisplayLink dock took it), if the module is loaded (`echo evdi | sudo tee /etc/modules-load.d/evdi.conf` to load it at boot): polkit asks for the password.
  The device stays once the PSP is gone, and is used again, so it's asked once until the next boot.
  The PSP may give up waiting meanwhile, it then connects again by itself.

The user at the computer can use any EVDI device without root: udev gives the graphics cards to the active session (`/dev/dri/card*`).

### Settings

`~/.config/pspdisp` holds:

- `pspdisp.conf`, the settings, edited by the desktop app, and saved by the daemon when they change on the PSP ("PC Settings" menu).
- `presets/*.preset`, the presets, listed on the PSP too.
- `control/*.control`, the control files: what the PSP buttons press on the keyboard and the mouse, in the format of the old desktop app (see `control.template`).

Both programs follow the changes of these files at once.

### Tests without a PSP

```sh
make run-test-display     # The virtual display, a frame saved each second in build/display_pictures/
make run-test-audio       # The desktop sound, recorded into build/test_audio.wav
make run-test-controller  # The virtual gamepad, playing each PSP button
```

## PSP app

Built from `psp/source` with the PSPSDK, see `psp/source/README` for the files to copy on the Memory Stick.

## Source tree

```
src/
├── shared.h, shared.old.h  # What the desktop and the PSP share (the old protocol in shared.old.h)
└── desktop/
    ├── audio/       # PSP audio and its connector (PipeWire, WASAPI)
    ├── controller/  # Virtual gamepad, keyboard and mouse driven by the PSP, and the control files
    ├── daemon/      # The daemon: the sessions with the PSPs
    ├── display/     # Virtual display (EVDI, IddCx)
    ├── settings/    # Settings shared by the daemon and the desktop app
    ├── status/      # Status of the daemon, sent to the desktop app
    ├── ui/          # The desktop app (GTK 4)
    ├── usb/         # USB link with the PSP (GUsb)
    └── wlan/        # WLAN link with the PSP (TCP)
psp/source/          # The PSP app
app/source/          # The old desktop app (Delphi, Windows), for reference
```

## Credits

The icon of the desktop app (`src/desktop/ui/com.pspdisp.ui.svg`) is based on [Psp.svg](https://commons.wikimedia.org/wiki/File:Psp.svg) by Mistman123, from Wikimedia Commons, with a desktop added on its screen.
The icon of the PSP app (`psp/source/ICON0.PNG`) is that icon rendered at 144x80, the size of the icons of the XMB.
Both are under the [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license, as the original, unlike the rest of PSPdisp (BSD-3-Clause, see `LICENSE`).
