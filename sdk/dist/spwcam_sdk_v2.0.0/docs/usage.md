# SPWater Camera SDK — Usage Guide

## Quick Start (C)

```c
#include "spwcam/spwcam.h"

// 1. Init
spwcam_init_params_t p = {0};
p.struct_size = sizeof(p);
p.log_level   = SPWCAM_LOG_INFO;
spwcam_context_t ctx = spwcam_init(&p);

// 2. Log callback
spwcam_set_log_callback(ctx,
    [](spwcam_log_level_t lvl, const char* msg, void* ud){
        printf("[%d] %s\n", lvl, msg);
    }, NULL);

// 3. Discovery
spwcam_discovery_start(ctx, 0, 0);  // default ports

// 4. Wait for device (poll)
while (spwcam_get_device_count(ctx) == 0) Sleep(200);

// 5. Get device and build RTSP URL
spwcam_device_info_t di = {0};
di.struct_size = sizeof(di);
spwcam_get_device_info(ctx, 0, &di);
char url[256];
snprintf(url, sizeof(url), "rtsp://%s:%u%s", di.ip, di.rtsp_port, di.rtsp_path);

// 6. Open stream
spwcam_stream_open(ctx, url, 0);

// 7. Grab frames (pull mode)
while (running) {
    spwcam_frame_t* frame = NULL;
    if (spwcam_frame_grab(ctx, &frame) == SPWCAM_OK) {
        // use frame->data (BGRA32, frame->width x frame->height)
        spwcam_frame_free(frame);
    }
    Sleep(33);
}

// 8. Record
spwcam_record_options_t opts = {0};
opts.struct_size = sizeof(opts);
strncpy(opts.video_dir, "spwcam_record", sizeof(opts.video_dir)-1);      /* prefer absolute paths */
strncpy(opts.snapshot_dir, "spwcam_snapshot", sizeof(opts.snapshot_dir)-1);
/* fps / bitrate_kbps / segment_minutes are fixed at 25 / 8000 / 30 in v2.x */
opts.container = SPWCAM_CONTAINER_MP4;
spwcam_record_set_options(ctx, &opts);
spwcam_record_start(ctx);
Sleep(5000);
spwcam_snapshot(ctx);
spwcam_record_stop(ctx);

// 9. Cleanup
spwcam_stream_close(ctx);
spwcam_deinit(ctx);
```

---

## Quick Start (C++)

```cpp
#include "spwcam/spwcam.hpp"

spwcam::Camera cam;
cam.init();

cam.on_log([](spwcam_log_level_t lvl, std::string msg){
    std::cout << msg << "\n";
});
cam.on_event([](spwcam_event_type_t ev, std::string data){
    std::cout << "event " << ev << ": " << data << "\n";
});

cam.discovery_start();
auto dev = cam.wait_for_device(10.0);  // wait up to 10 s
if (!dev) { /* no device */ }
else {
    cam.stream_open(dev->rtsp_url());
    cam.wait_for_stream(15.0);

    // push callback
    cam.on_frame([](const spwcam_frame_t& f){
        // f.data  = BGRA32 pixels
        // f.width, f.height, f.stride
    });

    cam.record_set_options("spwcam_record", "spwcam_snapshot");   // prefer absolute paths
    cam.record_start();                 // async: RECORD_STARTED event carries the file path
    std::this_thread::sleep_for(std::chrono::seconds(10));
    cam.snapshot();                     // async: SNAPSHOT_SAVED event carries the file path
    cam.record_stop();                  // async: RECORD_STOPPED event when the file is closed
    cam.stream_close();
}
cam.deinit();
```

Note: `wait_for_device()` returns `std::unique_ptr<spwcam::DeviceInfo>` (null on timeout) — check with `if (dev)`; `wait_for_stream()` returns `bool`. Both are plain polling helpers; the header is C++11-compatible.

---

## Quick Start (Python)

```python
from spwcam import Camera
import time

cam = Camera()
cam.on_log(lambda lvl, msg: print(f"[{lvl}] {msg}"))
cam.init()
cam.on_event(lambda ev, data: print(f"event {ev}: {data}"))

cam.discovery_start()
dev = cam.wait_for_device(timeout_s=10)
if dev:
    print(f"Device: {dev.sn}  URL: {dev.rtsp_url}")
    cam.stream_open(dev.rtsp_url)
    cam.wait_for_stream(15)

    cam.record_set_options("spwcam_record", "spwcam_snapshot")   # prefer absolute paths
    cam.record_start()
    time.sleep(10)
    cam.snapshot()
    cam.record_stop()
    cam.stream_close()

cam.deinit()
```

### NumPy frame grab

```python
import numpy as np

while True:
    arr = cam.frame_grab_numpy()   # H x W x 4, BGRA, uint8
    if arr is not None:
        bgr = arr[:, :, :3]        # drop alpha channel
        # pass to OpenCV: cv2.imshow("cam", bgr)
    time.sleep(0.033)
```

---

## Frame Formats

All frames are delivered as **BGRA32** by default (4 bytes/pixel, B G R A).  
This matches the internal `QImage::Format_ARGB32` memory layout on little-endian x86.

To convert to BGR for OpenCV (Python):
```python
bgr = arr[:, :, :3]          # drop alpha
bgr = arr[:, :, [2,1,0,3]]   # if you need RGB
```

To convert to BGR for OpenCV (C++):
```cpp
// frame->data is BGRA; OpenCV Mat wants BGR
cv::Mat bgra(frame->height, frame->width, CV_8UC4, frame->data, frame->stride);
cv::Mat bgr;
cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
```

---

## Device Control

### Exposure and Gain

```c
// exposure_us: 10000..30000 µs
// gain_db:       0.0..15.0 dB
spwcam_set_camera_params(ctx, sn, 20000, 7.5);
```

### LED

```c
spwcam_set_led(ctx, sn, 1);  // on
spwcam_set_led(ctx, sn, 0);  // off
```

### Trigger Mode

```c
spwcam_set_trigger_mode(ctx, sn, SPWCAM_TRIGGER_SOFTWARE);
spwcam_set_trigger_mode(ctx, sn, SPWCAM_TRIGGER_HARDWARE);
```

### Change IP

```c
// Asynchronous; result via SPWCAM_EVENT_IP_CHANGED
spwcam_set_ip(ctx, sn, "192.168.1.50", 24);
```

---

## Recording Options

| Field | Default | Notes |
|-------|---------|-------|
| `video_dir` | `"D:/SP_camera_record"` | Created if missing. Files go to `<video_dir>/<YYYY-MM-DD>/<timestamp>.mp4` |
| `snapshot_dir` | `"D:/SP_camera_capture"` | Created if missing. Files go to `<snapshot_dir>/<YYYY-MM-DD>/<timestamp>.<ext>` |
| `fps` | 25 | **Fixed at 25 in v2.x** (other values ignored, warning logged) |
| `bitrate_kbps` | 8000 | **Fixed at 8000 in v2.x** (other values ignored, warning logged) |
| `segment_minutes` | 30 | **Fixed at 30 in v2.x** (other values ignored, warning logged) |
| `container` | `SPWCAM_CONTAINER_MP4` | MP4 only; AVI falls back to MP4 with a warning |
| `snapshot_fmt` | `SPWCAM_IMAGE_PNG` | PNG / JPG / BMP |

Always set `video_dir` / `snapshot_dir` explicitly: the built-in defaults assume a `D:` drive.

Recording is asynchronous: `spwcam_record_start()` returns immediately, the
encoder is opened on the next frame and `SPWCAM_EVENT_RECORD_STARTED` fires
with the file path.  Every 30 minutes the file is closed and a new one opened;
`SPWCAM_EVENT_RECORD_SEGMENT` fires with the new path and
`spwcam_record_status_t.segment_index` increments.  `spwcam_record_stop()`
returns immediately; `SPWCAM_EVENT_RECORD_STOPPED` fires once the file is
closed.  `spwcam_deinit()` finalises an active recording synchronously, so the
file is always playable.

---

## Thread Safety

| Operation | Thread-safe? |
|-----------|-------------|
| All `spwcam_*` API functions | Yes — callable from any thread; most execute synchronously on the SDK's Qt thread |
| Log / event / frame callbacks | Invoked on the SDK's Qt thread with **no SDK lock held** — calling `spwcam_*` from inside a callback is allowed |
| Long-running work inside a callback | Avoid: while a callback runs no frames are pumped and no events are dispatched |
| `spwcam_frame_free()` | Yes |
| Accessing `spwcam_frame_t` data after callback returns | No — copy pixel data first |
| Multiple contexts in one process | Yes — the internal Qt thread is shared and reference counted |

If the host application already owns a `QCoreApplication`, the SDK uses it and
callbacks are delivered on the **host's Qt main thread**.

---

## Stream States and Events

```
STOPPED ──stream_open()──▶ CONNECTING ──first frame──▶ RUNNING
   ▲                            ▲                         │
   │                            └──frames resume──────────┤ (STREAM_CONNECTED)
   │                                                      ▼
   └────────stream_close()──────────────── ERROR ◀── no frame for 5 s (STREAM_LOST)
```

The SDK reconnects automatically and indefinitely while in CONNECTING / ERROR.
`SPWCAM_EVENT_DEVICE_DISCOVERED` fires once when a device comes online;
`SPWCAM_EVENT_DEVICE_LOST` fires after 10 s without heartbeat.

---

## Lifetime Rules

```
spwcam_init() → spwcam_context_t ctx
    └── spwcam_stream_open(ctx, ...)
    └── [frame callbacks, event callbacks]
    └── spwcam_record_start(ctx)
    └── spwcam_record_stop(ctx)
    └── spwcam_stream_close(ctx)
spwcam_deinit(ctx)   ← ctx is invalid after this
```

A context may be reused: close stream → open again → etc.  
Do not share a context across processes.
