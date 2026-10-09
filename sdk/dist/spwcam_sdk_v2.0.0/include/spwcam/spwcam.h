/**
 * spwcam.h  --  SPWater Camera SDK  Public C API
 *
 * Author   : Yuanshi Technology
 * Copyright (C) Zhoushan Yuanshi Technology Co., Ltd.  All rights reserved.
 *
 * Unauthorized copying, modification, distribution or use of this file,
 * via any medium, is strictly prohibited without prior written permission.
 *
 * ABI contract:
 *   - Pure C99 / C++11-compatible header, ASCII only
 *   - All strings: UTF-8, NUL-terminated
 *   - All memory allocated by the SDK must be freed by spwcam_frame_free() /
 *     the SDK's own cleanup paths; caller never calls free() directly.
 *   - No STL / Qt / C++ types cross the DLL boundary.
 *   - Every struct starts with  uint32_t struct_size = sizeof(T).
 *     Callers MUST initialise struct_size before passing the struct to any function.
 *
 * Threading model:
 *   - All spwcam_* functions are thread-safe and may be called from any thread.
 *     Most of them are executed synchronously on the SDK's internal Qt thread.
 *   - Log / event / frame callbacks are invoked from the SDK's internal Qt
 *     event-loop thread (or from the host application's Qt main thread when the
 *     host already owns a QCoreApplication).  No SDK lock is held while a
 *     callback runs, so it IS safe to call spwcam_* functions from inside any
 *     callback.  Keep callbacks short: while a callback runs, no frames are
 *     pumped and no events are dispatched.
 *   - Multiple contexts per process are supported; the internal Qt thread is
 *     reference counted and shared.
 */

#ifndef SPWCAM_H
#define SPWCAM_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ export */
#ifdef _WIN32
#  ifdef SPWCAM_BUILDING_DLL
#    define SPWCAM_API  __declspec(dllexport)
#  else
#    define SPWCAM_API  __declspec(dllimport)
#  endif
/* SPWCAM_CB: calling convention for user-supplied callbacks.
 * On x64 Windows there is only one calling convention, so this is empty.  */
#  define SPWCAM_CB     /* empty */
#else
#  define SPWCAM_API
#  define SPWCAM_CB
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================ version == */
#define SPWCAM_VERSION_MAJOR  2
#define SPWCAM_VERSION_MINOR  0
#define SPWCAM_VERSION_PATCH  0
#define SPWCAM_VERSION_STR    "2.0.0"

/* ================================================================ error codes */
typedef enum spwcam_error {
    SPWCAM_OK                   =   0,
    SPWCAM_ERR_INVALID_PARAM    =  -1,
    SPWCAM_ERR_NOT_INITIALIZED  =  -2,
    SPWCAM_ERR_ALREADY_OPEN     =  -3,
    SPWCAM_ERR_NOT_OPEN         =  -4,
    SPWCAM_ERR_DEVICE_NOT_FOUND =  -5,
    SPWCAM_ERR_TIMEOUT          =  -6,
    SPWCAM_ERR_IO               =  -7,
    SPWCAM_ERR_ENCODER          =  -8,
    SPWCAM_ERR_NO_FRAME         =  -9,
    SPWCAM_ERR_OUT_OF_MEMORY    = -10,
    SPWCAM_ERR_INTERNAL         = -99
} spwcam_error_t;

/* ================================================================ handles */
typedef void* spwcam_context_t;   /* opaque SDK context */

/* ================================================================ enumerations */
typedef enum spwcam_pixel_format {
    SPWCAM_PIXEL_BGRA32 = 0,      /* 4 bytes/pixel, B G R A -- the only format delivered in v2.x */
    SPWCAM_PIXEL_RGB24  = 1       /* reserved for future use; never delivered in v2.x */
} spwcam_pixel_format_t;

typedef enum spwcam_stream_status {
    SPWCAM_STREAM_STOPPED    = 0, /* stream_open() not called / stream_close() called */
    SPWCAM_STREAM_CONNECTING = 1, /* stream_open() called, no frame received yet (SDK keeps retrying) */
    SPWCAM_STREAM_RUNNING    = 2, /* frames are arriving */
    SPWCAM_STREAM_ERROR      = 3  /* was RUNNING, no frame for 5 s; SDK is reconnecting.
                                     Returns to RUNNING automatically when frames resume. */
} spwcam_stream_status_t;

typedef enum spwcam_trigger_mode {
    SPWCAM_TRIGGER_SOFTWARE = 0,
    SPWCAM_TRIGGER_HARDWARE = 1
} spwcam_trigger_mode_t;

typedef enum spwcam_log_level {
    SPWCAM_LOG_DEBUG = 0,
    SPWCAM_LOG_INFO  = 1,
    SPWCAM_LOG_WARN  = 2,
    SPWCAM_LOG_ERROR = 3
} spwcam_log_level_t;

typedef enum spwcam_event_type {
    SPWCAM_EVENT_DEVICE_DISCOVERED  = 1,  /* data = SN; fired once when a device comes online */
    SPWCAM_EVENT_DEVICE_LOST        = 2,  /* data = SN; no heartbeat for 10 s */
    SPWCAM_EVENT_STREAM_CONNECTED   = 3,  /* data = NULL; first frame after open / after loss */
    SPWCAM_EVENT_STREAM_LOST        = 4,  /* data = NULL; no frame for 5 s while running */
    SPWCAM_EVENT_RECORD_STARTED     = 5,  /* data = file path of the first segment */
    SPWCAM_EVENT_RECORD_STOPPED     = 6,  /* data = file path of the last segment (file closed) */
    SPWCAM_EVENT_RECORD_FAILED      = 7,  /* data = reason */
    SPWCAM_EVENT_RECORD_SEGMENT     = 8,  /* data = new segment file path (automatic 30-minute split) */
    SPWCAM_EVENT_SNAPSHOT_SAVED     = 9,  /* data = saved file path */
    SPWCAM_EVENT_TRIGGER_STATUS     = 10, /* data = "software" | "hardware" | "fallback" */
    SPWCAM_EVENT_IP_CHANGED         = 11  /* data = "ok:<new_ip>" | "failed:<reason>"
                                             reason: no_ack | device_rejected | reconnect_timeout */
} spwcam_event_type_t;

typedef enum spwcam_container {
    SPWCAM_CONTAINER_MP4 = 0,
    SPWCAM_CONTAINER_AVI = 1      /* not supported in v2.x: MP4 is used and a warning is logged */
} spwcam_container_t;

typedef enum spwcam_image_format {
    SPWCAM_IMAGE_PNG = 0,
    SPWCAM_IMAGE_JPG = 1,
    SPWCAM_IMAGE_BMP = 2
} spwcam_image_format_t;

/* ================================================================ structs */

/**
 * Initialisation parameters -- zero-initialise, set struct_size, then fill fields.
 */
typedef struct spwcam_init_params {
    uint32_t struct_size;         /* must be sizeof(spwcam_init_params_t) */
    uint16_t discover_port;       /* UDP discovery listen port (default 7776) */
    uint16_t heartbeat_port;      /* UDP heartbeat port        (default 8888) */
    uint16_t cmd_port;            /* UDP command send port     (default 7777) */
    spwcam_log_level_t log_level; /* minimum log level emitted to callback */
} spwcam_init_params_t;

/**
 * Device information snapshot.
 */
typedef struct spwcam_device_info {
    uint32_t struct_size;         /* must be sizeof(spwcam_device_info_t) */
    char     sn[64];              /* serial number, NUL-terminated UTF-8 */
    char     ip[48];              /* dotted-decimal IP, NUL-terminated */
    uint16_t rtsp_port;           /* RTSP server port (typically 8554) */
    char     rtsp_path[128];      /* e.g. "/YSTech-TURBIDCAM-0001" */
    int64_t  last_seen_ms;        /* epoch milliseconds of last heartbeat */
} spwcam_device_info_t;

/**
 * Image frame -- always owned by the SDK; call spwcam_frame_free() when done.
 */
typedef struct spwcam_frame {
    uint32_t              struct_size;
    uint8_t*              data;          /* pixel data, SDK-allocated */
    uint32_t              width;
    uint32_t              height;
    uint32_t              stride;        /* bytes per row */
    spwcam_pixel_format_t pixel_format;
    int64_t               timestamp_ms; /* wall-clock ms at capture */
    uint64_t              sequence;     /* monotonically-increasing frame counter */
} spwcam_frame_t;

/**
 * Current recording status.
 */
typedef struct spwcam_record_status {
    uint32_t struct_size;
    int      recording;              /* non-zero if currently recording */
    char     current_file[512];      /* active file path, empty when stopped */
    int      segment_index;          /* 0-based current segment index */
    int64_t  segment_elapsed_ms;     /* elapsed ms of current segment */
    int64_t  total_elapsed_ms;       /* total elapsed ms of entire recording session */
} spwcam_record_status_t;

/**
 * Recording / snapshot configuration.
 * Fill once before calling spwcam_record_set_options(); may be changed between
 * sessions but not while recording is active.
 *
 * v2.x limitations: the encoder runs at a fixed 25 fps / 8000 kbps H.264 with
 * automatic 30-minute segments.  fps, bitrate_kbps and segment_minutes are
 * accepted for forward compatibility; non-default values are ignored and a
 * warning is logged.  Files are written into <dir>/<YYYY-MM-DD>/<timestamp>.
 */
typedef struct spwcam_record_options {
    uint32_t struct_size;
    char     video_dir[512];         /* output directory for video segments */
    char     snapshot_dir[512];      /* output directory for snapshots */
    int      fps;                    /* reserved (fixed 25) */
    int      bitrate_kbps;           /* reserved (fixed 8000) */
    int      segment_minutes;        /* reserved (fixed 30) */
    spwcam_container_t    container; /* MP4 (AVI not supported) */
    spwcam_image_format_t snapshot_fmt; /* PNG / JPG / BMP */
} spwcam_record_options_t;

/* ================================================================ callbacks */

/**
 * Log callback -- called from the SDK's Qt thread.  No SDK lock is held during
 * the call; calling spwcam_* functions from inside the callback is allowed.
 *
 * @param level      log level
 * @param message    NUL-terminated UTF-8 message (valid only during the call)
 * @param user_data  value supplied to spwcam_set_log_callback()
 */
typedef void (SPWCAM_CB *spwcam_log_callback_t)(
    spwcam_log_level_t level,
    const char*        message,
    void*              user_data);

/**
 * Frame callback -- called from the SDK's Qt thread every time a new frame
 * arrives (about 25 fps).  The frame pointer is valid only during the call;
 * copy the pixel data if you need to retain it.  Frames that arrive while the
 * callback is still running are dropped (latest-frame semantics).
 *
 * @param frame      pointer to frame descriptor (read-only, valid during call)
 * @param user_data  value supplied to spwcam_set_frame_callback()
 */
typedef void (SPWCAM_CB *spwcam_frame_callback_t)(
    const spwcam_frame_t* frame,
    void*                 user_data);

/**
 * Event callback -- called from the SDK's Qt thread for asynchronous events.
 * Calling spwcam_* functions from inside the callback is allowed.
 *
 * @param event      event type
 * @param data       optional NUL-terminated event payload (may be NULL)
 * @param user_data  value supplied to spwcam_set_event_callback()
 */
typedef void (SPWCAM_CB *spwcam_event_callback_t)(
    spwcam_event_type_t event,
    const char*         data,
    void*               user_data);

/* ================================================================ API */

/* ---- version / utilities ---- */

/** Returns static version string "MAJOR.MINOR.PATCH", never NULL. */
SPWCAM_API const char* spwcam_version(void);

/** Returns human-readable description for an error code, never NULL. */
SPWCAM_API const char* spwcam_get_error_string(int error_code);

/* ---- lifecycle ---- */

/**
 * Create an SDK context.
 *
 * Starts an internal Qt event-loop thread (or re-uses an existing
 * QCoreApplication in the same process) and initialises GStreamer.
 * UDP discovery does NOT start until spwcam_discovery_start() is called.
 *
 * @param params  init parameters; may be NULL for all-defaults.
 * @return        opaque context handle, or NULL on failure.
 *                Call spwcam_get_last_error() for the error code.
 */
SPWCAM_API spwcam_context_t spwcam_init(const spwcam_init_params_t* params);

/**
 * Destroy an SDK context and release all resources.
 * Stops the stream, stops recording, and shuts down the internal Qt loop if
 * this context created it.  Safe to call with ctx == NULL.
 */
SPWCAM_API void spwcam_deinit(spwcam_context_t ctx);

/**
 * Retrieve the last error code stored in this context.
 * Each context has its own last-error slot; thread-safe.
 */
SPWCAM_API int spwcam_get_last_error(spwcam_context_t ctx);

/* ---- logging ---- */

/** Install a log callback.  Pass cb=NULL to remove.  user_data may be NULL. */
SPWCAM_API void spwcam_set_log_callback(
    spwcam_context_t     ctx,
    spwcam_log_callback_t cb,
    void*                user_data);

/** Set the minimum log level forwarded to the callback. */
SPWCAM_API void spwcam_set_log_level(
    spwcam_context_t  ctx,
    spwcam_log_level_t level);

/* ---- events ---- */

/** Install an asynchronous event callback.  Pass cb=NULL to remove. */
SPWCAM_API void spwcam_set_event_callback(
    spwcam_context_t      ctx,
    spwcam_event_callback_t cb,
    void*                 user_data);

/* ---- device discovery ---- */

/**
 * Start UDP device discovery.
 *
 * @param ctx            SDK context
 * @param discover_port  UDP port to listen for device announcements (0 = use default from init)
 * @param heartbeat_port UDP heartbeat port (0 = use default from init)
 * @return SPWCAM_OK, or SPWCAM_ERR_IO if the UDP ports could not be bound
 *         (typically because another process -- e.g. the desktop application --
 *         already owns them).
 */
SPWCAM_API int spwcam_discovery_start(
    spwcam_context_t ctx,
    uint16_t         discover_port,
    uint16_t         heartbeat_port);

/** Stop UDP device discovery. */
SPWCAM_API void spwcam_discovery_stop(spwcam_context_t ctx);

/** Returns the number of currently known devices. */
SPWCAM_API int spwcam_get_device_count(spwcam_context_t ctx);

/**
 * Fill *info with data about the device at position index (0-based) in the
 * internal device list.
 *
 * info->struct_size must be set by the caller before the call.
 * Returns SPWCAM_ERR_INVALID_PARAM if index is out of range.
 */
SPWCAM_API int spwcam_get_device_info(
    spwcam_context_t   ctx,
    int                index,
    spwcam_device_info_t* info);

/**
 * Fill *info for the device identified by serial-number sn.
 * Returns SPWCAM_ERR_DEVICE_NOT_FOUND if not known.
 */
SPWCAM_API int spwcam_get_device_info_by_sn(
    spwcam_context_t      ctx,
    const char*           sn,
    spwcam_device_info_t* info);

/* ---- device control ---- */

/** Enable or disable the device's LED ring.  enable=0 off, non-zero on. */
SPWCAM_API int spwcam_set_led(
    spwcam_context_t ctx,
    const char*      sn,
    int              enable);

/** Switch hardware / software trigger mode. */
SPWCAM_API int spwcam_set_trigger_mode(
    spwcam_context_t      ctx,
    const char*           sn,
    spwcam_trigger_mode_t mode);

/**
 * Set camera exposure and gain simultaneously.
 *
 * @param exposure_us  exposure time in microseconds [10000 .. 30000]
 * @param gain_db      analogue gain in dB [0.0 .. 15.0]
 */
SPWCAM_API int spwcam_set_camera_params(
    spwcam_context_t ctx,
    const char*      sn,
    int              exposure_us,
    double           gain_db);

/**
 * Change the device's IP address.
 *
 * Result is asynchronous; the SPWCAM_EVENT_IP_CHANGED event is fired when
 * the device reconnects or the attempt times out.
 *
 * @param new_ip  new dotted-decimal IPv4 address
 * @param mask    subnet mask prefix length (e.g. 24 for /24)
 */
SPWCAM_API int spwcam_set_ip(
    spwcam_context_t ctx,
    const char*      sn,
    const char*      new_ip,
    int              mask);

/* ---- RTSP stream ---- */

/**
 * Open the RTSP stream.
 *
 * Returns immediately; connection happens asynchronously.  Poll
 * spwcam_stream_get_status() or wait for SPWCAM_EVENT_STREAM_CONNECTED.
 * The SDK retries the connection indefinitely until spwcam_stream_close().
 * Calling this while a stream is open closes the old stream first.
 *
 * @param rtsp_url   full RTSP URL, e.g. "rtsp://192.168.1.100:8554/cam"
 * @param latency_ms jitter-buffer latency in ms, clamped to [300, 600];
 *                   <=0 = SDK default (350 ms)
 * @return SPWCAM_OK or error
 */
SPWCAM_API int spwcam_stream_open(
    spwcam_context_t ctx,
    const char*      rtsp_url,
    int              latency_ms);

/** Stop the RTSP stream.  Safe to call when already stopped. */
SPWCAM_API int spwcam_stream_close(spwcam_context_t ctx);

/** Returns current stream status (does not require the stream to be open). */
SPWCAM_API spwcam_stream_status_t spwcam_stream_get_status(spwcam_context_t ctx);

/* ---- frame access ---- */

/**
 * Install a per-frame callback (push mode).
 *
 * Called from the SDK's Qt thread at ~25 fps when frames arrive.
 * The frame pointer is only valid during the callback.
 * Pass cb=NULL to uninstall.
 */
SPWCAM_API void spwcam_set_frame_callback(
    spwcam_context_t      ctx,
    spwcam_frame_callback_t cb,
    void*                  user_data);

/**
 * Grab the latest available frame (pull mode).
 *
 * On success, *out_frame is set to a newly-allocated spwcam_frame_t.
 * The caller MUST call spwcam_frame_free(*out_frame) when done.
 * Returns SPWCAM_ERR_NO_FRAME if no new frame has arrived since the last call.
 *
 * @param out_frame  receives pointer to frame on success; set to NULL on error
 * @return SPWCAM_OK or error code
 */
SPWCAM_API int spwcam_frame_grab(
    spwcam_context_t  ctx,
    spwcam_frame_t**  out_frame);

/** Release a frame obtained from spwcam_frame_grab().  Safe to call with NULL. */
SPWCAM_API void spwcam_frame_free(spwcam_frame_t* frame);

/* ---- recording ---- */

/**
 * Configure recording / snapshot paths and options.
 *
 * Must be called before spwcam_record_start() if non-default paths are wanted.
 * opts->struct_size must be set by the caller.
 */
SPWCAM_API int spwcam_record_set_options(
    spwcam_context_t            ctx,
    const spwcam_record_options_t* opts);

/**
 * Start recording.  Stream status must be RUNNING, otherwise
 * SPWCAM_ERR_NOT_OPEN is returned.
 *
 * Asynchronous: the encoder is opened on the next frame; then
 * SPWCAM_EVENT_RECORD_STARTED fires with the file path and
 * spwcam_record_get_status() reports recording != 0.  On failure
 * SPWCAM_EVENT_RECORD_FAILED fires.
 */
SPWCAM_API int spwcam_record_start(spwcam_context_t ctx);

/**
 * Stop recording.
 * Asynchronous: SPWCAM_EVENT_RECORD_STOPPED fires when the file is flushed
 * and closed.  spwcam_deinit() stops and finalises an active recording
 * synchronously.
 */
SPWCAM_API int spwcam_record_stop(spwcam_context_t ctx);

/**
 * Query recording status.
 * status->struct_size must be set by the caller.
 * segment_index counts automatic 30-minute splits within the current session
 * (0 for the first file).
 */
SPWCAM_API int spwcam_record_get_status(
    spwcam_context_t       ctx,
    spwcam_record_status_t* status);

/**
 * Capture a single snapshot from the most recent frame.
 *
 * The file is written asynchronously; SPWCAM_EVENT_SNAPSHOT_SAVED fires on
 * completion.  Returns SPWCAM_ERR_NOT_OPEN if the stream is not RUNNING and
 * SPWCAM_ERR_NO_FRAME if no frame has been received yet.
 */
SPWCAM_API int spwcam_snapshot(spwcam_context_t ctx);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SPWCAM_H */
