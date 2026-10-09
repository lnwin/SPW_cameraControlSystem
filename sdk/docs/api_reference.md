# SPWater Camera SDK — API Reference

## Conventions

- All functions are `__cdecl` and exported with `__declspec(dllexport)`.
- All strings are UTF-8, NUL-terminated.
- Every struct has a `uint32_t struct_size` first field that **the caller must set to `sizeof(T)`** before passing the struct.
- Memory allocated by the SDK (e.g. `spwcam_frame_t`) must be released by the corresponding SDK free function.
- Return value `0` = `SPWCAM_OK`; negative values = error codes.

---

## Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `SPWCAM_OK` | 0 | Success |
| `SPWCAM_ERR_INVALID_PARAM` | -1 | NULL pointer or out-of-range value |
| `SPWCAM_ERR_NOT_INITIALIZED` | -2 | Context is NULL |
| `SPWCAM_ERR_ALREADY_OPEN` | -3 | Resource already open |
| `SPWCAM_ERR_NOT_OPEN` | -4 | Stream not open / not running |
| `SPWCAM_ERR_DEVICE_NOT_FOUND` | -5 | SN not in device list |
| `SPWCAM_ERR_TIMEOUT` | -6 | Operation timed out |
| `SPWCAM_ERR_IO` | -7 | I/O failure |
| `SPWCAM_ERR_ENCODER` | -8 | FFmpeg encoder error |
| `SPWCAM_ERR_NO_FRAME` | -9 | No new frame available (not an error) |
| `SPWCAM_ERR_OUT_OF_MEMORY` | -10 | Allocation failed |
| `SPWCAM_ERR_INTERNAL` | -99 | Internal SDK error |

---

## Lifecycle

### `spwcam_version`
```c
const char* spwcam_version(void);
```
Returns the SDK version string (e.g. `"2.0.0"`). Never returns NULL. Thread-safe.

---

### `spwcam_get_error_string`
```c
const char* spwcam_get_error_string(int error_code);
```
Returns a human-readable description. Returns `"Unknown error"` for unrecognised codes.

---

### `spwcam_init`
```c
spwcam_context_t spwcam_init(const spwcam_init_params_t* params);
```
Creates an SDK context. Starts an internal Qt event-loop thread if `QCoreApplication` does not already exist (the thread is shared and reference-counted across contexts, so several contexts per process are allowed). Initialises GStreamer. Prepends the SDK `bin` directory to the process `PATH` (once).

`params` may be NULL (all defaults). If provided, `params->struct_size` must equal `sizeof(spwcam_init_params_t)`.

Returns an opaque handle on success, NULL on failure.

**Default ports:** discover=7776, heartbeat=8888, cmd=7777.

---

### `spwcam_deinit`
```c
void spwcam_deinit(spwcam_context_t ctx);
```
Releases all resources. Safe to call with `ctx == NULL`. Stops the stream and any active recording before cleanup.

---

### `spwcam_get_last_error`
```c
int spwcam_get_last_error(spwcam_context_t ctx);
```
Returns the last error code stored for this context. Thread-safe.

---

## Logging

### `spwcam_set_log_callback`
```c
void spwcam_set_log_callback(spwcam_context_t ctx,
                              spwcam_log_callback_t cb,
                              void* user_data);
```
Install a log callback. Pass `cb=NULL` to remove. Called on the SDK's Qt thread.

No SDK lock is held while the callback runs; calling SDK functions from inside
any callback is allowed. Keep callbacks short — frames and events are not
dispatched while a callback is executing.

---

### `spwcam_set_log_level`
```c
void spwcam_set_log_level(spwcam_context_t ctx, spwcam_log_level_t level);
```
Sets minimum log level forwarded to callback. Levels: DEBUG(0) < INFO(1) < WARN(2) < ERROR(3).

---

## Events

### `spwcam_set_event_callback`
```c
void spwcam_set_event_callback(spwcam_context_t ctx,
                                spwcam_event_callback_t cb,
                                void* user_data);
```

| Event | `data` field | When |
|-------|-------------|------|
| `SPWCAM_EVENT_DEVICE_DISCOVERED` | Device SN | once, when a device first comes online |
| `SPWCAM_EVENT_DEVICE_LOST` | Device SN | no heartbeat for 10 s |
| `SPWCAM_EVENT_STREAM_CONNECTED` | NULL | first frame after `stream_open()` or after a loss |
| `SPWCAM_EVENT_STREAM_LOST` | NULL | no frame for 5 s while RUNNING; status becomes ERROR |
| `SPWCAM_EVENT_RECORD_STARTED` | File path | encoder opened (first frame after `record_start()`) |
| `SPWCAM_EVENT_RECORD_STOPPED` | File path | file closed after `record_stop()` |
| `SPWCAM_EVENT_RECORD_FAILED` | Error message | encoder could not be opened |
| `SPWCAM_EVENT_RECORD_SEGMENT` | New segment file path | automatic 30-minute split |
| `SPWCAM_EVENT_SNAPSHOT_SAVED` | Saved file path | |
| `SPWCAM_EVENT_TRIGGER_STATUS` | `"software"` / `"hardware"` / `"fallback"` | device reports its trigger mode |
| `SPWCAM_EVENT_IP_CHANGED` | `"ok:<new_ip>"` / `"failed:no_ack"` / `"failed:device_rejected"` / `"failed:reconnect_timeout"` | result of `spwcam_set_ip()` |

---

## Device Discovery

### `spwcam_discovery_start`
```c
int spwcam_discovery_start(spwcam_context_t ctx,
                            uint16_t discover_port,
                            uint16_t heartbeat_port);
```
Starts UDP device discovery. Pass 0 for ports to use the defaults set in `spwcam_init`.
Returns `SPWCAM_ERR_IO` if the UDP ports cannot be bound — typically because another process (e.g. the desktop application) already owns 7776/8888.

### `spwcam_discovery_stop`
```c
void spwcam_discovery_stop(spwcam_context_t ctx);
```

### `spwcam_get_device_count`
```c
int spwcam_get_device_count(spwcam_context_t ctx);
```
Returns the number of currently known devices.

### `spwcam_get_device_info`
```c
int spwcam_get_device_info(spwcam_context_t ctx, int index,
                            spwcam_device_info_t* info);
```
Fill `*info` for the device at `index` (0-based).  
Returns `SPWCAM_ERR_INVALID_PARAM` if index out of range.

### `spwcam_get_device_info_by_sn`
```c
int spwcam_get_device_info_by_sn(spwcam_context_t ctx, const char* sn,
                                   spwcam_device_info_t* info);
```
Returns `SPWCAM_ERR_DEVICE_NOT_FOUND` if SN not in device list.

---

## Device Control

### `spwcam_set_led`
```c
int spwcam_set_led(spwcam_context_t ctx, const char* sn, int enable);
```
`enable=0` → off; non-zero → on. Command is sent via UDP; no ACK is waited for.

### `spwcam_set_trigger_mode`
```c
int spwcam_set_trigger_mode(spwcam_context_t ctx, const char* sn,
                              spwcam_trigger_mode_t mode);
```

### `spwcam_set_camera_params`
```c
int spwcam_set_camera_params(spwcam_context_t ctx, const char* sn,
                               int exposure_us, double gain_db);
```
`exposure_us`: [10000 .. 30000] µs  
`gain_db`: [0.0 .. 15.0] dB

### `spwcam_set_ip`
```c
int spwcam_set_ip(spwcam_context_t ctx, const char* sn,
                   const char* new_ip, int mask);
```
Asynchronous. Result arrives via `SPWCAM_EVENT_IP_CHANGED`.  
`mask`: prefix length, e.g. 24 for 255.255.255.0.

---

## RTSP Stream

### `spwcam_stream_open`
```c
int spwcam_stream_open(spwcam_context_t ctx,
                        const char* rtsp_url, int latency_ms);
```
Returns immediately; the connection is established asynchronously and retried
indefinitely until `spwcam_stream_close()`.
`latency_ms ≤ 0` → SDK default (350 ms); other values are clamped to [300, 600].
Stream status transitions: STOPPED → CONNECTING → RUNNING ⇄ ERROR (no frame for 5 s; auto-reconnect).

### `spwcam_stream_close`
```c
int spwcam_stream_close(spwcam_context_t ctx);
```
Safe to call when already stopped.

### `spwcam_stream_get_status`
```c
spwcam_stream_status_t spwcam_stream_get_status(spwcam_context_t ctx);
```

---

## Frames

### `spwcam_set_frame_callback` (push mode)
```c
void spwcam_set_frame_callback(spwcam_context_t ctx,
                                spwcam_frame_callback_t cb,
                                void* user_data);
```
Fires ~25 fps when the stream is running.  
**The `spwcam_frame_t*` pointer is only valid during the callback.**  
Do NOT call `spwcam_frame_free()` on it.

### `spwcam_frame_grab` (pull mode)
```c
int spwcam_frame_grab(spwcam_context_t ctx, spwcam_frame_t** out_frame);
```
Returns `SPWCAM_ERR_NO_FRAME` if no new frame since last call (not an error condition).  
On success, `*out_frame` points to a heap-allocated frame. **Caller must call `spwcam_frame_free()`.**

### `spwcam_frame_free`
```c
void spwcam_frame_free(spwcam_frame_t* frame);
```
Safe to call with NULL.

---

## Recording

### `spwcam_record_set_options`
```c
int spwcam_record_set_options(spwcam_context_t ctx,
                               const spwcam_record_options_t* opts);
```
Call before `spwcam_record_start()`. Do not call while recording is active.
v2.x: `fps`, `bitrate_kbps`, `segment_minutes` are fixed at 25 / 8000 / 30 and
`SPWCAM_CONTAINER_AVI` falls back to MP4 (a warning is logged in each case).

### `spwcam_record_start` / `spwcam_record_stop`
```c
int spwcam_record_start(spwcam_context_t ctx);
int spwcam_record_stop(spwcam_context_t ctx);
```
Both are asynchronous. `record_start` returns `SPWCAM_ERR_NOT_OPEN` if the
stream is not RUNNING; the encoder opens on the next frame and
`SPWCAM_EVENT_RECORD_STARTED` fires. `record_stop` closes the file and fires
`SPWCAM_EVENT_RECORD_STOPPED`. `spwcam_deinit()` finalises an active recording
synchronously.

### `spwcam_record_get_status`
```c
int spwcam_record_get_status(spwcam_context_t ctx,
                              spwcam_record_status_t* status);
```

### `spwcam_snapshot`
```c
int spwcam_snapshot(spwcam_context_t ctx);
```
Asynchronous. File path arrives via `SPWCAM_EVENT_SNAPSHOT_SAVED`.  
Returns `SPWCAM_ERR_NOT_OPEN` if the stream is not running and `SPWCAM_ERR_NO_FRAME` if no frame has been received yet. Uses the most recently received frame.
