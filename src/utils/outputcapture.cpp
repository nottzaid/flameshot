// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "outputcapture.h"

#include <QPixmap>
#include <QScreen>

#if defined(USE_IMAGE_COPY_CAPTURE)
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QImage>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

namespace {

// A compositor answers within a frame or two; past this, give up and let
// the caller grab the desktop another way.
constexpr int captureTimeoutMs = 500;

struct Capture
{
    wl_shm* shm = nullptr;
    ext_output_image_capture_source_manager_v1* sources = nullptr;
    ext_image_copy_capture_manager_v1* copier = nullptr;

    // Buffer constraints, complete once the session sends done.
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    QImage::Format imageFormat = QImage::Format_Invalid;
    bool constraintsDone = false;
    bool stopped = false;

    uint32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
    bool ready = false;
    bool failed = false;
};

void registryGlobal(void* data,
                    wl_registry* registry,
                    uint32_t name,
                    const char* interface,
                    uint32_t)
{
    auto* capture = static_cast<Capture*>(data);
    if (qstrcmp(interface, wl_shm_interface.name) == 0) {
        capture->shm = static_cast<wl_shm*>(
          wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (qstrcmp(interface,
                       ext_output_image_capture_source_manager_v1_interface
                         .name) == 0) {
        capture->sources =
          static_cast<ext_output_image_capture_source_manager_v1*>(
            wl_registry_bind(
              registry,
              name,
              &ext_output_image_capture_source_manager_v1_interface,
              1));
    } else if (qstrcmp(interface,
                       ext_image_copy_capture_manager_v1_interface.name) ==
               0) {
        capture->copier = static_cast<ext_image_copy_capture_manager_v1*>(
          wl_registry_bind(
            registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    }
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registryListener = { registryGlobal,
                                                registryGlobalRemove };

void sessionBufferSize(void* data,
                       ext_image_copy_capture_session_v1*,
                       uint32_t width,
                       uint32_t height)
{
    auto* capture = static_cast<Capture*>(data);
    capture->width = width;
    capture->height = height;
}

// Formats QImage can wrap as they are. Screen contents are opaque, so the
// alpha variants are read as their X counterparts.
void sessionShmFormat(void* data,
                      ext_image_copy_capture_session_v1*,
                      uint32_t format)
{
    auto* capture = static_cast<Capture*>(data);
    if (capture->imageFormat == QImage::Format_RGB32) {
        return; // already have the preferred one
    }
    switch (format) {
        case WL_SHM_FORMAT_XRGB8888:
        case WL_SHM_FORMAT_ARGB8888:
            capture->format = format;
            capture->imageFormat = QImage::Format_RGB32;
            break;
        case WL_SHM_FORMAT_XBGR8888:
        case WL_SHM_FORMAT_ABGR8888:
            capture->format = format;
            capture->imageFormat = QImage::Format_RGBX8888;
            break;
        default:
            break;
    }
}

void sessionDmabufDevice(void*,
                         ext_image_copy_capture_session_v1*,
                         wl_array*)
{}

void sessionDmabufFormat(void*,
                         ext_image_copy_capture_session_v1*,
                         uint32_t,
                         wl_array*)
{}

void sessionDone(void* data, ext_image_copy_capture_session_v1*)
{
    static_cast<Capture*>(data)->constraintsDone = true;
}

void sessionStopped(void* data, ext_image_copy_capture_session_v1*)
{
    static_cast<Capture*>(data)->stopped = true;
}

const ext_image_copy_capture_session_v1_listener sessionListener = {
    sessionBufferSize,   sessionShmFormat, sessionDmabufDevice,
    sessionDmabufFormat, sessionDone,      sessionStopped
};

void frameTransform(void* data,
                    ext_image_copy_capture_frame_v1*,
                    uint32_t transform)
{
    static_cast<Capture*>(data)->transform = transform;
}

void frameDamage(void*,
                 ext_image_copy_capture_frame_v1*,
                 int32_t,
                 int32_t,
                 int32_t,
                 int32_t)
{}

void framePresentationTime(void*,
                           ext_image_copy_capture_frame_v1*,
                           uint32_t,
                           uint32_t,
                           uint32_t)
{}

void frameReady(void* data, ext_image_copy_capture_frame_v1*)
{
    static_cast<Capture*>(data)->ready = true;
}

void frameFailed(void* data, ext_image_copy_capture_frame_v1*, uint32_t)
{
    static_cast<Capture*>(data)->failed = true;
}

const ext_image_copy_capture_frame_v1_listener frameListener = {
    frameTransform, frameDamage, framePresentationTime, frameReady, frameFailed
};

// Dispatches the private queue until `done` holds or the deadline passes.
// Qt's event thread reads the same socket; preparing, reading and
// cancelling reads is how libwayland lets several readers share it.
template<typename Done>
bool dispatchUntil(wl_display* display,
                   wl_event_queue* queue,
                   const QDeadlineTimer& deadline,
                   Done done)
{
    while (!done()) {
        while (wl_display_prepare_read_queue(display, queue) != 0) {
            if (wl_display_dispatch_queue_pending(display, queue) < 0) {
                return false;
            }
            if (done()) {
                return true;
            }
        }
        wl_display_flush(display);
        pollfd pfd = { wl_display_get_fd(display), POLLIN, 0 };
        const int timeout = static_cast<int>(deadline.remainingTime());
        if (timeout <= 0 || poll(&pfd, 1, timeout) <= 0) {
            wl_display_cancel_read(display);
            return false;
        }
        if (wl_display_read_events(display) < 0 ||
            wl_display_dispatch_queue_pending(display, queue) < 0) {
            return false;
        }
    }
    return true;
}

// Copies one frame of the output into `image`.
bool copyOutput(wl_display* display,
                wl_event_queue* queue,
                Capture& capture,
                wl_output* output,
                QImage& image)
{
    const QDeadlineTimer deadline(captureTimeoutMs);

    ext_image_capture_source_v1* source =
      ext_output_image_capture_source_manager_v1_create_source(
        capture.sources, output);
    ext_image_copy_capture_session_v1* session =
      ext_image_copy_capture_manager_v1_create_session(
        capture.copier, source, 0);
    ext_image_copy_capture_session_v1_add_listener(
      session, &sessionListener, &capture);

    bool ok =
      dispatchUntil(display, queue, deadline, [&capture] {
          return capture.constraintsDone || capture.stopped;
      }) &&
      !capture.stopped && capture.imageFormat != QImage::Format_Invalid &&
      capture.width > 0 && capture.height > 0;

    const int stride = static_cast<int>(capture.width) * 4;
    const size_t size = static_cast<size_t>(stride) * capture.height;
    void* pixels = MAP_FAILED;
    wl_buffer* buffer = nullptr;
    if (ok) {
        const int fd = memfd_create("flameshot-capture", MFD_CLOEXEC);
        if (fd >= 0 && ftruncate(fd, static_cast<off_t>(size)) == 0) {
            pixels =
              mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        }
        if (pixels != MAP_FAILED) {
            // The request takes its own copy of the fd.
            wl_shm_pool* pool =
              wl_shm_create_pool(capture.shm, fd, static_cast<int32_t>(size));
            buffer = wl_shm_pool_create_buffer(pool,
                                               0,
                                               static_cast<int32_t>(
                                                 capture.width),
                                               static_cast<int32_t>(
                                                 capture.height),
                                               stride,
                                               capture.format);
            wl_shm_pool_destroy(pool);
        }
        if (fd >= 0) {
            close(fd);
        }
        ok = buffer != nullptr;
    }

    ext_image_copy_capture_frame_v1* frame = nullptr;
    if (ok) {
        frame = ext_image_copy_capture_session_v1_create_frame(session);
        ext_image_copy_capture_frame_v1_add_listener(
          frame, &frameListener, &capture);
        ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
        ext_image_copy_capture_frame_v1_damage_buffer(
          frame,
          0,
          0,
          static_cast<int32_t>(capture.width),
          static_cast<int32_t>(capture.height));
        ext_image_copy_capture_frame_v1_capture(frame);
        ok = dispatchUntil(display,
                           queue,
                           deadline,
                           [&capture] {
                               return capture.ready || capture.failed;
                           }) &&
             capture.ready && capture.transform == WL_OUTPUT_TRANSFORM_NORMAL;
    }
    if (ok) {
        image = QImage(static_cast<const uchar*>(pixels),
                       static_cast<int>(capture.width),
                       static_cast<int>(capture.height),
                       stride,
                       capture.imageFormat)
                  .copy();
        ok = !image.isNull();
    }

    if (frame != nullptr) {
        ext_image_copy_capture_frame_v1_destroy(frame);
    }
    if (buffer != nullptr) {
        wl_buffer_destroy(buffer);
    }
    if (pixels != MAP_FAILED) {
        munmap(pixels, size);
    }
    ext_image_copy_capture_session_v1_destroy(session);
    ext_image_capture_source_v1_destroy(source);
    return ok;
}

}
#endif

bool OutputCapture::grab(QScreen* screen, QPixmap& screenshot)
{
#if defined(USE_IMAGE_COPY_CAPTURE)
    auto* app =
      qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    auto* waylandScreen =
      screen->nativeInterface<QNativeInterface::QWaylandScreen>();
    if (app == nullptr || app->display() == nullptr ||
        waylandScreen == nullptr || waylandScreen->output() == nullptr) {
        return false;
    }

    // Everything runs on a private queue, leaving Qt's own queue alone.
    wl_display* display = app->display();
    wl_event_queue* queue = wl_display_create_queue(display);
    auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
    wl_registry* registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);

    Capture capture;
    wl_registry_add_listener(registry, &registryListener, &capture);
    wl_display_roundtrip_queue(display, queue);

    QImage image;
    const bool ok =
      capture.shm != nullptr && capture.sources != nullptr &&
      capture.copier != nullptr &&
      copyOutput(display, queue, capture, waylandScreen->output(), image);

    if (capture.copier != nullptr) {
        ext_image_copy_capture_manager_v1_destroy(capture.copier);
    }
    if (capture.sources != nullptr) {
        ext_output_image_capture_source_manager_v1_destroy(capture.sources);
    }
    if (capture.shm != nullptr) {
        wl_shm_destroy(capture.shm);
    }
    wl_registry_destroy(registry);
    // Sends the destructors before their queue goes.
    wl_display_flush(display);
    wl_event_queue_destroy(queue);

    if (!ok) {
        return false;
    }

    // Wayland hands over physical pixels; fit them to the screen as Qt sees
    // it, as cropping the whole desktop to one monitor does.
    const qreal dpr = screen->devicePixelRatio();
    const QSize physical(qRound(screen->geometry().width() * dpr),
                         qRound(screen->geometry().height() * dpr));
    if (image.size() != physical) {
        image = image.scaled(
          physical, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    screenshot = QPixmap::fromImage(std::move(image));
    screenshot.setDevicePixelRatio(dpr);
    return true;
#else
    Q_UNUSED(screen)
    Q_UNUSED(screenshot)
    return false;
#endif
}
