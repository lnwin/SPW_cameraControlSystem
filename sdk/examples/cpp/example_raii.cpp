/**
 * example_raii.cpp  --  SPWater Camera SDK  C++17 入门示例
 *
 * 作者   : 渊视科技
 * 版权所有: 舟山渊视科技有限公司
 * Copyright (C) Zhoushan Yuanshi Technology Co., Ltd.  All rights reserved.
 *
 * 功能演示：
 *   - RAII Camera 对象：自动管理资源，异常安全
 *   - lambda 回调：日志 / 事件 / 帧
 *   - DeviceInfo::rtsp_url() 直接构造 RTSP 地址
 *   - Frame RAII 包装：自动释放帧内存
 *   - 录像 / 截图 / 分段状态查询
 *
 * 编译方法（x64 MSVC）：
 *   cl /std:c++17 /utf-8 example_raii.cpp /I..\..\include /link ..\..\lib\spwcam_sdk.lib
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>

#include "spwcam/spwcam.hpp"

using namespace std::chrono_literals;

/* ─────────────────── 辅助：打印彩色等级前缀 ─────────────────────────── */
static const char* level_tag(spwcam_log_level_t lvl)
{
    switch (lvl) {
    case SPWCAM_LOG_DEBUG: return "[DBG]";
    case SPWCAM_LOG_INFO:  return "[INF]";
    case SPWCAM_LOG_WARN:  return "[WRN]";
    case SPWCAM_LOG_ERROR: return "[ERR]";
    default:               return "[???]";
    }
}

/* ═══════════════════════════════════════════════════════════════════════ */
int main(int argc, char* argv[])
{
    const std::string url = (argc > 1)
        ? argv[1]
        : "rtsp://192.168.1.100:8554/YSTech-TURBIDCAM-0001";

    std::cout
        << "============================================================\n"
        << " SPWater Camera SDK  C++ 示例\n"
        << " 渊视科技 | 舟山渊视科技有限公司\n"
        << " SDK 版本: " << spwcam::Camera::version() << "\n"
        << "============================================================\n\n";

    spwcam::Camera cam;
    std::atomic<int> frame_count{0};   // 生命周期必须覆盖 cam（回调按引用捕获）

    try {
        /* ── 1. 初始化 ─────────────────────────────────────────────── */
        cam.init(0, 0, 0, SPWCAM_LOG_INFO);

        /* ── 2. 注册 lambda 回调 ─────────────────────────────────────── */
        cam.on_log([](spwcam_log_level_t lvl, std::string msg) {
            std::cout << level_tag(lvl) << " " << msg << "\n";
        });

        cam.on_event([](spwcam_event_type_t ev, std::string data) {
            static const char* names[] = {
                "", "DEVICE_DISCOVERED", "DEVICE_LOST",
                "STREAM_CONNECTED", "STREAM_LOST",
                "RECORD_STARTED", "RECORD_STOPPED", "RECORD_FAILED",
                "RECORD_SEGMENT", "SNAPSHOT_SAVED",
                "TRIGGER_STATUS", "IP_CHANGED"
            };
            int idx = (ev >= 1 && ev <= 11) ? (int)ev : 0;
            std::cout << "[EVT] " << std::left << std::setw(22)
                      << names[idx] << "  " << data << "\n";
        });

        cam.on_frame([&frame_count](const spwcam_frame_t& f) {
            int n = ++frame_count;
            if (n % 25 == 0)
                std::cout << "[FRM] #" << std::setw(6) << n
                          << "  seq=" << f.sequence
                          << "  " << f.width << "x" << f.height << "\n";
        });

        /* ── 3. 设备发现 ─────────────────────────────────────────────── */
        std::cout << "[>>] 开始设备发现...\n";
        cam.discovery_start();

        // 最多等 8 秒
        for (int i = 0; i < 32 && cam.device_count() == 0; ++i)
            std::this_thread::sleep_for(250ms);

        auto devices = cam.all_devices();
        std::cout << "[OK] 发现 " << devices.size() << " 台设备\n";
        for (auto& d : devices) {
            std::cout << "     SN   : " << d.sn  << "\n"
                      << "     IP   : " << d.ip  << "\n"
                      << "     RTSP : " << d.rtsp_url() << "\n\n";
        }

        /* ── 4. 相机参数设置 ─────────────────────────────────────────── */
        if (!devices.empty()) {
            auto& d = devices[0];
            cam.set_camera_params(d.sn, 20000, 7.5);  // 曝光 20ms，增益 7.5dB
            cam.set_led(d.sn, true);
            cam.set_trigger_mode(d.sn, false);         // false = 软件触发
            std::cout << "[>>] 相机参数已设置  曝光=20000µs  增益=7.5dB  LED=ON\n\n";
        }

        /* ── 5. 开启码流 ─────────────────────────────────────────────── */
        std::cout << "[>>] 连接: " << url << "\n";
        cam.stream_open(url);

        bool ok = cam.wait_for_stream(15.0);
        if (!ok) {
            std::cerr << "[ERR] 码流未能进入 RUNNING 状态\n";
            return 1;
        }
        std::cout << "[OK] 码流运行中\n\n";

        /* ── 6. 接收帧 5 秒 ──────────────────────────────────────────── */
        std::cout << "[>>] 推送回调接收帧 5 秒...\n";
        std::this_thread::sleep_for(5s);
        std::cout << "[OK] 共收到 " << frame_count.load() << " 帧\n\n";

        /* ── 7. 拉取模式抓帧 ─────────────────────────────────────────── */
        {
            spwcam::Frame frame = cam.frame_grab();
            if (frame) {
                std::cout << "[OK] 拉取帧: seq=" << frame.sequence()
                          << "  " << frame.width() << "x" << frame.height()
                          << "  format="
                          << (frame.format() == SPWCAM_PIXEL_BGRA32
                                ? "BGRA32" : "RGB24")
                          << "\n";

                // frame->data 是 BGRA32 像素缓冲，可传入 OpenCV：
                // cv::Mat mat(frame.height(), frame.width(), CV_8UC4,
                //             const_cast<uint8_t*>(frame.data()),
                //             frame.stride());
            }
        } // Frame 析构函数自动调用 spwcam_frame_free()

        /* ── 8. 录像 ─────────────────────────────────────────────────── */
        // 输出目录：当前工作目录下的 spwcam_record / spwcam_snapshot（可改为任意绝对路径）
        cam.record_set_options("spwcam_record", "spwcam_snapshot");

        std::cout << "[>>] 开始录像...\n";
        cam.record_start();

        std::this_thread::sleep_for(3s);

        std::cout << "[>>] 截图...\n";
        try {
            cam.snapshot();
        } catch (const spwcam::Error& e) {
            std::cout << "[WARN] 截图失败: " << e.what() << "\n";
        }

        std::this_thread::sleep_for(2s);

        std::cout << "[>>] 停止录像...\n";
        cam.record_stop();
        std::this_thread::sleep_for(1s);   // 停止是异步的：等待 RECORD_STOPPED / 文件关闭

        auto st = cam.record_status();
        std::cout << "[OK] 录像已停止  分段=" << st.segment_index
                  << "  总时长=" << st.total_elapsed_ms << " ms\n\n";

        /* ── 9. 关闭 ─────────────────────────────────────────────────── */
        cam.stream_close();
        cam.deinit(); // 析构函数也会自动调用，此处显式调用更清晰

        std::cout << "[DONE] 程序正常退出\n";
        return 0;

    } catch (const spwcam::Error& e) {
        std::cerr << "[EXCEPTION] spwcam 错误: " << e.what()
                  << "  (code=" << e.code() << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] " << e.what() << "\n";
        return 1;
    }
}
