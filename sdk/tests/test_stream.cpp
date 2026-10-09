/**
 * test_stream.cpp  --  end-to-end integration test against a live RTSP source
 *
 * Requires an H.264 RTSP stream.  The URL is taken from the environment
 * variable SPWCAM_TEST_RTSP_URL; when it is not set the test prints SKIP and
 * exits 0 so that ctest stays green on machines without a stream.
 *
 * Verifies (all of these were broken in the original v1.0.0 package):
 *   1. stream reaches RUNNING and delivers ~25 fps 1920x1080 BGRA frames
 *   2. STREAM_CONNECTED event fires
 *   3. pull-mode grab returns a frame, immediate re-grab returns NO_FRAME
 *   4. recording actually starts: RECORD_STARTED event, status.recording == 1,
 *      MP4 file exists and grows, RECORD_STOPPED on stop
 *   5. snapshot: N calls -> N SNAPSHOT_SAVED events (no silent drops)
 *   6. calling SDK functions from inside an event callback does not crash
 *   7. spwcam_deinit() while recording finalises the file (size > 0)
 *
 * Exit code 0 = pass / skip, 1 = fail.
 */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  include <sys/stat.h>
#endif

#include "spwcam/spwcam.h"

static int g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) std::printf("  PASS  %s\n", name); \
    else { std::printf("  FAIL  %s\n", name); ++g_fail; } } while (0)

static std::atomic<int> g_frames{0}, g_connected{0}, g_rec_started{0}, g_rec_stopped{0},
                        g_rec_failed{0}, g_snap_saved{0}, g_reentry_ok{0};
static std::atomic<uint32_t> g_w{0}, g_h{0}, g_fmt{99};
static std::mutex g_m;
static std::string g_rec_path, g_last_snap;
static spwcam_context_t g_ctx = nullptr;

static void on_frame(const spwcam_frame_t* f, void*) {
    ++g_frames; g_w = f->width; g_h = f->height; g_fmt = (uint32_t)f->pixel_format;
}
static void on_event(spwcam_event_type_t ev, const char* data, void*) {
    std::lock_guard<std::mutex> lk(g_m);
    switch (ev) {
    case SPWCAM_EVENT_STREAM_CONNECTED: ++g_connected; break;
    case SPWCAM_EVENT_RECORD_STARTED:   ++g_rec_started; g_rec_path = data ? data : ""; break;
    case SPWCAM_EVENT_RECORD_STOPPED:   ++g_rec_stopped; break;
    case SPWCAM_EVENT_RECORD_FAILED:    ++g_rec_failed; std::printf("  [RECORD_FAILED] %s\n", data ? data : ""); break;
    case SPWCAM_EVENT_SNAPSHOT_SAVED:
        ++g_snap_saved; g_last_snap = data ? data : "";
        /* re-entrancy: call back into the SDK from inside the event callback */
        {
            spwcam_record_status_t st{}; st.struct_size = sizeof(st);
            if (spwcam_record_get_status(g_ctx, &st) == SPWCAM_OK &&
                spwcam_stream_get_status(g_ctx) == SPWCAM_STREAM_RUNNING)
                ++g_reentry_ok;
        }
        break;
    default: break;
    }
}

static long long file_size(const std::string& p) {
    struct _stat64 st{};
    if (_stat64(p.c_str(), &st) != 0) return -1;
    return (long long)st.st_size;
}

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main()
{
    const char* url = std::getenv("SPWCAM_TEST_RTSP_URL");
    if (!url || !*url) {
        std::printf("SKIP: SPWCAM_TEST_RTSP_URL not set (no RTSP source available)\n");
        return 0;
    }
    std::string outdir = std::getenv("SPWCAM_TEST_OUT_DIR") ? std::getenv("SPWCAM_TEST_OUT_DIR") : "spwcam_test_out";
    _mkdir(outdir.c_str());

    std::printf("===== spwcam stream integration test (%s) =====\n", url);

    spwcam_init_params_t p{}; p.struct_size = sizeof(p); p.log_level = SPWCAM_LOG_WARN;
    g_ctx = spwcam_init(&p);
    CHECK(g_ctx != nullptr, "spwcam_init");
    if (!g_ctx) return 1;

    spwcam_set_event_callback(g_ctx, on_event, nullptr);
    spwcam_set_frame_callback(g_ctx, on_frame, nullptr);

    /* ---- 1/2: stream ---- */
    CHECK(spwcam_stream_open(g_ctx, url, 0) == SPWCAM_OK, "stream_open");
    for (int i = 0; i < 150 && spwcam_stream_get_status(g_ctx) != SPWCAM_STREAM_RUNNING; ++i) sleep_ms(100);
    CHECK(spwcam_stream_get_status(g_ctx) == SPWCAM_STREAM_RUNNING, "stream RUNNING within 15 s");
    CHECK(g_connected >= 1, "STREAM_CONNECTED event fired");

    const int f0 = g_frames; sleep_ms(2000); const int f1 = g_frames;
    std::printf("  frames in 2 s: %d  (%ux%u fmt=%u)\n", f1 - f0, g_w.load(), g_h.load(), g_fmt.load());
    CHECK(f1 - f0 >= 30, "frame rate >= 15 fps over 2 s");
    CHECK(g_w == 1920 && g_h == 1080, "frame is 1920x1080");
    CHECK(g_fmt == SPWCAM_PIXEL_BGRA32, "pixel format BGRA32");

    /* ---- 3: pull mode ---- */
    {
        spwcam_frame_t* fr = nullptr;
        int rc = spwcam_frame_grab(g_ctx, &fr);
        CHECK(rc == SPWCAM_OK && fr && fr->data && fr->stride >= fr->width * 4, "frame_grab returns a frame");
        uint64_t last_seq = fr ? fr->sequence : 0;
        spwcam_frame_free(fr);

        /* NO_FRAME semantics: rapid successive grabs must never hand out the
           same frame twice; sequences strictly increase; and within ~10 ms
           (frames are 40 ms apart) most grabs must report NO_FRAME.        */
        int ok = 0, none = 0; bool monotonic = true;
        for (int i = 0; i < 6; ++i) {
            fr = nullptr;
            rc = spwcam_frame_grab(g_ctx, &fr);
            if (rc == SPWCAM_OK && fr) {
                ++ok;
                if (fr->sequence <= last_seq) monotonic = false;
                last_seq = fr->sequence;
                spwcam_frame_free(fr);
            } else if (rc == SPWCAM_ERR_NO_FRAME && fr == nullptr) {
                ++none;
            }
        }
        CHECK(none >= 4, "rapid re-grab reports NO_FRAME when no new frame arrived");
        CHECK(monotonic, "grabbed frames are never duplicated (sequence strictly increasing)");
        CHECK(ok + none == 6, "grab returns only OK or NO_FRAME");
    }

    /* ---- 4: recording ---- */
    spwcam_record_options_t o{}; o.struct_size = sizeof(o);
    std::strncpy(o.video_dir, outdir.c_str(), sizeof(o.video_dir) - 1);
    std::strncpy(o.snapshot_dir, outdir.c_str(), sizeof(o.snapshot_dir) - 1);
    o.container = SPWCAM_CONTAINER_MP4; o.snapshot_fmt = SPWCAM_IMAGE_PNG;
    CHECK(spwcam_record_set_options(g_ctx, &o) == SPWCAM_OK, "record_set_options");

    CHECK(spwcam_record_start(g_ctx) == SPWCAM_OK, "record_start returns OK");
    for (int i = 0; i < 50 && g_rec_started == 0 && g_rec_failed == 0; ++i) sleep_ms(100);
    CHECK(g_rec_started == 1, "RECORD_STARTED event within 5 s");
    CHECK(g_rec_failed == 0, "no RECORD_FAILED");
    {
        spwcam_record_status_t st{}; st.struct_size = sizeof(st);
        spwcam_record_get_status(g_ctx, &st);
        CHECK(st.recording == 1, "status.recording == 1 while recording");
        CHECK(st.current_file[0] != '\0', "status.current_file set");
        CHECK(st.segment_index == 0, "segment_index == 0 for first file");
    }
    std::string rec_path; { std::lock_guard<std::mutex> lk(g_m); rec_path = g_rec_path; }
    sleep_ms(2000);
    long long sz1 = file_size(rec_path);
    sleep_ms(1500);
    long long sz2 = file_size(rec_path);
    std::printf("  record file: %s  size %lld -> %lld\n", rec_path.c_str(), sz1, sz2);
    CHECK(sz1 > 0, "record file exists and is non-empty");
    CHECK(sz2 > sz1, "record file grows while recording");

    /* ---- 5: snapshots (N calls -> N saved) ---- */
    const int N = 10; int ok_calls = 0;
    for (int i = 0; i < N; ++i) { if (spwcam_snapshot(g_ctx) == SPWCAM_OK) ++ok_calls; sleep_ms(60); }
    for (int i = 0; i < 50 && g_snap_saved < ok_calls; ++i) sleep_ms(100);
    std::printf("  snapshot: %d/%d calls OK, %d SNAPSHOT_SAVED events\n", ok_calls, N, g_snap_saved.load());
    CHECK(ok_calls == N, "every snapshot call accepted while stream is running");
    CHECK(g_snap_saved == ok_calls, "every accepted snapshot produced a SNAPSHOT_SAVED event");
    { std::lock_guard<std::mutex> lk(g_m); CHECK(file_size(g_last_snap) > 0, "snapshot file exists"); }

    /* ---- 6: re-entrancy from inside the event callback ---- */
    CHECK(g_reentry_ok >= 1, "SDK calls from inside event callback succeeded (no deadlock/crash)");

    /* ---- 4b: stop ---- */
    CHECK(spwcam_record_stop(g_ctx) == SPWCAM_OK, "record_stop returns OK");
    for (int i = 0; i < 50 && g_rec_stopped == 0; ++i) sleep_ms(100);
    CHECK(g_rec_stopped == 1, "RECORD_STOPPED event within 5 s");
    {
        spwcam_record_status_t st{}; st.struct_size = sizeof(st);
        spwcam_record_get_status(g_ctx, &st);
        CHECK(st.recording == 0, "status.recording == 0 after stop");
    }
    long long sz_final = file_size(rec_path);
    CHECK(sz_final >= sz2, "record file finalised (size >= last observed)");

    /* ---- 7: deinit while recording finalises the file ---- */
    g_rec_started = 0;
    CHECK(spwcam_record_start(g_ctx) == SPWCAM_OK, "second record_start");
    for (int i = 0; i < 50 && g_rec_started == 0; ++i) sleep_ms(100);
    std::string rec2; { std::lock_guard<std::mutex> lk(g_m); rec2 = g_rec_path; }
    sleep_ms(1500);
    spwcam_deinit(g_ctx);
    g_ctx = nullptr;
    long long sz3 = file_size(rec2);
    std::printf("  file recorded until deinit: %s  size=%lld\n", rec2.c_str(), sz3);
    CHECK(sz3 > 0, "deinit while recording leaves a non-empty finalised file");

    /* MP4 sanity: a finalised MP4 must contain a 'moov' box (index).  The
       muxer may place it at the head or the tail, so scan the whole file
       (test recordings are only a few MB).                                 */
    {
        FILE* f = std::fopen(rec2.c_str(), "rb");
        bool has_moov = false;
        if (f) {
            std::vector<char> buf(1 << 20);
            std::string carry;
            size_t n;
            while (!has_moov && (n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
                std::string chunk = carry + std::string(buf.data(), n);
                if (chunk.find("moov") != std::string::npos) has_moov = true;
                carry = chunk.size() >= 3 ? chunk.substr(chunk.size() - 3) : chunk;
            }
            std::fclose(f);
        }
        CHECK(has_moov, "MP4 contains a moov box (finalised / playable)");
    }

    std::printf("\n%s: total frames=%d\n", g_fail ? "FAILED" : "ALL PASSED", g_frames.load());
    return g_fail ? 1 : 0;
}
