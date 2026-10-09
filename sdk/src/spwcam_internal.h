/**
 * spwcam_internal.h  --  SDK-internal shared types
 *
 * NOT part of the public API.  Never include this from outside sdk/src/.
 */
#pragma once

#include "spwcam/spwcam.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ fwd */
class SdkQtWorker;

/* -------------------------------------------------------- callback state */
struct SdkCallbacks {
    std::mutex              lock;
    spwcam_log_callback_t   log_cb    = nullptr;
    void*                   log_ud    = nullptr;
    spwcam_log_level_t      log_level = SPWCAM_LOG_INFO;
    spwcam_event_callback_t event_cb  = nullptr;
    void*                   event_ud  = nullptr;
    spwcam_frame_callback_t frame_cb  = nullptr;
    void*                   frame_ud  = nullptr;
};

/* ------------------------------------------------------- recording state */
struct SdkRecordState {
    std::mutex  lock;
    bool        recording       = false;   /* encoder actually open (reported) */
    std::string current_file;
    int         segment_index   = 0;
    int64_t     segment_start_ms = 0;  /* wall-clock ms when segment started  */
    int64_t     session_start_ms = 0;  /* wall-clock ms when record_start()   */
};

/* -------------------------------------------------- pull-mode frame slot */
struct SdkGrabFrame {
    std::mutex           lock;
    bool                 fresh  = false;
    std::vector<uint8_t> data;           /* BGRA pixels                        */
    uint32_t             width  = 0;
    uint32_t             height = 0;
    uint32_t             stride = 0;
    int64_t              ts_ms  = 0;
    uint64_t             seq    = 0;
};

/* ---------------------------------------------------------- main context */
struct SdkContext {
    /* Qt event-loop thread */
    std::atomic<bool>   qt_ready{false};
    SdkQtWorker*        worker = nullptr;   /* QObject living on Qt thread      */

    /* per-context state */
    SdkCallbacks        cbs;
    SdkRecordState      rec;
    SdkGrabFrame        grab;

    std::atomic<int>    stream_status{SPWCAM_STREAM_STOPPED};
    std::atomic<int>    last_error{SPWCAM_OK};

    /* defaults from spwcam_init() */
    uint16_t            init_discover_port  = 7776;
    uint16_t            init_heartbeat_port = 8888;
    uint16_t            init_cmd_port       = 7777;
};

/* --------------------------------------------------------------- helpers */

/*
 * IMPORTANT: user callbacks are invoked with cbs.lock RELEASED.
 * The lock only protects the copy of (callback, user_data).  This allows the
 * user to call any spwcam_* API from inside a callback without deadlocking.
 */
inline void sdk_log(SdkContext* ctx, spwcam_log_level_t lvl, const char* msg)
{
    if (!ctx) return;
    spwcam_log_callback_t cb = nullptr;
    void*                 ud = nullptr;
    {
        std::lock_guard<std::mutex> lk(ctx->cbs.lock);
        if (ctx->cbs.log_cb && lvl >= ctx->cbs.log_level) {
            cb = ctx->cbs.log_cb;
            ud = ctx->cbs.log_ud;
        }
    }
    if (cb) cb(lvl, msg, ud);
}

inline void sdk_log(SdkContext* ctx, spwcam_log_level_t lvl,
                    const std::string& msg)
{
    sdk_log(ctx, lvl, msg.c_str());
}

inline void sdk_event(SdkContext* ctx, spwcam_event_type_t ev,
                      const char* data = nullptr)
{
    if (!ctx) return;
    spwcam_event_callback_t cb = nullptr;
    void*                   ud = nullptr;
    {
        std::lock_guard<std::mutex> lk(ctx->cbs.lock);
        cb = ctx->cbs.event_cb;
        ud = ctx->cbs.event_ud;
    }
    if (cb) cb(ev, data, ud);
}

inline void sdk_set_error(SdkContext* ctx, int code)
{
    if (ctx) ctx->last_error.store(code, std::memory_order_relaxed);
}

/** Milliseconds since Unix epoch (wall-clock). */
inline int64_t sdk_now_ms()
{
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

/** Safe strncpy into a fixed-size char array. */
template<size_t N>
static inline void sdk_strcpy(char (&dst)[N], const char* src)
{
    if (!src) { dst[0] = '\0'; return; }
    size_t n = std::strlen(src);
    if (n >= N) n = N - 1;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}
