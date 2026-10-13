# Bundled components

Electron and Chromium notices are in the app's Resources directory. Python's license and the QMK firmware license accompany this file.

The firmware service is frozen with PyInstaller (GPL with its bootloader exception). dfu-util 0.11 is GPL-2.0-or-later; its statically linked libusb 1.0.29 is LGPL-2.1-or-later. Their license texts, unmodified complete source archives, and the build recipe are in this directory. The recipe builds libusb statically and links dfu-util against that archive plus macOS system frameworks. The backend does not link to dfu-util; it invokes its command-line interface.

Source projects:

- https://www.electronjs.org/
- https://www.python.org/
- https://pyinstaller.org/
- https://dfu-util.sourceforge.net/
- https://github.com/libusb/libusb

The bundled Keychron firmware is built from this project's QMK checkout. Retain that exact checkout and its modifications alongside any public firmware distribution, including QMK submodule sources. The embedded release identity and checksum are recorded in build-info.json.
