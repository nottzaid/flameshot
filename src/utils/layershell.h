// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

class QScreen;
class QWidget;

// The capture window as a wlr-layer-shell overlay instead of a fullscreen
// toplevel. A fullscreen toplevel is still a window to the compositor: on
// Hyprland it takes the workspace's single fullscreen slot and knocks the
// window that held it out of fullscreen. An overlay-layer surface sits above
// panels and windows without touching the window layout.
namespace LayerShell {

// Built with layer-shell-qt, running on Wayland, and the compositor offers
// zwlr_layer_shell_v1.
bool available();

// Make the not yet shown widget an overlay-layer surface covering screen.
// Show it with show(), not showFullScreen().
void makeOverlay(QWidget* widget, QScreen* screen);

}
