// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

class QPixmap;
class QScreen;

namespace OutputCapture {

// One Wayland output, copied by the compositor straight into our memory with
// ext-image-copy-capture-v1: no helper process, no encoding, and only the
// pixels of that output. The pixmap carries the screen's device pixel ratio.
// False when built without it, when the compositor lacks the protocol, for
// a rotated or flipped output, or on any failure, so the caller can fall
// back to grabbing the whole desktop.
bool grab(QScreen* screen, QPixmap& screenshot);

}
