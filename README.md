# Flameshot for Hyprland

A [Flameshot](https://github.com/flameshot-org/flameshot) fork that captures
through `grim` and uses a layer-shell overlay without disturbing fullscreen
windows.

On Arch / CachyOS:

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-svg qt6-tools qt6-wayland layer-shell-qt kguiaddons grim
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DUSE_LAYER_SHELL=ON -DUSE_WAYLAND_CLIPBOARD=ON -DDISABLE_UPDATE_CHECKER=ON
cmake --build build
sudo cmake --install build
QT_QPA_PLATFORM=wayland flameshot gui
```

Quit any existing Flameshot instance before starting this build.
Bind `flameshot gui` to your screenshot key; `flameshot --help` lists commands.

[Upstream usage](https://flameshot.org/docs/guide/).

Code: [GPL-3.0-or-later](LICENSE). Logo:
[Free Art License 1.3](data/img/app/flameshotLogoLicense.txt). Google Material
Design Icons: [Apache-2.0](data/img/material/LICENSE.txt).

`capturewidget.cpp` and `.h` derive from Lightscreen's
[areadialog.cpp](https://github.com/ckaiser/Lightscreen/blob/master/dialogs/areadialog.cpp)
and [.h](https://github.com/ckaiser/Lightscreen/blob/master/dialogs/areadialog.h)
(GPLv2). Additional code comes from KSnapshot's `regiongrabber.cpp` r796531
(LGPL) and [Qt-Color-Widgets](https://github.com/mbasaglia/Qt-Color-Widgets)
(LGPL/GPL; see its licence and exceptions).

If I take code from your project and that implies a relicense to GPLv3+, you can reuse my changes with the original previous license of your project applied.
