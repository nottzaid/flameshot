// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "layershell.h"

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>

#if defined(USE_LAYER_SHELL)
#include <LayerShellQt/Window>
#include <wayland-client.h>

namespace {

void registryGlobal(void* data,
                    wl_registry*,
                    uint32_t,
                    const char* interface,
                    uint32_t)
{
    if (qstrcmp(interface, "zwlr_layer_shell_v1") == 0) {
        *static_cast<bool*>(data) = true;
    }
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registryListener = { registryGlobal,
                                                registryGlobalRemove };

// Asks the compositor on a private event queue, so Qt's own queue is left
// alone. layer-shell-qt only warns when the protocol is missing and the
// window would then show as a plain, non-fullscreen toplevel.
bool compositorHasLayerShell()
{
    auto* app =
      qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (app == nullptr || app->display() == nullptr) {
        return false;
    }
    wl_display* display = app->display();
    wl_event_queue* queue = wl_display_create_queue(display);
    auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
    wl_registry* registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);

    bool found = false;
    wl_registry_add_listener(registry, &registryListener, &found);
    wl_display_roundtrip_queue(display, queue);
    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);
    return found;
}

}
#endif

bool LayerShell::available()
{
#if defined(USE_LAYER_SHELL)
    static const bool result =
      QGuiApplication::platformName() == QLatin1String("wayland") &&
      compositorHasLayerShell();
    return result;
#else
    return false;
#endif
}

void LayerShell::makeOverlay(QWidget* widget, QScreen* screen)
{
#if defined(USE_LAYER_SHELL)
    using LayerWindow = LayerShellQt::Window;
    widget->winId(); // creates the QWindow the layer surface attaches to
    LayerWindow* layer = LayerWindow::get(widget->windowHandle());
    layer->setScope(QStringLiteral("flameshot"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(
      LayerWindow::Anchors(LayerWindow::AnchorTop | LayerWindow::AnchorBottom |
                           LayerWindow::AnchorLeft | LayerWindow::AnchorRight));
    // Cover panels' exclusive zones too, like a fullscreen window would.
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(
      LayerWindow::KeyboardInteractivityExclusive);
    if (screen != nullptr) {
        layer->setScreen(screen);
    }
#else
    Q_UNUSED(widget)
    Q_UNUSED(screen)
#endif
}
