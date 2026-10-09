/**
 * spwcam_device.cpp  --  device discovery and control
 */

#include "spwcam/spwcam.h"
#include "spwcam_internal.h"
#include "qt_worker.h"

#include <QMetaObject>
#include <QStringList>
#include <QString>

static inline SdkContext* ctx_cast(spwcam_context_t h) {
    return static_cast<SdkContext*>(h);
}

/* ===================================================== discovery ======= */

SPWCAM_API int spwcam_discovery_start(spwcam_context_t handle,
                                       uint16_t         discover_port,
                                       uint16_t         heartbeat_port)
{
    if (!handle) return SPWCAM_ERR_NOT_INITIALIZED;
    SdkContext* ctx = ctx_cast(handle);

    const quint16 dp = discover_port  ? discover_port  : ctx->init_discover_port;
    const quint16 hb = heartbeat_port ? heartbeat_port : ctx->init_heartbeat_port;
    const quint16 cmd= ctx->init_cmd_port;

    bool ok = false;
    SdkQtApp::runOnQtThread([ctx, dp, hb, cmd, &ok]{
        ok = ctx->worker->doDiscoveryStart(dp, hb, cmd);
    });

    if (!ok) {
        sdk_log(ctx, SPWCAM_LOG_ERROR,
                "[SDK] discovery start FAILED: cannot bind UDP " + std::to_string(dp) +
                "/" + std::to_string(hb) + " (port in use by another process?)");
        sdk_set_error(ctx, SPWCAM_ERR_IO);
        return SPWCAM_ERR_IO;
    }

    sdk_log(ctx, SPWCAM_LOG_INFO,
            "[SDK] discovery started  disc=" + std::to_string(dp) +
            " hb=" + std::to_string(hb));
    return SPWCAM_OK;
}

SPWCAM_API void spwcam_discovery_stop(spwcam_context_t handle)
{
    if (!handle) return;
    SdkContext* ctx = ctx_cast(handle);
    SdkQtApp::runOnQtThread([ctx]{ ctx->worker->doDiscoveryStop(); });
}

/* ===================================================== device list ===== */

SPWCAM_API int spwcam_get_device_count(spwcam_context_t handle)
{
    if (!handle) return 0;
    SdkContext* ctx = ctx_cast(handle);
    int n = 0;
    SdkQtApp::runOnQtThread([ctx, &n]{
        n = ctx->worker->queryDeviceCount();
    });
    return n;
}

SPWCAM_API int spwcam_get_device_info(spwcam_context_t      handle,
                                       int                   index,
                                       spwcam_device_info_t* info)
{
    if (!handle || !info) return SPWCAM_ERR_INVALID_PARAM;
    if (info->struct_size < sizeof(spwcam_device_info_t)) return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx = ctx_cast(handle);
    int rc = SPWCAM_ERR_DEVICE_NOT_FOUND;

    SdkQtApp::runOnQtThread([ctx, index, info, &rc]{
        QStringList sns = ctx->worker->queryDeviceSnList();
        if (index < 0 || index >= sns.size()) return;

        const QString& sn = sns.at(index);
        QString ip; quint16 rport; QString rpath; qint64 seen;
        if (!ctx->worker->queryDeviceInfo(sn, ip, rport, rpath, seen)) return;

        sdk_strcpy(info->sn,        sn.toUtf8().constData());
        sdk_strcpy(info->ip,        ip.toUtf8().constData());
        sdk_strcpy(info->rtsp_path, rpath.toUtf8().constData());
        info->rtsp_port    = rport;
        info->last_seen_ms = seen;
        rc = SPWCAM_OK;
    });

    if (rc != SPWCAM_OK) sdk_set_error(ctx, rc);
    return rc;
}

SPWCAM_API int spwcam_get_device_info_by_sn(spwcam_context_t      handle,
                                              const char*           sn,
                                              spwcam_device_info_t* info)
{
    if (!handle || !sn || !info) return SPWCAM_ERR_INVALID_PARAM;
    if (info->struct_size < sizeof(spwcam_device_info_t)) return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx = ctx_cast(handle);
    int rc = SPWCAM_ERR_DEVICE_NOT_FOUND;
    const QString qsn = QString::fromUtf8(sn);

    SdkQtApp::runOnQtThread([ctx, &qsn, info, &rc]{
        QString ip; quint16 rport; QString rpath; qint64 seen;
        if (!ctx->worker->queryDeviceInfo(qsn, ip, rport, rpath, seen)) return;

        sdk_strcpy(info->sn,        qsn.toUtf8().constData());
        sdk_strcpy(info->ip,        ip.toUtf8().constData());
        sdk_strcpy(info->rtsp_path, rpath.toUtf8().constData());
        info->rtsp_port    = rport;
        info->last_seen_ms = seen;
        rc = SPWCAM_OK;
    });

    if (rc != SPWCAM_OK) sdk_set_error(ctx, rc);
    return rc;
}

/* ===================================================== device control == */

SPWCAM_API int spwcam_set_led(spwcam_context_t handle,
                               const char*      sn,
                               int              enable)
{
    if (!handle || !sn) return SPWCAM_ERR_INVALID_PARAM;
    SdkContext* ctx = ctx_cast(handle);
    const QString qsn   = QString::fromUtf8(sn);
    const bool    en    = (enable != 0);
    SdkQtApp::runOnQtThread([ctx, qsn, en]{
        ctx->worker->doSetLed(qsn, en);
    });
    return SPWCAM_OK;
}

SPWCAM_API int spwcam_set_trigger_mode(spwcam_context_t      handle,
                                        const char*           sn,
                                        spwcam_trigger_mode_t mode)
{
    if (!handle || !sn) return SPWCAM_ERR_INVALID_PARAM;
    SdkContext* ctx = ctx_cast(handle);
    const QString qsn  = QString::fromUtf8(sn);
    const QString qmode= (mode == SPWCAM_TRIGGER_HARDWARE)
                         ? QStringLiteral("hardware")
                         : QStringLiteral("software");
    SdkQtApp::runOnQtThread([ctx, qsn, qmode]{
        ctx->worker->doSetTrigger(qsn, qmode);
    });
    return SPWCAM_OK;
}

SPWCAM_API int spwcam_set_camera_params(spwcam_context_t handle,
                                         const char*      sn,
                                         int              exposure_us,
                                         double           gain_db)
{
    if (!handle || !sn) return SPWCAM_ERR_INVALID_PARAM;
    if (exposure_us < 10000 || exposure_us > 30000) return SPWCAM_ERR_INVALID_PARAM;
    if (gain_db < 0.0 || gain_db > 15.0)            return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx = ctx_cast(handle);
    const QString qsn = QString::fromUtf8(sn);
    SdkQtApp::runOnQtThread([ctx, qsn, exposure_us, gain_db]{
        ctx->worker->doSetCameraParams(qsn, exposure_us, gain_db);
    });
    return SPWCAM_OK;
}

SPWCAM_API int spwcam_set_ip(spwcam_context_t handle,
                              const char*      sn,
                              const char*      new_ip,
                              int              mask)
{
    if (!handle || !sn || !new_ip) return SPWCAM_ERR_INVALID_PARAM;
    if (mask < 8 || mask > 30) return SPWCAM_ERR_INVALID_PARAM;

    SdkContext* ctx = ctx_cast(handle);
    const QString qsn = QString::fromUtf8(sn);
    const QString qip = QString::fromUtf8(new_ip);
    SdkQtApp::runOnQtThread([ctx, qsn, qip, mask]{
        ctx->worker->doSetIp(qsn, qip, mask);
    });
    return SPWCAM_OK;
}
