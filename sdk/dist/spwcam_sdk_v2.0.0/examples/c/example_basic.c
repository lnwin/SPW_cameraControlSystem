/**
 * example_basic.c  --  SPWater Camera SDK  C 语言入门示例
 *
 * 作者   : 渊视科技
 * 版权所有: 舟山渊视科技有限公司
 * Copyright (C) Zhoushan Yuanshi Technology Co., Ltd.  All rights reserved.
 *
 * 功能演示：
 *   1. SDK 初始化与日志回调注册
 *   2. UDP 设备发现与设备信息查询
 *   3. 相机参数设置（曝光 / 增益 / LED / 触发模式）
 *   4. RTSP 码流开启与状态等待
 *   5. 帧获取（推送回调 + 拉取接口）
 *   6. 录像开始 / 截图 / 录像停止
 *   7. 资源释放
 *
 * 编译方法（x64 MSVC）：
 *   cl example_basic.c /utf-8 /I..\..\include /link ..\..\lib\spwcam_sdk.lib
 *
 * 运行依赖：
 *   spwcam_sdk.dll、Qt6Core.dll、Qt6Network.dll、Qt6Gui.dll
 *   GStreamer DLLs、FFmpeg DLLs（均需在 PATH 中或与 exe 同目录）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define SLEEP_MS(ms)  Sleep(ms)
#else
#  include <unistd.h>
#  define SLEEP_MS(ms)  usleep((ms) * 1000u)
#endif

#include "spwcam/spwcam.h"

/* ─────────────────────────────────────────── 日志回调 ─────────────────── */

static void on_log(spwcam_log_level_t level, const char* msg, void* ud)
{
    const char* tag = "DBG";
    if (level == SPWCAM_LOG_INFO)  tag = "INF";
    if (level == SPWCAM_LOG_WARN)  tag = "WRN";
    if (level == SPWCAM_LOG_ERROR) tag = "ERR";
    printf("[%s] %s\n", tag, msg);
    (void)ud;
}

/* ─────────────────────────────────────────── 事件回调 ─────────────────── */

static void on_event(spwcam_event_type_t ev, const char* data, void* ud)
{
    /* 将事件枚举值映射为可读字符串 */
    const char* name = "UNKNOWN";
    switch (ev) {
    case SPWCAM_EVENT_DEVICE_DISCOVERED: name = "DEVICE_DISCOVERED"; break;
    case SPWCAM_EVENT_DEVICE_LOST:       name = "DEVICE_LOST";       break;
    case SPWCAM_EVENT_STREAM_CONNECTED:  name = "STREAM_CONNECTED";  break;
    case SPWCAM_EVENT_STREAM_LOST:       name = "STREAM_LOST";       break;
    case SPWCAM_EVENT_RECORD_STARTED:    name = "RECORD_STARTED";    break;
    case SPWCAM_EVENT_RECORD_STOPPED:    name = "RECORD_STOPPED";    break;
    case SPWCAM_EVENT_SNAPSHOT_SAVED:    name = "SNAPSHOT_SAVED";    break;
    default: break;
    }
    printf("[EVT] %-22s  data=%s\n", name, data ? data : "");
    (void)ud;
}

/* ─────────────────────────────────────────── 帧回调 ───────────────────── */

static volatile int g_frame_count = 0;

static void on_frame(const spwcam_frame_t* f, void* ud)
{
    g_frame_count++;
    /* 每 25 帧打印一次统计（约 1 秒一行） */
    if (g_frame_count % 25 == 0) {
        printf("[FRM] #%-6d  seq=%-8llu  %4u x %-4u  format=%s\n",
               g_frame_count,
               (unsigned long long)f->sequence,
               f->width, f->height,
               f->pixel_format == SPWCAM_PIXEL_BGRA32 ? "BGRA32" : "RGB24");
    }
    (void)ud;
}

/* ─────────────────────────────────────────── 辅助：打印设备信息 ────────── */

static void print_device(spwcam_context_t ctx, int idx)
{
    spwcam_device_info_t di;
    memset(&di, 0, sizeof(di));
    di.struct_size = (uint32_t)sizeof(di);

    if (spwcam_get_device_info(ctx, idx, &di) != SPWCAM_OK) return;

    printf("  [%d] SN    : %s\n",   idx, di.sn);
    printf("      IP    : %s\n",         di.ip);
    printf("      RTSP  : rtsp://%s:%u%s\n",
           di.ip, di.rtsp_port, di.rtsp_path);
}

/* ─────────────────────────────────────────── main ─────────────────────── */

int main(int argc, char* argv[])
{
    /* 默认 RTSP URL，也可通过命令行参数覆盖 */
    const char* rtsp_url = (argc > 1)
        ? argv[1]
        : "rtsp://192.168.1.100:8554/YSTech-TURBIDCAM-0001";

    printf("============================================================\n");
    printf(" SPWater Camera SDK  示例程序\n");
    printf(" 渊视科技 | 舟山渊视科技有限公司\n");
    printf(" SDK 版本: %s\n", spwcam_version());
    printf("============================================================\n\n");

    /* ── 1. 初始化 SDK ──────────────────────────────────────────────── */
    spwcam_init_params_t params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.log_level   = SPWCAM_LOG_INFO;   /* 只显示 INFO 及以上等级日志 */

    spwcam_context_t ctx = spwcam_init(&params);
    if (!ctx) {
        fprintf(stderr, "[ERR] spwcam_init 失败\n");
        return 1;
    }
    printf("[OK] SDK 初始化成功\n\n");

    /* ── 2. 注册回调 ─────────────────────────────────────────────────── */
    spwcam_set_log_callback(ctx, on_log, NULL);
    spwcam_set_event_callback(ctx, on_event, NULL);
    spwcam_set_frame_callback(ctx, on_frame, NULL);

    /* ── 3. 设备发现 ─────────────────────────────────────────────────── */
    printf("[>>] 开始设备发现（默认端口 7776 / 8888）...\n");
    if (spwcam_discovery_start(ctx, 0, 0) != SPWCAM_OK) {
        fprintf(stderr, "[ERR] discovery_start 失败\n");
        spwcam_deinit(ctx);
        return 1;
    }

    /* 轮询等待，最多 8 秒 */
    int waited = 0;
    while (spwcam_get_device_count(ctx) == 0 && waited < 8000) {
        SLEEP_MS(250);
        waited += 250;
    }

    int dev_count = spwcam_get_device_count(ctx);
    printf("[OK] 发现设备 %d 台\n", dev_count);
    for (int i = 0; i < dev_count; i++)
        print_device(ctx, i);
    printf("\n");

    /* ── 4. 相机参数设置（以第 0 台设备为例） ───────────────────────── */
    if (dev_count > 0) {
        spwcam_device_info_t di;
        memset(&di, 0, sizeof(di));
        di.struct_size = (uint32_t)sizeof(di);
        spwcam_get_device_info(ctx, 0, &di);

        /* 曝光时间 20000 µs，增益 7.5 dB */
        spwcam_set_camera_params(ctx, di.sn, 20000, 7.5);
        printf("[>>] 已发送相机参数: 曝光=20000µs  增益=7.5dB\n");

        /* 打开 LED 补光 */
        spwcam_set_led(ctx, di.sn, 1);
        printf("[>>] LED 已开启\n");

        /* 软件触发模式（默认） */
        spwcam_set_trigger_mode(ctx, di.sn, SPWCAM_TRIGGER_SOFTWARE);
        printf("[>>] 触发模式: 软件触发\n\n");
    }

    /* ── 5. 开启 RTSP 码流 ──────────────────────────────────────────── */
    printf("[>>] 连接码流: %s\n", rtsp_url);
    if (spwcam_stream_open(ctx, rtsp_url, 0) != SPWCAM_OK) {
        fprintf(stderr, "[ERR] stream_open 失败: %s\n",
                spwcam_get_error_string(spwcam_get_last_error(ctx)));
        spwcam_deinit(ctx);
        return 1;
    }

    /* 等待码流进入 RUNNING 状态（最多 15 秒） */
    waited = 0;
    while (spwcam_stream_get_status(ctx) != SPWCAM_STREAM_RUNNING
           && waited < 15000) {
        SLEEP_MS(200);
        waited += 200;
    }

    if (spwcam_stream_get_status(ctx) != SPWCAM_STREAM_RUNNING) {
        fprintf(stderr, "[ERR] 码流未能进入 RUNNING 状态\n");
        spwcam_deinit(ctx);
        return 1;
    }
    printf("[OK] 码流已运行\n\n");

    /* ── 6. 接收帧（推送模式）5 秒 ─────────────────────────────────── */
    printf("[>>] 推送回调接收帧，持续 5 秒...\n");
    SLEEP_MS(5000);
    printf("[OK] 共收到 %d 帧\n\n", g_frame_count);

    /* ── 7. 拉取最新一帧（拉取模式）─────────────────────────────────── */
    {
        spwcam_frame_t* frame = NULL;
        int rc = spwcam_frame_grab(ctx, &frame);
        if (rc == SPWCAM_OK && frame) {
            printf("[OK] 拉取帧: seq=%-8llu  %ux%u  stride=%u bytes\n",
                   (unsigned long long)frame->sequence,
                   frame->width, frame->height, frame->stride);

            /* frame->data 指向 BGRA32 像素缓冲区
             * 可在此处传入 OpenCV / 自定义处理管线 */

            spwcam_frame_free(frame);   /* 必须释放，否则内存泄漏 */
        } else {
            printf("[WARN] 拉取帧: %s\n", spwcam_get_error_string(rc));
        }
    }
    printf("\n");

    /* ── 8. 配置录像选项 ─────────────────────────────────────────────── */
    {
        spwcam_record_options_t opts;
        memset(&opts, 0, sizeof(opts));
        opts.struct_size     = (uint32_t)sizeof(opts);
        opts.fps             = 25;
        opts.bitrate_kbps    = 8000;
        opts.segment_minutes = 30;          /* 30 分钟自动分段 */
        opts.container       = SPWCAM_CONTAINER_MP4;
        opts.snapshot_fmt    = SPWCAM_IMAGE_PNG;

        /* 输出目录：当前工作目录下的 spwcam_record / spwcam_snapshot（可改为任意绝对路径） */
        strncpy(opts.video_dir,    "spwcam_record",   sizeof(opts.video_dir)    - 1);
        strncpy(opts.snapshot_dir, "spwcam_snapshot", sizeof(opts.snapshot_dir) - 1);

        if (spwcam_record_set_options(ctx, &opts) != SPWCAM_OK) {
            fprintf(stderr, "[ERR] record_set_options 失败\n");
        }
    }

    /* ── 9. 开始录像 ─────────────────────────────────────────────────── */
    printf("[>>] 开始录像（3 秒后截图，再等 2 秒后停止）...\n");
    if (spwcam_record_start(ctx) != SPWCAM_OK) {
        printf("[WARN] record_start 失败（可能码流不在 RUNNING）\n");
    } else {
        SLEEP_MS(3000);

        /* ── 10. 截图 ───────────────────────────────────────────────── */
        printf("[>>] 截图...\n");
        {
            int src = spwcam_snapshot(ctx);   /* 异步，文件路径通过 SNAPSHOT_SAVED 事件返回 */
            if (src != SPWCAM_OK)
                printf("[WARN] 截图失败: %s\n", spwcam_get_error_string(src));
        }

        SLEEP_MS(2000);

        /* ── 11. 停止录像 ────────────────────────────────────────────── */
        printf("[>>] 停止录像...\n");
        spwcam_record_stop(ctx);
        SLEEP_MS(1000);   /* 停止是异步的：等待 RECORD_STOPPED 事件 / 文件关闭 */

        /* 查询录像状态 */
        spwcam_record_status_t st;
        memset(&st, 0, sizeof(st));
        st.struct_size = (uint32_t)sizeof(st);
        if (spwcam_record_get_status(ctx, &st) == SPWCAM_OK) {
            printf("[OK] 录像状态: recording=%d  segment=%d  total=%lld ms\n",
                   st.recording, st.segment_index,
                   (long long)st.total_elapsed_ms);
        }
    }

    /* ── 12. 清理资源 ────────────────────────────────────────────────── */
    printf("\n[>>] 关闭码流...\n");
    spwcam_stream_close(ctx);

    printf("[>>] 释放 SDK...\n");
    spwcam_deinit(ctx);

    printf("\n[DONE] 程序正常退出\n");
    return 0;
}
