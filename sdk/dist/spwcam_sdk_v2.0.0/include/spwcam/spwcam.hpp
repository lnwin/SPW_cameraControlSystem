/**
 * spwcam.hpp  --  SPWater Camera SDK  C++11 RAII Wrapper (header-only)
 *
 * Author   : Yuanshi Technology
 * Copyright (C) Zhoushan Yuanshi Technology Co., Ltd.  All rights reserved.
 *
 * Unauthorized copying, modification, distribution or use of this file,
 * via any medium, is strictly prohibited without prior written permission.
 *
 * Zero external dependencies beyond spwcam.h and the SDK DLL.
 * Usage example:
 *
 *   spwcam::Camera cam;
 *   cam.init();
 *   cam.on_frame([](const spwcam_frame_t& f){ ... });
 *   cam.discovery_start();
 *   if (auto dev = cam.wait_for_device(10.0)) {
 *       cam.stream_open(dev->rtsp_url());
 *       cam.wait_for_stream(15.0);
 *   }
 *
 * Callback handlers (on_log / on_event / on_frame) are invoked on the SDK's
 * Qt thread.  Calling Camera methods from inside a handler is allowed.
 * Camera is movable; callbacks follow the object.
 */

#pragma once

#include "spwcam.h"

#include <stdexcept>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstring>   // memset
#include <cstdint>

namespace spwcam {

/* ============================================================ exception */

class Error : public std::runtime_error {
public:
    explicit Error(int code)
        : std::runtime_error(spwcam_get_error_string(code))
        , code_(code)
    {}

    int code() const noexcept { return code_; }

private:
    int code_;
};

inline void check(int rc) {
    if (rc != SPWCAM_OK) throw Error(rc);
}

/* ============================================================ DeviceInfo */

struct DeviceInfo {
    std::string sn;
    std::string ip;
    uint16_t    rtsp_port   = 0;
    std::string rtsp_path;
    int64_t     last_seen_ms = 0;

    /** Build RTSP URL for use with Camera::stream_open(). */
    std::string rtsp_url() const {
        return std::string("rtsp://") + ip + ":" +
               std::to_string(rtsp_port) + rtsp_path;
    }

    static DeviceInfo from_c(const spwcam_device_info_t& d) {
        DeviceInfo out;
        out.sn           = d.sn;
        out.ip           = d.ip;
        out.rtsp_port    = d.rtsp_port;
        out.rtsp_path    = d.rtsp_path;
        out.last_seen_ms = d.last_seen_ms;
        return out;
    }
};

/* ============================================================ Frame (RAII) */

class Frame {
public:
    Frame() noexcept = default;

    /** Takes ownership of a frame pointer returned by spwcam_frame_grab(). */
    explicit Frame(spwcam_frame_t* p) noexcept : p_(p) {}

    ~Frame() { reset(); }

    Frame(const Frame&)            = delete;
    Frame& operator=(const Frame&) = delete;

    Frame(Frame&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Frame& operator=(Frame&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }

    void reset() noexcept { if (p_) { spwcam_frame_free(p_); p_ = nullptr; } }

    explicit operator bool() const noexcept { return p_ != nullptr; }

    const spwcam_frame_t& get() const { return *p_; }

    uint32_t            width()      const noexcept { return p_ ? p_->width  : 0; }
    uint32_t            height()     const noexcept { return p_ ? p_->height : 0; }
    uint32_t            stride()     const noexcept { return p_ ? p_->stride : 0; }
    const uint8_t*      data()       const noexcept { return p_ ? p_->data   : nullptr; }
    spwcam_pixel_format_t format()   const noexcept { return p_ ? p_->pixel_format : SPWCAM_PIXEL_BGRA32; }
    int64_t             timestamp()  const noexcept { return p_ ? p_->timestamp_ms : 0; }
    uint64_t            sequence()   const noexcept { return p_ ? p_->sequence : 0; }

    /** Copy raw pixel bytes into a std::vector<uint8_t>. */
    std::vector<uint8_t> to_vector() const {
        if (!p_) return {};
        const size_t sz = static_cast<size_t>(p_->stride) * p_->height;
        return std::vector<uint8_t>(p_->data, p_->data + sz);
    }

private:
    spwcam_frame_t* p_ = nullptr;
};

/* ============================================================ RecordStatus */

struct RecordStatus {
    bool        recording          = false;
    std::string current_file;
    int         segment_index      = 0;
    int64_t     segment_elapsed_ms = 0;
    int64_t     total_elapsed_ms   = 0;

    static RecordStatus from_c(const spwcam_record_status_t& s) {
        RecordStatus out;
        out.recording          = s.recording != 0;
        out.current_file       = s.current_file;
        out.segment_index      = s.segment_index;
        out.segment_elapsed_ms = s.segment_elapsed_ms;
        out.total_elapsed_ms   = s.total_elapsed_ms;
        return out;
    }
};

/* ============================================================ Camera */

class Camera {
public:
    using LogCallback   = std::function<void(spwcam_log_level_t, std::string)>;
    using EventCallback = std::function<void(spwcam_event_type_t, std::string)>;
    using FrameCallback = std::function<void(const spwcam_frame_t&)>;

    Camera() : h_(new Handlers()) {}

    ~Camera() { deinit(); }

    Camera(const Camera&)            = delete;
    Camera& operator=(const Camera&) = delete;

    /* Handlers live on the heap and are registered with the DLL by address,
       so moving the Camera keeps every callback valid without re-binding.  */
    Camera(Camera&& o) noexcept : ctx_(o.ctx_), h_(std::move(o.h_)) { o.ctx_ = nullptr; }
    Camera& operator=(Camera&& o) noexcept {
        if (this != &o) { deinit(); ctx_ = o.ctx_; h_ = std::move(o.h_); o.ctx_ = nullptr; }
        return *this;
    }

    /* ------------ lifecycle ------------ */

    /**
     * Initialise the SDK context.
     *
     * @param discover_port  0 = default (7776)
     * @param heartbeat_port 0 = default (8888)
     * @param cmd_port       0 = default (7777)
     * @param log_level      minimum log level forwarded to on_log()
     */
    void init(uint16_t          discover_port  = 0,
              uint16_t          heartbeat_port = 0,
              uint16_t          cmd_port       = 0,
              spwcam_log_level_t log_level      = SPWCAM_LOG_INFO)
    {
        if (ctx_) throw Error(SPWCAM_ERR_ALREADY_OPEN);
        if (!h_)  h_.reset(new Handlers());

        spwcam_init_params_t p{};
        p.struct_size    = static_cast<uint32_t>(sizeof(p));
        p.discover_port  = discover_port;
        p.heartbeat_port = heartbeat_port;
        p.cmd_port       = cmd_port;
        p.log_level      = log_level;

        ctx_ = spwcam_init(&p);
        if (!ctx_) throw Error(SPWCAM_ERR_INTERNAL);

        spwcam_set_log_callback  (ctx_, &Camera::log_thunk,   h_.get());
        spwcam_set_event_callback(ctx_, &Camera::event_thunk, h_.get());
        spwcam_set_frame_callback(ctx_, &Camera::frame_thunk, h_.get());
    }

    /** Release SDK context.  Safe to call multiple times / on un-init'd instance. */
    void deinit() noexcept {
        if (ctx_) { spwcam_deinit(ctx_); ctx_ = nullptr; }
    }

    bool initialised() const noexcept { return ctx_ != nullptr; }

    /* ------------ callbacks (may be set before or after init) ------------ */

    void on_log  (LogCallback   cb) { std::lock_guard<std::mutex> lk(h_->m); h_->log   = std::move(cb); }
    void on_event(EventCallback cb) { std::lock_guard<std::mutex> lk(h_->m); h_->event = std::move(cb); }
    void on_frame(FrameCallback cb) { std::lock_guard<std::mutex> lk(h_->m); h_->frame = std::move(cb); }

    /* ------------ device discovery ------------ */

    void discovery_start(uint16_t discover_port = 0, uint16_t heartbeat_port = 0) {
        check(spwcam_discovery_start(ctx_, discover_port, heartbeat_port));
    }

    void discovery_stop() { spwcam_discovery_stop(ctx_); }

    int device_count() const { return spwcam_get_device_count(ctx_); }

    DeviceInfo device_info(int index) const {
        spwcam_device_info_t d{};
        d.struct_size = static_cast<uint32_t>(sizeof(d));
        check(spwcam_get_device_info(ctx_, index, &d));
        return DeviceInfo::from_c(d);
    }

    DeviceInfo device_info_by_sn(const std::string& sn) const {
        spwcam_device_info_t d{};
        d.struct_size = static_cast<uint32_t>(sizeof(d));
        check(spwcam_get_device_info_by_sn(ctx_, sn.c_str(), &d));
        return DeviceInfo::from_c(d);
    }

    std::vector<DeviceInfo> all_devices() const {
        int n = device_count();
        std::vector<DeviceInfo> out;
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) out.push_back(device_info(i));
        return out;
    }

    /**
     * Block until at least one device is known or the timeout expires.
     * Returns a pointer to the first device (owned by the returned unique_ptr),
     * or nullptr on timeout.
     */
    std::unique_ptr<DeviceInfo> wait_for_device(double timeout_s = 10.0,
                                                double poll_s    = 0.25) const
    {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::duration<double>(timeout_s);
        while (std::chrono::steady_clock::now() < deadline) {
            if (device_count() > 0)
                return std::unique_ptr<DeviceInfo>(new DeviceInfo(device_info(0)));
            std::this_thread::sleep_for(std::chrono::duration<double>(poll_s));
        }
        return nullptr;
    }

    /* ------------ device control ------------ */

    void set_led(const std::string& sn, bool enable) {
        check(spwcam_set_led(ctx_, sn.c_str(), enable ? 1 : 0));
    }

    void set_trigger_mode(const std::string& sn, spwcam_trigger_mode_t mode) {
        check(spwcam_set_trigger_mode(ctx_, sn.c_str(), mode));
    }

    /** Convenience overload: hardware = true -> HARDWARE, false -> SOFTWARE. */
    void set_trigger_mode(const std::string& sn, bool hardware) {
        set_trigger_mode(sn, hardware ? SPWCAM_TRIGGER_HARDWARE : SPWCAM_TRIGGER_SOFTWARE);
    }

    void set_camera_params(const std::string& sn, int exposure_us, double gain_db) {
        check(spwcam_set_camera_params(ctx_, sn.c_str(), exposure_us, gain_db));
    }

    void set_ip(const std::string& sn, const std::string& new_ip, int mask = 24) {
        check(spwcam_set_ip(ctx_, sn.c_str(), new_ip.c_str(), mask));
    }

    /* ------------ stream ------------ */

    void stream_open(const std::string& rtsp_url, int latency_ms = 0) {
        check(spwcam_stream_open(ctx_, rtsp_url.c_str(), latency_ms));
    }

    void stream_close() { check(spwcam_stream_close(ctx_)); }

    spwcam_stream_status_t stream_status() const {
        return spwcam_stream_get_status(ctx_);
    }

    bool stream_running() const {
        return stream_status() == SPWCAM_STREAM_RUNNING;
    }

    /** Block until the stream is RUNNING or the timeout expires.  */
    bool wait_for_stream(double timeout_s = 15.0, double poll_s = 0.1) const {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::duration<double>(timeout_s);
        while (std::chrono::steady_clock::now() < deadline) {
            if (stream_running()) return true;
            std::this_thread::sleep_for(std::chrono::duration<double>(poll_s));
        }
        return stream_running();
    }

    /* ------------ frame pull ------------ */

    /**
     * Grab the latest frame.  Returns an empty Frame (operator bool == false)
     * if no new frame is available.
     */
    Frame frame_grab() {
        spwcam_frame_t* p = nullptr;
        int rc = spwcam_frame_grab(ctx_, &p);
        if (rc == SPWCAM_ERR_NO_FRAME || !p) return Frame{};
        if (rc != SPWCAM_OK) throw Error(rc);
        return Frame{p};
    }

    /* ------------ recording ------------ */

    void record_set_options(const spwcam_record_options_t& opts) {
        check(spwcam_record_set_options(ctx_, &opts));
    }

    /** Convenience overload.  See spwcam_record_options_t for v2.x limits. */
    void record_set_options(const std::string& video_dir,
                             const std::string& snapshot_dir,
                             int  fps              = 25,
                             int  bitrate_kbps     = 8000,
                             int  segment_minutes  = 30,
                             spwcam_container_t    container     = SPWCAM_CONTAINER_MP4,
                             spwcam_image_format_t snapshot_fmt  = SPWCAM_IMAGE_PNG)
    {
        spwcam_record_options_t o{};
        o.struct_size     = static_cast<uint32_t>(sizeof(o));
        o.fps             = fps;
        o.bitrate_kbps    = bitrate_kbps;
        o.segment_minutes = segment_minutes;
        o.container       = container;
        o.snapshot_fmt    = snapshot_fmt;

        auto cp = [](char* dst, size_t dsz, const std::string& src){
            size_t n = src.size() < dsz - 1 ? src.size() : dsz - 1;
            std::memcpy(dst, src.c_str(), n);
            dst[n] = '\0';
        };
        cp(o.video_dir,    sizeof(o.video_dir),    video_dir);
        cp(o.snapshot_dir, sizeof(o.snapshot_dir), snapshot_dir);

        check(spwcam_record_set_options(ctx_, &o));
    }

    void record_start() { check(spwcam_record_start(ctx_)); }
    void record_stop()  { check(spwcam_record_stop(ctx_));  }

    RecordStatus record_status() const {
        spwcam_record_status_t s{};
        s.struct_size = static_cast<uint32_t>(sizeof(s));
        check(spwcam_record_get_status(ctx_, &s));
        return RecordStatus::from_c(s);
    }

    bool recording() const { return record_status().recording; }

    /** Throws Error(SPWCAM_ERR_NO_FRAME) if no frame has been received yet. */
    void snapshot() { check(spwcam_snapshot(ctx_)); }

    /* ------------ raw handle (escape hatch) ------------ */
    spwcam_context_t handle() const noexcept { return ctx_; }

    /** SDK version string, e.g. "2.0.0".  Static -- no context needed. */
    static std::string version() {
        const char* v = spwcam_version();
        return v ? std::string(v) : std::string();
    }

private:
    struct Handlers {
        std::mutex    m;
        LogCallback   log;
        EventCallback event;
        FrameCallback frame;
    };

    /* C thunks: copy the std::function under the lock, invoke it outside. */
    static void SPWCAM_CB log_thunk(spwcam_log_level_t lvl, const char* msg, void* ud) {
        auto* h = static_cast<Handlers*>(ud);
        LogCallback cb;
        { std::lock_guard<std::mutex> lk(h->m); cb = h->log; }
        if (cb) cb(lvl, msg ? std::string(msg) : std::string());
    }
    static void SPWCAM_CB event_thunk(spwcam_event_type_t ev, const char* data, void* ud) {
        auto* h = static_cast<Handlers*>(ud);
        EventCallback cb;
        { std::lock_guard<std::mutex> lk(h->m); cb = h->event; }
        if (cb) cb(ev, data ? std::string(data) : std::string());
    }
    static void SPWCAM_CB frame_thunk(const spwcam_frame_t* f, void* ud) {
        auto* h = static_cast<Handlers*>(ud);
        FrameCallback cb;
        { std::lock_guard<std::mutex> lk(h->m); cb = h->frame; }
        if (cb && f) cb(*f);
    }

    spwcam_context_t          ctx_ = nullptr;
    std::unique_ptr<Handlers> h_;
};

}  /* namespace spwcam */
