/**
 * spwcam_record.cpp  --  recording, snapshot, status
 */

#include "spwcam/spwcam.h"
#include "spwcam_internal.h"
#include "qt_worker.h"
#include "myStruct.h"

#include <QString>
#include <cstring>

static inline SdkContext* ctx_cast(spwcam_context_t h) {
    return static_cast<SdkContext*>(h);
}

/* ==================================================== options ========= */

SPWCAM_API int spwcam_record_set_options(spwcam_context_t               handle,
                                          const spwcam_record_options_t* opts)
{
    if (!handle || !opts) return SPWCAM_ERR_INVALID_PARAM;
    if (opts->struct_size < sizeof(spwcam_record_options_t))
        return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx = ctx_cast(handle);

    myRecordOptions mo;
    mo.recordPath  = QString::fromUtf8(opts->video_dir);
    mo.capturePath = QString::fromUtf8(opts->snapshot_dir);

    /* container */
    switch (opts->container) {
    case SPWCAM_CONTAINER_AVI: mo.recordType = VideoContainer::AVI; break;
    default:                   mo.recordType = VideoContainer::MP4; break;
    }

    /* snapshot format */
    switch (opts->snapshot_fmt) {
    case SPWCAM_IMAGE_JPG: mo.capturType = ImageFormat::JPG; break;
    case SPWCAM_IMAGE_BMP: mo.capturType = ImageFormat::BMP; break;
    default:               mo.capturType = ImageFormat::PNG; break;
    }

    const int fps     = (opts->fps     > 0) ? opts->fps     : 25;
    const int bitrate = (opts->bitrate_kbps > 0) ? opts->bitrate_kbps : 8000;

    if (opts->segment_minutes != 0 && opts->segment_minutes != 30)
        sdk_log(ctx, SPWCAM_LOG_WARN,
                "[SDK] record options: segment_minutes is fixed at 30 in this version; requested value ignored");
    if (opts->container == SPWCAM_CONTAINER_AVI)
        sdk_log(ctx, SPWCAM_LOG_WARN,
                "[SDK] record options: AVI container is not supported; MP4 will be used");

    SdkQtApp::runOnQtThread([ctx, mo, fps, bitrate]{
        ctx->worker->doRecordSetOptions(mo, fps, bitrate);
    });

    sdk_log(ctx, SPWCAM_LOG_INFO,
            "[SDK] record options set  video=" + std::string(opts->video_dir));
    return SPWCAM_OK;
}

/* ==================================================== start / stop ==== */

SPWCAM_API int spwcam_record_start(spwcam_context_t handle)
{
    if (!handle) return SPWCAM_ERR_NOT_INITIALIZED;
    SdkContext* ctx = ctx_cast(handle);

    if (spwcam_stream_get_status(handle) != SPWCAM_STREAM_RUNNING) {
        sdk_set_error(ctx, SPWCAM_ERR_NOT_OPEN);
        return SPWCAM_ERR_NOT_OPEN;
    }

    /* Reset segment counter for a fresh session */
    {
        std::lock_guard<std::mutex> lk(ctx->rec.lock);
        ctx->rec.segment_index    = 0;
        ctx->rec.session_start_ms = sdk_now_ms();
    }

    SdkQtApp::runOnQtThread([ctx]{
        ctx->worker->doRecordStart();
    });

    sdk_log(ctx, SPWCAM_LOG_INFO, "[SDK] record_start requested");
    return SPWCAM_OK;
}

SPWCAM_API int spwcam_record_stop(spwcam_context_t handle)
{
    if (!handle) return SPWCAM_ERR_NOT_INITIALIZED;
    SdkContext* ctx = ctx_cast(handle);

    SdkQtApp::runOnQtThread([ctx]{
        ctx->worker->doRecordStop();
    });

    sdk_log(ctx, SPWCAM_LOG_INFO, "[SDK] record_stop requested");
    return SPWCAM_OK;
}

/* ==================================================== status ========== */

SPWCAM_API int spwcam_record_get_status(spwcam_context_t        handle,
                                         spwcam_record_status_t* status)
{
    if (!handle || !status) return SPWCAM_ERR_INVALID_PARAM;
    if (status->struct_size < sizeof(spwcam_record_status_t))
        return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx = ctx_cast(handle);

    std::lock_guard<std::mutex> lk(ctx->rec.lock);
    status->recording    = ctx->rec.recording ? 1 : 0;
    status->segment_index= ctx->rec.segment_index;

    sdk_strcpy(status->current_file, ctx->rec.current_file.c_str());

    const int64_t now = sdk_now_ms();
    if (ctx->rec.recording && ctx->rec.segment_start_ms > 0)
        status->segment_elapsed_ms = now - ctx->rec.segment_start_ms;
    else
        status->segment_elapsed_ms = 0;

    if (ctx->rec.session_start_ms > 0)
        status->total_elapsed_ms = now - ctx->rec.session_start_ms;
    else
        status->total_elapsed_ms = 0;

    return SPWCAM_OK;
}

/* ==================================================== snapshot ======== */

SPWCAM_API int spwcam_snapshot(spwcam_context_t handle)
{
    if (!handle) return SPWCAM_ERR_NOT_INITIALIZED;
    SdkContext* ctx = ctx_cast(handle);

    if (spwcam_stream_get_status(handle) != SPWCAM_STREAM_RUNNING) {
        sdk_set_error(ctx, SPWCAM_ERR_NOT_OPEN);
        return SPWCAM_ERR_NOT_OPEN;
    }

    bool ok = false;
    SdkQtApp::runOnQtThread([ctx, &ok]{
        ok = ctx->worker->doSnapshot();
    });
    if (!ok) {
        sdk_set_error(ctx, SPWCAM_ERR_NO_FRAME);
        return SPWCAM_ERR_NO_FRAME;
    }

    sdk_log(ctx, SPWCAM_LOG_INFO, "[SDK] snapshot requested");
    return SPWCAM_OK;
}
