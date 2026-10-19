// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

class QPixmap;
class QScreen;

namespace Hyprland {

// The screen of Hyprland's focused monitor, asked over its IPC socket.
// Wayland hides the cursor position from clients, but Hyprland will say
// which monitor has focus. nullptr outside Hyprland or on any failure.
QScreen* focusedScreen();

// The whole desktop from grim, as an uncompressed PPM. On Hyprland the
// screenshot portal runs grim itself but has it write a compressed PNG,
// which takes over a second on a large desktop; this is the same image in
// about a tenth of that. False outside Hyprland or on any failure, so the
// caller can fall back to the portal.
bool grabDesktop(QPixmap& screenshot);

}
