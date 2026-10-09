/**
 * test_init.cpp  --  C++ integration test: init / deinit / basic API calls
 *
 * Does NOT require a physical camera.
 * Exit code 0 = pass.
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#include "spwcam/spwcam.h"
#include "spwcam/spwcam.hpp"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { std::cout << "  PASS  " name "\n"; ++g_pass; } \
    else       { std::cout << "  FAIL  " name "\n"; ++g_fail; } \
} while(0)

/* ---- C API: init / deinit ------------------------------------------------ */
static void test_c_init()
{
    std::cout << "\n[C API: init/deinit]\n";

    spwcam_init_params_t p{};
    p.struct_size = static_cast<uint32_t>(sizeof(p));
    p.log_level   = SPWCAM_LOG_ERROR;  /* silence during test */

    spwcam_context_t ctx = spwcam_init(&p);
    CHECK(ctx != nullptr, "spwcam_init() returns non-NULL");

    if (ctx) {
        CHECK(spwcam_get_last_error(ctx) == SPWCAM_OK,
              "last_error == OK after init");

        /* version */
        const char* v = spwcam_version();
        CHECK(v && std::strlen(v) > 0, "version non-empty");

        /* stream status without opening */
        auto st = spwcam_stream_get_status(ctx);
        CHECK(st == SPWCAM_STREAM_STOPPED, "initial stream status == STOPPED");

        /* frame_grab without stream returns NO_FRAME */
        spwcam_frame_t* fr = nullptr;
        int rc = spwcam_frame_grab(ctx, &fr);
        CHECK(rc == SPWCAM_ERR_NO_FRAME, "frame_grab without stream → NO_FRAME");
        CHECK(fr == nullptr, "out_frame is NULL on NO_FRAME");

        /* record_start without stream returns NOT_OPEN */
        rc = spwcam_record_start(ctx);
        CHECK(rc == SPWCAM_ERR_NOT_OPEN, "record_start without stream → NOT_OPEN");

        spwcam_deinit(ctx);
        g_pass++;
        std::cout << "  PASS  spwcam_deinit() completed\n";
    }
}

/* ---- C API: default init (NULL params) ----------------------------------- */
static void test_c_init_null_params()
{
    std::cout << "\n[C API: init with NULL params]\n";
    spwcam_context_t ctx = spwcam_init(nullptr);
    CHECK(ctx != nullptr, "spwcam_init(NULL) returns non-NULL");
    if (ctx) { spwcam_deinit(ctx); g_pass++; }
}

/* ---- C API: record options ----------------------------------------------- */
static void test_record_options()
{
    std::cout << "\n[C API: record_set_options]\n";

    spwcam_context_t ctx = spwcam_init(nullptr);
    if (!ctx) { g_fail++; return; }

    spwcam_record_options_t opts{};
    opts.struct_size     = static_cast<uint32_t>(sizeof(opts));
    opts.fps             = 25;
    opts.bitrate_kbps    = 8000;
    opts.segment_minutes = 30;
    opts.container       = SPWCAM_CONTAINER_MP4;
    opts.snapshot_fmt    = SPWCAM_IMAGE_PNG;

    std::strncpy(opts.video_dir,    "D:/test_vid",  sizeof(opts.video_dir) - 1);
    std::strncpy(opts.snapshot_dir, "D:/test_snap", sizeof(opts.snapshot_dir) - 1);

    int rc = spwcam_record_set_options(ctx, &opts);
    CHECK(rc == SPWCAM_OK, "record_set_options → OK");

    /* query status */
    spwcam_record_status_t st{};
    st.struct_size = static_cast<uint32_t>(sizeof(st));
    rc = spwcam_record_get_status(ctx, &st);
    CHECK(rc == SPWCAM_OK,       "record_get_status → OK");
    CHECK(st.recording == 0,     "not recording initially");
    CHECK(st.segment_index == 0, "segment_index == 0 initially");

    spwcam_deinit(ctx);
    g_pass++;
}

/* ---- C++ RAII wrapper ---------------------------------------------------- */
static void test_cpp_raii()
{
    std::cout << "\n[C++ RAII wrapper]\n";

    try {
        spwcam::Camera cam;
        cam.init(0, 0, 0, SPWCAM_LOG_ERROR);
        CHECK(cam.initialised(), "cam.initialised()");
        CHECK(!cam.stream_running(), "stream not running initially");
        CHECK(!cam.recording(),      "not recording initially");

        std::string ver = spwcam::Camera::version();
        CHECK(!ver.empty(), "version() non-empty");

        /* set options via convenience overload */
        cam.record_set_options("D:/test_vid", "D:/test_snap");
        CHECK(true, "record_set_options overload compiles and runs");

        /* frame_grab returns empty Frame when no stream */
        {
            spwcam::Frame f = cam.frame_grab();
            CHECK(!f, "frame_grab without stream → empty Frame");
        }

        cam.deinit();
        CHECK(!cam.initialised(), "cam.initialised() == false after deinit");

    } catch (const spwcam::Error& e) {
        std::cerr << "  UNEXPECTED spwcam::Error: " << e.what() << "\n";
        ++g_fail;
    }
}

/* ---- C++ RAII: move semantics -------------------------------------------- */
static int g_move_log_hits = 0;

static spwcam::Camera make_camera()
{
    spwcam::Camera c;
    c.init(0, 0, 0, SPWCAM_LOG_INFO);
    c.on_log([](spwcam_log_level_t, std::string) { ++g_move_log_hits; });
    return std::move(c);   /* force a move (factory / container pattern) */
}

static void test_cpp_move()
{
    std::cout << "\n[C++ RAII: move semantics]\n";

    spwcam::Camera a;
    a.init(0, 0, 0, SPWCAM_LOG_ERROR);
    CHECK(a.initialised(), "a initialised before move");

    spwcam::Camera b = std::move(a);
    CHECK(!a.initialised(), "a not initialised after move");
    CHECK(b.initialised(),  "b initialised after move");

    b.deinit();
    CHECK(!b.initialised(), "b deinited");

    /* callbacks must survive the move of a *destroyed* source object */
    {
        spwcam::Camera cam = make_camera();          /* source destroyed here */
        g_move_log_hits = 0;
        cam.stream_open("rtsp://127.0.0.1:1/none", 0);   /* emits an INFO log line */
        cam.stream_close();                              /* emits another */
        CHECK(g_move_log_hits >= 1, "log handler installed before move still fires after move");
        cam.deinit();
    }
}

/* ---- two contexts in one process ---------------------------------------- */
static void test_two_contexts()
{
    std::cout << "\n[C API: two contexts in one process]\n";
    spwcam_context_t a = spwcam_init(nullptr);
    spwcam_context_t b = spwcam_init(nullptr);
    CHECK(a && b, "two contexts created");

    spwcam_deinit(a);                                   /* must not tear down b's Qt thread */
    int n = spwcam_get_device_count(b);
    CHECK(n == 0, "second context still functional after first deinit");
    int rc = spwcam_stream_close(b);
    CHECK(rc == SPWCAM_OK, "second context API still returns OK");
    spwcam_deinit(b);

    spwcam_context_t c = spwcam_init(nullptr);          /* Qt thread re-created */
    CHECK(c != nullptr, "init works again after all contexts released");
    spwcam_deinit(c);
}

/* ---- discovery: bind failure is reported --------------------------------- */
static void test_discovery_port_conflict()
{
    std::cout << "\n[C API: discovery port conflict]\n";
    spwcam_context_t a = spwcam_init(nullptr);
    spwcam_context_t b = spwcam_init(nullptr);
    int ra = spwcam_discovery_start(a, 47776, 48888);
    int rb = spwcam_discovery_start(b, 47776, 48888);   /* same ports -> must fail */
    CHECK(ra == SPWCAM_OK,     "first discovery_start OK");
    CHECK(rb == SPWCAM_ERR_IO, "second discovery_start on same ports -> ERR_IO");
    CHECK(spwcam_get_last_error(b) == SPWCAM_ERR_IO, "last_error == ERR_IO");
    spwcam_deinit(b);
    spwcam_deinit(a);
}

/* ---- callback re-entrancy (no stream needed: log callback) --------------- */
static int g_reentry_rc = 12345;
static void reentrant_log_cb(spwcam_log_level_t, const char*, void* ud)
{
    /* calling back into the SDK from inside a callback must be allowed */
    spwcam_context_t ctx = static_cast<spwcam_context_t>(ud);
    spwcam_set_log_level(ctx, SPWCAM_LOG_INFO);          /* takes cbs.lock */
    g_reentry_rc = spwcam_stream_get_status(ctx);
}

static void test_callback_reentrancy()
{
    std::cout << "\n[C API: SDK call from inside a callback]\n";
    spwcam_init_params_t p{}; p.struct_size = sizeof(p); p.log_level = SPWCAM_LOG_INFO;
    spwcam_context_t ctx = spwcam_init(&p);
    spwcam_set_log_callback(ctx, reentrant_log_cb, ctx);
    spwcam_stream_open(ctx, "rtsp://127.0.0.1:1/none", 0);   /* logs -> callback -> re-enters */
    CHECK(g_reentry_rc == SPWCAM_STREAM_CONNECTING, "re-entrant call from log callback returned correct value");
    spwcam_set_log_callback(ctx, nullptr, nullptr);
    spwcam_deinit(ctx);
}

/* ---- DeviceInfo helpers -------------------------------------------------- */
static void test_device_info_helpers()
{
    std::cout << "\n[DeviceInfo / Frame helpers]\n";

    spwcam_device_info_t raw{};
    raw.struct_size = static_cast<uint32_t>(sizeof(raw));
    std::strncpy(raw.sn,        "CAM-001",     sizeof(raw.sn) - 1);
    std::strncpy(raw.ip,        "192.168.1.10",sizeof(raw.ip) - 1);
    std::strncpy(raw.rtsp_path, "/test",        sizeof(raw.rtsp_path) - 1);
    raw.rtsp_port = 8554;

    auto d = spwcam::DeviceInfo::from_c(raw);
    CHECK(d.sn        == "CAM-001",      "DeviceInfo::sn");
    CHECK(d.ip        == "192.168.1.10", "DeviceInfo::ip");
    CHECK(d.rtsp_port == 8554,           "DeviceInfo::rtsp_port");
    CHECK(d.rtsp_url() == "rtsp://192.168.1.10:8554/test", "DeviceInfo::rtsp_url()");
}

/* ---- main ---------------------------------------------------------------- */
int main()
{
    std::cout << "===== spwcam init integration tests =====\n";

    test_c_init();
    test_c_init_null_params();
    test_record_options();
    test_cpp_raii();
    test_cpp_move();
    test_device_info_helpers();
    test_two_contexts();
    test_discovery_port_conflict();
    test_callback_reentrancy();

    std::cout << "\n=========================================\n";
    std::cout << "Passed: " << g_pass << "  Failed: " << g_fail << "\n";
    return (g_fail == 0) ? 0 : 1;
}
