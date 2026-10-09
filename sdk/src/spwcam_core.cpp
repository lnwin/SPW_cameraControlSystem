/**
 * spwcam_core.cpp  --  spwcam_init / spwcam_deinit / version / log / error
 *
 * 作者   : 渊视科技
 * 版权所有: 舟山渊视科技有限公司
 */

#include "spwcam/spwcam.h"
#include "spwcam_internal.h"
#include "qt_worker.h"

#include <QCoreApplication>
#include <QMetaType>
#include <QThread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
}

#include <atomic>
#include <new>
#include <string>

/* The SDK is compiled against FFmpeg headers and loads FFmpeg DLLs at runtime;
   both must be the same major version or AVCodecContext layouts differ and the
   encoder silently fails to open.  Checked at compile time (against the DLLs
   found by CMake) and again at runtime (against the DLL actually loaded).    */
#ifdef SPWCAM_EXPECT_AVCODEC_MAJOR
static_assert(LIBAVCODEC_VERSION_MAJOR == SPWCAM_EXPECT_AVCODEC_MAJOR,
              "libavcodec header major version does not match the FFmpeg DLLs");
#endif

static bool sdk_check_ffmpeg_runtime(std::string& msg)
{
    const unsigned rt = avcodec_version() >> 16;
    if (rt != (unsigned)LIBAVCODEC_VERSION_MAJOR) {
        msg = "[SDK] FATAL: avcodec DLL major " + std::to_string(rt) +
              " != compiled header major " + std::to_string(LIBAVCODEC_VERSION_MAJOR) +
              " (wrong avcodec-*.dll on PATH?)";
        return false;
    }
    return true;
}

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

/* ============================================ runtime path bootstrap === */
/*
 * Called once at init time.  Detects the directory that contains
 * spwcam_sdk.dll and sets GStreamer environment variables so that:
 *   - GStreamer plugin DLLs are loaded from  <dll_dir>\gst_plugins\
 *   - The registry cache is written to the user's TEMP directory
 *   - System-wide GStreamer installations are ignored completely
 *
 * This makes the SDK fully self-contained: the third party only needs to
 * place the bundled DLLs next to their application.
 */
static void sdk_setup_runtime_paths()
{
#ifdef _WIN32
    /* process-wide, idempotent: repeated init/deinit must not grow PATH */
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return;

    /* ── Step 1: locate this DLL's own directory ── */
    wchar_t dll_path[MAX_PATH] = {};
    HMODULE hmod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&sdk_setup_runtime_paths),
        &hmod);
    if (!hmod || !GetModuleFileNameW(hmod, dll_path, MAX_PATH))
        return;

    wchar_t* sep = wcsrchr(dll_path, L'\\');
    if (sep) *sep = L'\0';
    /* dll_path is now the directory containing spwcam_sdk.dll */

    /* ── Step 2: prepend dll_dir to PATH so GStreamer core DLLs load ── */
    {
        wchar_t cur[32768] = {};
        GetEnvironmentVariableW(L"PATH", cur, 32768);
        std::wstring new_path = std::wstring(dll_path) + L";" + cur;
        SetEnvironmentVariableW(L"PATH", new_path.c_str());
    }

    /* ── Step 3: GStreamer plugin directory ── */
    /* Plugins are expected in  <dll_dir>\gst_plugins\  (self-contained bundle).
       If that directory does not exist (e.g. running from a build tree or with
       a system-wide GStreamer), leave the GStreamer environment untouched so
       the default plugin search path / GST_PLUGIN_PATH_1_0 keep working.      */
    {
        std::wstring pdir = std::wstring(dll_path) + L"\\gst_plugins";
        const DWORD attr = GetFileAttributesW(pdir.c_str());
        const bool bundled = (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
        if (bundled) {
            /* GST_PLUGIN_PATH_1_0  overrides the built-in search path       */
            SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0",  pdir.c_str());
            /* Suppress the system-wide plugin directories entirely           */
            SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", L"");
            /* Disable the external scanner helper (not needed for bundles)   */
            SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER_1_0", L"");

            /* ── Step 4: private registry cache in the user's TEMP dir ── */
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            std::wstring reg = std::wstring(tmp) + L"spwcam_sdk_gst.registry";
            SetEnvironmentVariableW(L"GST_REGISTRY_1_0", reg.c_str());
            SetEnvironmentVariableW(L"GST_REGISTRY_UPDATE", L"yes");
        }
    }
#endif  /* _WIN32 */
}


/* ======================================================= version / error */

SPWCAM_API const char* spwcam_version(void)
{
    return SPWCAM_VERSION_STR;
}

SPWCAM_API const char* spwcam_get_error_string(int code)
{
    switch (code) {
    case SPWCAM_OK:                   return "OK";
    case SPWCAM_ERR_INVALID_PARAM:    return "Invalid parameter";
    case SPWCAM_ERR_NOT_INITIALIZED:  return "SDK not initialized";
    case SPWCAM_ERR_ALREADY_OPEN:     return "Already open";
    case SPWCAM_ERR_NOT_OPEN:         return "Not open";
    case SPWCAM_ERR_DEVICE_NOT_FOUND: return "Device not found";
    case SPWCAM_ERR_TIMEOUT:          return "Operation timed out";
    case SPWCAM_ERR_IO:               return "I/O error";
    case SPWCAM_ERR_ENCODER:          return "Encoder error";
    case SPWCAM_ERR_NO_FRAME:         return "No frame available";
    case SPWCAM_ERR_OUT_OF_MEMORY:    return "Out of memory";
    case SPWCAM_ERR_INTERNAL:         return "Internal SDK error";
    default:                          return "Unknown error";
    }
}

/* ========================================================== lifecycle === */

SPWCAM_API spwcam_context_t spwcam_init(const spwcam_init_params_t* params)
{
    /* runtime paths (GStreamer plugin dir, DLL dir on PATH) — before gst_init() */
    sdk_setup_runtime_paths();

    /* --- acquire the process-wide QCoreApplication (reference counted) --- */
    SdkQtApp::acquire();

    /* --- register metatypes needed across queued connections --- */
    qRegisterMetaType<QSharedPointer<QImage>>("QSharedPointer<QImage>");
    qRegisterMetaType<myRecordOptions>("myRecordOptions");

    /* --- allocate context --- */
    SdkContext* ctx = nullptr;
    try {
        ctx = new SdkContext();
    } catch (const std::bad_alloc&) {
        SdkQtApp::release();
        return nullptr;
    }

    /* --- apply init params --- */
    if (params && params->struct_size >= sizeof(spwcam_init_params_t)) {
        if (params->discover_port)  ctx->init_discover_port  = params->discover_port;
        if (params->heartbeat_port) ctx->init_heartbeat_port = params->heartbeat_port;
        if (params->cmd_port)       ctx->init_cmd_port       = params->cmd_port;
        ctx->cbs.log_level = params->log_level;
    }

    /* --- create worker on Qt thread --- */
    SdkQtWorker* worker = nullptr;
    bool ran = false;
    try {
        ran = SdkQtApp::runOnQtThread([&]{
            worker = new SdkQtWorker(ctx);
        });
    } catch (...) {
        ran = false;
    }

    if (!ran || !worker) {
        delete ctx;
        SdkQtApp::release();
        return nullptr;
    }

    ctx->worker = worker;
    ctx->qt_ready.store(true);

    /* runtime FFmpeg ABI check -- log loudly; recording would fail otherwise */
    {
        std::string m;
        if (!sdk_check_ffmpeg_runtime(m)) {
            sdk_log(ctx, SPWCAM_LOG_ERROR, m);
            sdk_set_error(ctx, SPWCAM_ERR_INTERNAL);
        }
    }

    sdk_log(ctx, SPWCAM_LOG_INFO,
            "[SDK] initialized  version=" SPWCAM_VERSION_STR);
    return static_cast<spwcam_context_t>(ctx);
}

SPWCAM_API void spwcam_deinit(spwcam_context_t handle)
{
    if (!handle) return;
    SdkContext* ctx = static_cast<SdkContext*>(handle);

    sdk_log(ctx, SPWCAM_LOG_INFO, "[SDK] deinit");

    /* The worker destructor (on the Qt thread) stops the stream, finalises
       any open recording file synchronously, stops the recorder thread and
       stops discovery — in that order.                                     */
    if (ctx->worker) {
        SdkQtApp::runOnQtThread([ctx]{
            delete ctx->worker;
            ctx->worker = nullptr;
        });
    }

    /* no callbacks may fire after this point */
    {
        std::lock_guard<std::mutex> lk(ctx->cbs.lock);
        ctx->cbs.log_cb = nullptr; ctx->cbs.event_cb = nullptr; ctx->cbs.frame_cb = nullptr;
    }

    delete ctx;
    SdkQtApp::release();
}

SPWCAM_API int spwcam_get_last_error(spwcam_context_t handle)
{
    if (!handle) return SPWCAM_ERR_NOT_INITIALIZED;
    return static_cast<SdkContext*>(handle)->last_error.load();
}

/* =========================================================== logging === */

SPWCAM_API void spwcam_set_log_callback(spwcam_context_t handle,
                                         spwcam_log_callback_t cb,
                                         void* user_data)
{
    if (!handle) return;
    SdkContext* ctx = static_cast<SdkContext*>(handle);
    std::lock_guard<std::mutex> lk(ctx->cbs.lock);
    ctx->cbs.log_cb = cb;
    ctx->cbs.log_ud = user_data;
}

SPWCAM_API void spwcam_set_log_level(spwcam_context_t handle,
                                      spwcam_log_level_t level)
{
    if (!handle) return;
    SdkContext* ctx = static_cast<SdkContext*>(handle);
    std::lock_guard<std::mutex> lk(ctx->cbs.lock);
    ctx->cbs.log_level = level;
}

/* =========================================================== events === */

SPWCAM_API void spwcam_set_event_callback(spwcam_context_t handle,
                                           spwcam_event_callback_t cb,
                                           void* user_data)
{
    if (!handle) return;
    SdkContext* ctx = static_cast<SdkContext*>(handle);
    std::lock_guard<std::mutex> lk(ctx->cbs.lock);
    ctx->cbs.event_cb = cb;
    ctx->cbs.event_ud = user_data;
}
