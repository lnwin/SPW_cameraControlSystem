/**
 * spwcam_stream.cpp  --  RTSP stream open / close / status / frame
 */

#include "spwcam/spwcam.h"
#include "spwcam_internal.h"
#include "qt_worker.h"

#include <QString>
#include <cstdlib>
#include <cstring>
#include <new>

static inline SdkContext* ctx_cast(spwcam_context_t h) {
    return static_cast<SdkContext*>(h);
}

/* =========================================================== stream === */

SPWCAM_API int spwcam_stream_open(spwcam_context_t handle,
                                   const char*      rtsp_url,
                                   int              latency_ms)
{
    if (!handle || !rtsp_url || rtsp_url[0] == '\0')
        return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx  = ctx_cast(handle);
    const QString url = QString::fromUtf8(rtsp_url);

    SdkQtApp::runOnQtThread([ctx, url, latency_ms]{
        ctx->worker->doStreamOpen(url, latency_ms);
    });

    sdk_log(ctx, SPWCAM_LOG_INFO,
            "[SDK] stream_open: " + std::string(rtsp_url));
    return SPWCAM_OK;
}

SPWCAM_API int spwcam_stream_close(spwcam_context_t handle)
{
    if (!handle) return SPWCAM_ERR_NOT_INITIALIZED;
    SdkContext* ctx = ctx_cast(handle);

    SdkQtApp::runOnQtThread([ctx]{
        ctx->worker->doStreamClose();
    });

    sdk_log(ctx, SPWCAM_LOG_INFO, "[SDK] stream_close");
    return SPWCAM_OK;
}

SPWCAM_API spwcam_stream_status_t spwcam_stream_get_status(spwcam_context_t handle)
{
    if (!handle) return SPWCAM_STREAM_STOPPED;
    SdkContext* ctx = ctx_cast(handle);
    int s = ctx->stream_status.load(std::memory_order_relaxed);
    return static_cast<spwcam_stream_status_t>(s);
}

/* =========================================================== frames === */

SPWCAM_API void spwcam_set_frame_callback(spwcam_context_t        handle,
                                           spwcam_frame_callback_t cb,
                                           void*                   user_data)
{
    if (!handle) return;
    SdkContext* ctx = ctx_cast(handle);
    std::lock_guard<std::mutex> lk(ctx->cbs.lock);
    ctx->cbs.frame_cb = cb;
    ctx->cbs.frame_ud = user_data;
}

SPWCAM_API int spwcam_frame_grab(spwcam_context_t  handle,
                                  spwcam_frame_t**  out_frame)
{
    if (!handle || !out_frame) return SPWCAM_ERR_INVALID_PARAM;
    *out_frame = nullptr;

    SdkContext* ctx = ctx_cast(handle);

    std::lock_guard<std::mutex> lk(ctx->grab.lock);
    if (!ctx->grab.fresh) return SPWCAM_ERR_NO_FRAME;

    /* Allocate a self-contained frame: struct + pixel data in one block */
    const size_t data_bytes = ctx->grab.data.size();
    uint8_t* block = static_cast<uint8_t*>(
        std::malloc(sizeof(spwcam_frame_t) + data_bytes));
    if (!block) return SPWCAM_ERR_OUT_OF_MEMORY;

    spwcam_frame_t* f = reinterpret_cast<spwcam_frame_t*>(block);
    uint8_t* pixels   = block + sizeof(spwcam_frame_t);

    f->struct_size  = static_cast<uint32_t>(sizeof(spwcam_frame_t));
    f->data         = pixels;
    f->width        = ctx->grab.width;
    f->height       = ctx->grab.height;
    f->stride       = ctx->grab.stride;
    f->pixel_format = SPWCAM_PIXEL_BGRA32;
    f->timestamp_ms = ctx->grab.ts_ms;
    f->sequence     = ctx->grab.seq;

    std::memcpy(pixels, ctx->grab.data.data(), data_bytes);
    ctx->grab.fresh = false;

    *out_frame = f;
    return SPWCAM_OK;
}

SPWCAM_API void spwcam_frame_free(spwcam_frame_t* frame)
{
    /* frame + pixel data were allocated in one std::malloc block */
    std::free(frame);
}
