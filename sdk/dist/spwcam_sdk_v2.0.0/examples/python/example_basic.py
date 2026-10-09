"""
example_basic.py  --  SPWater Camera SDK  Python 入门示例

作者   : 渊视科技
版权所有: 舟山渊视科技有限公司
Copyright (C) Zhoushan Yuanshi Technology Co., Ltd.  All rights reserved.

功能演示：
  1. SDK 初始化与日志/事件回调
  2. 设备发现与参数设置
  3. RTSP 码流连接与状态等待
  4. 帧获取（推送回调 + NumPy 拉取）
  5. 录像 / 截图 / 录像停止

依赖安装：
  pip install numpy          # 可选，用于 frame_grab_numpy()

运行方法：
  python example_basic.py
  python example_basic.py rtsp://192.168.1.100:8554/cam
"""

import sys
import time
import threading
from typing import Optional

# ── 把 SDK python 目录加入搜索路径（从 SDK 目录树直接运行时需要）──────────
# 目录布局：<sdk>/examples/python/example_basic.py，<sdk>/python/spwcam，<sdk>/bin
# spwcam 包会自动从 <sdk>/bin 加载 spwcam_sdk.dll 及其全部依赖；
# 若 DLL 放在其他位置，运行前设置环境变量 SPWCAM_SDK_BIN=<bin 目录>。
import os
_THIS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_THIS, "..", "..", "python"))

from spwcam import Camera, CameraError
import spwcam as sp

RTSP_URL = sys.argv[1] if len(sys.argv) > 1 else "rtsp://192.168.1.100:8554/YSTech-TURBIDCAM-0001"

# ── 帧计数器（线程安全）─────────────────────────────────────────────────────
_frame_lock  = threading.Lock()
_frame_count = 0


# ════════════════════════════════════════ 回调函数 ══════════════════════════

def on_log(level: int, msg: str) -> None:
    """日志回调：将 SDK 内部日志输出到控制台"""
    tags = {
        sp.SPWCAM_LOG_DEBUG: "[DBG]",
        sp.SPWCAM_LOG_INFO:  "[INF]",
        sp.SPWCAM_LOG_WARN:  "[WRN]",
        sp.SPWCAM_LOG_ERROR: "[ERR]",
    }
    print(f"{tags.get(level, '[???]')} {msg}")


def on_event(ev_type: int, data: str) -> None:
    """事件回调：设备上下线、录像状态、截图完成等"""
    names = {
        sp.SPWCAM_EVENT_DEVICE_DISCOVERED: "DEVICE_DISCOVERED",
        sp.SPWCAM_EVENT_DEVICE_LOST:       "DEVICE_LOST",
        sp.SPWCAM_EVENT_STREAM_CONNECTED:  "STREAM_CONNECTED",
        sp.SPWCAM_EVENT_STREAM_LOST:       "STREAM_LOST",
        sp.SPWCAM_EVENT_RECORD_STARTED:    "RECORD_STARTED",
        sp.SPWCAM_EVENT_RECORD_STOPPED:    "RECORD_STOPPED",
        sp.SPWCAM_EVENT_RECORD_FAILED:     "RECORD_FAILED",
        sp.SPWCAM_EVENT_SNAPSHOT_SAVED:    "SNAPSHOT_SAVED",
    }
    print(f"[EVT] {names.get(ev_type, ev_type):<22}  {data}")


def on_frame(frame_ptr) -> None:
    """帧推送回调：由 SDK 内部线程调用，约 25fps。

    注意：frame_ptr 只在本次回调中有效，不可在函数返回后使用。
    如需保留帧数据，请在此处复制 frame_ptr.contents.data。
    """
    global _frame_count
    with _frame_lock:
        _frame_count += 1
        cnt = _frame_count

    f = frame_ptr.contents
    if cnt % 25 == 0:   # 每 ~1 秒打印一次
        print(f"[FRM] #{cnt:<6}  seq={f.sequence:<8}  {f.width}x{f.height}")


# ════════════════════════════════════════ 主程序 ════════════════════════════

def main() -> int:
    print("=" * 60)
    print(" SPWater Camera SDK  Python 示例")
    print(" 渊视科技 | 舟山渊视科技有限公司")
    print(f" SDK 版本: {Camera.version()}")
    print("=" * 60)
    print()

    cam = Camera()

    # ── 1. 初始化 ─────────────────────────────────────────────────────────
    cam.init(log_level=sp.SPWCAM_LOG_INFO)
    cam.on_log(on_log)
    cam.on_event(on_event)
    cam.on_frame(on_frame)
    print("[OK] SDK 初始化成功\n")

    # ── 2. 设备发现 ───────────────────────────────────────────────────────
    print("[>>] 开始设备发现...")
    cam.discovery_start()

    dev = cam.wait_for_device(timeout_s=8.0)
    devices = cam.all_devices()
    print(f"[OK] 发现 {len(devices)} 台设备")
    for d in devices:
        print(f"     SN   : {d.sn}")
        print(f"     IP   : {d.ip}")
        print(f"     RTSP : {d.rtsp_url}")
        print()

    # ── 3. 相机参数设置 ───────────────────────────────────────────────────
    if devices:
        d = devices[0]
        cam.set_camera_params(d.sn, exposure_us=20000, gain_db=7.5)
        cam.set_led(d.sn, enable=True)
        cam.set_trigger_mode(d.sn, hardware=False)
        print("[>>] 相机参数已设置  曝光=20000µs  增益=7.5dB  LED=ON\n")

    # ── 4. 连接码流 ───────────────────────────────────────────────────────
    print(f"[>>] 连接码流: {RTSP_URL}")
    cam.stream_open(RTSP_URL)

    if not cam.wait_for_stream(timeout_s=15.0):
        print("[ERR] 码流未能进入 RUNNING 状态", file=sys.stderr)
        cam.deinit()
        return 1
    print("[OK] 码流运行中\n")

    # ── 5. 推送回调接收帧 5 秒 ────────────────────────────────────────────
    print("[>>] 推送回调接收帧 5 秒...")
    time.sleep(5)
    with _frame_lock:
        cnt = _frame_count
    print(f"[OK] 共收到 {cnt} 帧\n")

    # ── 6. NumPy 拉取模式（需要 numpy）───────────────────────────────────
    try:
        import numpy as np
        arr = cam.frame_grab_numpy()   # 返回 H×W×4  BGRA uint8 数组，或 None
        if arr is not None:
            print(f"[OK] NumPy 帧: shape={arr.shape}  dtype={arr.dtype}")

            # 转换为 BGR（OpenCV 格式）并保存示例
            bgr = arr[:, :, :3]         # 丢弃 alpha 通道 → H×W×3 BGR
            # import cv2
            # cv2.imwrite("grab.png", bgr)

            # 计算亮度均值（简单图像质量检验）
            brightness = float(arr[:, :, :3].mean())
            print(f"     平均亮度: {brightness:.1f} / 255.0")
        else:
            print("[WARN] 没有新帧可拉取")
    except ImportError:
        print("[INFO] numpy 未安装，跳过 NumPy 帧测试")
    print()

    # ── 7. 录像配置与录像 ──────────────────────────────────────────────────
    # 输出目录：当前工作目录下的 spwcam_record / spwcam_snapshot（可改为任意绝对路径）
    cam.record_set_options(
        video_dir       = os.path.join(os.getcwd(), "spwcam_record"),
        snapshot_dir    = os.path.join(os.getcwd(), "spwcam_snapshot"),
        container       = sp.SPWCAM_CONTAINER_MP4,
        snapshot_fmt    = sp.SPWCAM_IMAGE_PNG,
    )

    print("[>>] 开始录像（3 秒后截图，再等 2 秒后停止）...")
    try:
        cam.record_start()
    except CameraError as e:
        print(f"[WARN] record_start 失败: {e}")
    else:
        time.sleep(3)

        print("[>>] 截图...")
        try:
            cam.snapshot()   # 异步，路径通过 SNAPSHOT_SAVED 事件返回
        except CameraError as e:
            print(f"[WARN] snapshot 失败: {e}")

        time.sleep(2)

        print("[>>] 停止录像...")
        cam.record_stop()
        time.sleep(1)        # 停止是异步的：等待 RECORD_STOPPED 事件 / 文件关闭

        st = cam.record_status()
        print(f"[OK] 录像已停止  分段={st.segment_index}"
              f"  总时长={st.total_elapsed_ms} ms")

    # ── 8. 清理 ────────────────────────────────────────────────────────────
    print()
    print("[>>] 关闭码流...")
    cam.stream_close()

    print("[>>] 释放 SDK...")
    cam.deinit()

    print("\n[DONE] 程序正常退出")
    return 0


if __name__ == "__main__":
    sys.exit(main())
