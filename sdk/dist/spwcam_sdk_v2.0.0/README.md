# SPWater Camera SDK  V2.0

**作者：渊视科技**  
**版权所有：舟山渊视科技有限公司**  
**Copyright © Zhoushan Yuanshi Technology Co., Ltd. All rights reserved.**

---

## 简介

SPWater Camera SDK 是渊视相机的 Windows x64 C/C++/Python 开发工具包，封装了设备发现、参数控制、RTSP 拉流、帧获取、录像和截图等核心功能，以稳定的 C ABI 为 DLL 边界，同时提供 C++17 RAII 封装和 Python ctypes 绑定。

> 本 SDK 基于现有上位机核心代码（rtspviewerqt、videorecorder、udpserver）通过适配层封装，不修改原始源码逻辑。

---

## SDK 目录结构

```
sdk/
├── include/spwcam/
│   ├── spwcam.h          # 公开 C API（DLL 边界接口）
│   └── spwcam.hpp        # C++17 RAII 封装
├── src/                  # SDK 内部实现（无需阅读，仅供构建）
├── python/spwcam/        # Python ctypes 包
│   ├── __init__.py
│   ├── _ctypes_wrap.py
│   └── camera.py         # 高层 Python API
├── examples/
│   ├── c/                # C 示例
│   ├── cpp/              # C++ 示例
│   └── python/           # Python 示例
├── tests/                # ABI 测试 + 初始化测试
├── docs/
│   ├── build.md          # 构建手册
│   ├── api_reference.md  # API 参考
│   ├── usage.md          # 使用指南
│   └── troubleshooting.md
└── scripts/
    ├── build_sdk.bat     # 一键构建
    └── package_sdk.bat   # 打包交付
```

---

## 快速上手

### 环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10/11 x64 |
| 编译器 | Visual Studio 2019/2022（MSVC x64） |
| CMake | ≥ 3.22 |
| Qt | 6.4.x 或 6.5.x，MSVC x64 |
| GStreamer | 1.22.x，MSVC x86_64 |
| FFmpeg | 8.0，shared build |
| Python（可选） | 3.8+ |

---

### 一键构建

```bat
:: 1. 按实际路径修改 scripts\build_sdk.bat 顶部三个变量
::    QT_ROOT、GST_ROOT、FFMPEG_ROOT

:: 2. 在"x64 Native Tools Command Prompt for VS 2022"中运行
cd D:\SPwater_CODE\SPW_cameraControlSystem\sdk
scripts\build_sdk.bat
```

构建产物位于 `build\bin\Release\`：
- `spwcam_sdk.dll` — 可分发 DLL
- `spwcam_sdk.lib` — 链接时导入库（位于 `build\lib\Release\`）

随后运行 `scripts\package_sdk.bat` 生成自包含的分发包 `dist\spwcam_sdk_v2.0.0\`（脚本末尾用 dumpbin 做依赖闭包检查，缺任何非系统 DLL 即失败）。

---

### C 语言调用（5 分钟入门）

```c
#include "spwcam/spwcam.h"

/* 回调必须是普通 C 函数 */
static void on_log(spwcam_log_level_t lvl, const char* msg, void* ud) { printf("[%d] %s\n", lvl, msg); }

/* 1. 初始化 */
spwcam_init_params_t p = {0};
p.struct_size = sizeof(p);
p.log_level   = SPWCAM_LOG_INFO;
spwcam_context_t ctx = spwcam_init(&p);

/* 2. 注册日志回调 */
spwcam_set_log_callback(ctx, on_log, NULL);

/* 3. 设备发现（等待 5 秒） */
spwcam_discovery_start(ctx, 0, 0);
Sleep(5000);

/* 4. 查询第 0 台设备 */
spwcam_device_info_t di = {sizeof(di)};
spwcam_get_device_info(ctx, 0, &di);

/* 5. 开启码流 */
char url[256];
snprintf(url, sizeof(url), "rtsp://%s:%u%s", di.ip, di.rtsp_port, di.rtsp_path);
spwcam_stream_open(ctx, url, 0);

/* 6. 注册帧回调（BGRA32，约 25fps；f->data 仅在回调期间有效） */
spwcam_set_frame_callback(ctx, on_frame, NULL);   /* on_frame: static void on_frame(const spwcam_frame_t*, void*) */

Sleep(5000);  /* 接收 5 秒 */

/* 7. 录像 */
spwcam_record_options_t opts = {sizeof(opts)};
strncpy(opts.video_dir,    "spwcam_record",   sizeof(opts.video_dir)-1);   /* 建议使用绝对路径 */
strncpy(opts.snapshot_dir, "spwcam_snapshot", sizeof(opts.snapshot_dir)-1);
opts.container = SPWCAM_CONTAINER_MP4;   /* fps/码率/分段在 v2.x 固定为 25/8000/30 分钟 */
spwcam_record_set_options(ctx, &opts);
spwcam_record_start(ctx);           /* 异步：RECORD_STARTED 事件携带文件路径 */
Sleep(10000);
spwcam_snapshot(ctx);               /* 异步：SNAPSHOT_SAVED 事件携带文件路径 */
spwcam_record_stop(ctx);            /* 异步：RECORD_STOPPED 事件表示文件已关闭 */

/* 8. 清理 */
spwcam_stream_close(ctx);
spwcam_deinit(ctx);
```

> 完整示例见 `examples/c/example_basic.c`

---

### C++17 调用

```cpp
#include "spwcam/spwcam.hpp"
using namespace std::chrono_literals;

spwcam::Camera cam;
cam.init();

cam.on_log([](spwcam_log_level_t lvl, std::string msg){
    std::cout << msg << "\n";
});
cam.on_event([](spwcam_event_type_t ev, std::string data){
    std::cout << "event " << ev << ": " << data << "\n";
});

// 设备发现
cam.discovery_start();
for (int i = 0; i < 32 && cam.device_count() == 0; ++i)
    std::this_thread::sleep_for(250ms);

for (auto& d : cam.all_devices())
    std::cout << d.sn << "  " << d.rtsp_url() << "\n";

// 码流 + 帧回调
cam.stream_open("rtsp://192.168.1.100:8554/cam");
cam.wait_for_stream(15.0);

cam.on_frame([](const spwcam_frame_t& f){
    // f.data: BGRA32, f.width x f.height
});

std::this_thread::sleep_for(5s);

// 录像
cam.record_set_options("spwcam_record", "spwcam_snapshot");
cam.record_start();
std::this_thread::sleep_for(10s);
cam.snapshot();
cam.record_stop();

cam.stream_close();
// cam 析构时自动 deinit()
```

> 完整示例见 `examples/cpp/example_raii.cpp`

---

### Python 调用

```python
from spwcam import Camera
import time

cam = Camera()
cam.on_log(lambda lvl, msg: print(f"[{lvl}] {msg}"))
cam.init(log_level=0)  # 0=DEBUG
cam.on_event(lambda ev, data: print(f"event {ev}: {data}"))

# 设备发现
cam.discovery_start()
dev = cam.wait_for_device(timeout_s=8)
if dev:
    print(f"发现设备 SN={dev.sn}  RTSP={dev.rtsp_url}")
    cam.set_camera_params(dev.sn, exposure_us=20000, gain_db=7.5)

# 连接码流
cam.stream_open("rtsp://192.168.1.100:8554/cam")
cam.wait_for_stream(15)

# NumPy 帧（BGRA32）
import numpy as np
arr = cam.frame_grab_numpy()      # H × W × 4, uint8
if arr is not None:
    bgr = arr[:, :, :3]           # 转 BGR（OpenCV 格式）

# 录像
cam.record_set_options("spwcam_record", "spwcam_snapshot")
cam.record_start()
time.sleep(10)
cam.snapshot()
cam.record_stop()

cam.stream_close()
cam.deinit()
```

**安装 Python 包：**
```bat
cd sdk\dist\spwcam_sdk_v2.0.0\python
pip install -e .
```

> 完整示例见 `examples/python/example_basic.py`

---

## API 速查

### 错误码

| 常量 | 值 | 含义 |
|------|----|------|
| `SPWCAM_OK` | 0 | 成功 |
| `SPWCAM_ERR_INVALID_PARAM` | -1 | 参数无效 |
| `SPWCAM_ERR_NOT_INITIALIZED` | -2 | 未初始化 |
| `SPWCAM_ERR_NOT_OPEN` | -4 | 码流未开启 |
| `SPWCAM_ERR_NO_FRAME` | -9 | 无新帧（非真正错误） |

### 核心函数

```c
/* 生命周期 */
spwcam_context_t  spwcam_init(const spwcam_init_params_t* params);
void              spwcam_deinit(spwcam_context_t ctx);

/* 设备发现 */
int  spwcam_discovery_start(ctx, discover_port, heartbeat_port);
int  spwcam_get_device_count(ctx);
int  spwcam_get_device_info(ctx, index, spwcam_device_info_t*);

/* 相机控制 */
int  spwcam_set_camera_params(ctx, sn, exposure_us, gain_db);
int  spwcam_set_led(ctx, sn, enable);
int  spwcam_set_trigger_mode(ctx, sn, mode);
int  spwcam_set_ip(ctx, sn, new_ip, mask);

/* 码流 */
int  spwcam_stream_open(ctx, rtsp_url, latency_ms);
int  spwcam_stream_close(ctx);
spwcam_stream_status_t  spwcam_stream_get_status(ctx);

/* 帧 */
void spwcam_set_frame_callback(ctx, callback, user_data);  /* 推送 */
int  spwcam_frame_grab(ctx, &frame);                       /* 拉取 */
void spwcam_frame_free(frame);

/* 录像 */
int  spwcam_record_set_options(ctx, spwcam_record_options_t*);
int  spwcam_record_start(ctx);
int  spwcam_record_stop(ctx);
int  spwcam_record_get_status(ctx, spwcam_record_status_t*);
int  spwcam_snapshot(ctx);
```

> 完整参数说明见 `docs/api_reference.md`

---

## 帧格式

所有帧以 **BGRA32**（4字节/像素，B G R A 顺序）输出，分辨率 1920×1080，约 25fps。

```c
// C: 访问像素
const uint8_t* row = frame->data + y * frame->stride;
uint8_t B = row[x*4+0], G = row[x*4+1],
        R = row[x*4+2], A = row[x*4+3];

// C++: 转 OpenCV
cv::Mat mat(frame.height(), frame.width(), CV_8UC4,
            const_cast<uint8_t*>(frame.data()), frame.stride());
cv::Mat bgr;
cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);

// Python + NumPy
arr = cam.frame_grab_numpy()   # shape: (H, W, 4)  dtype: uint8
bgr = arr[:, :, :3]            # → (H, W, 3)  BGR
```

---

## 运行时依赖

应用程序目录（或系统 PATH）中须包含以下 DLL：

```
spwcam_sdk.dll
Qt6Core.dll  Qt6Network.dll  Qt6Gui.dll
gstreamer-1.0.dll  gstapp-1.0.dll  gstbase-1.0.dll
gstrtsp-1.0.dll  gstsdp-1.0.dll  gstvideo-1.0.dll
glib-2.0.dll  gobject-2.0.dll
avcodec-62.dll  avformat-62.dll  avutil-60.dll
swscale-9.dll  swresample-6.dll
```

---

## 注意事项

1. **单路限制**：SDK 同一个 context 仅支持一路 RTSP，不支持多路并发。
2. **UI 无关**：SDK 不包含任何 QML/窗口/语言切换功能。
3. **录像前提**：`spwcam_record_start()` 要求码流处于 `RUNNING` 状态。
4. **帧生命周期**：推送回调中的 `frame` 指针仅在回调期间有效；拉取的帧必须调用 `spwcam_frame_free()` 释放。
5. **线程安全**：所有 API 函数可从任意线程调用；回调在 SDK 内部 Qt 线程上执行且不持有任何 SDK 锁，**允许**在回调内调用 SDK 函数，但回调应尽快返回（回调执行期间不会分发帧和事件）。
6. **异步语义**：`stream_open` / `record_start` / `record_stop` / `snapshot` 均立即返回，结果通过事件回调（`STREAM_CONNECTED` / `RECORD_STARTED` / `RECORD_STOPPED` / `SNAPSHOT_SAVED`）通知。
7. **端口独占**：SDK 与桌面程序不能同时监听 UDP 7776/8888；被占用时 `spwcam_discovery_start` 返回 `SPWCAM_ERR_IO`。
8. **Python**：需 Python 3.8+；`spwcam_sdk.dll` 从 SDK 的 `bin\` 目录加载（不要把 DLL 单独拷进 Python 包），非标准布局请设置环境变量 `SPWCAM_SDK_BIN`。
9. **第三方组件许可证（对外分发前必须处理）**：当前打包脚本引用的 FFmpeg 为 GPL 构建（`--enable-gpl --enable-libx264`），录像 H.264 编码实际落在 libx264 上；闭源分发该组合违反 GPL。对外发布前需替换为 LGPL 构建的 FFmpeg（`--disable-gpl`）并将编码器改为 `h264_mf` / `h264_nvenc` / `libopenh264`，或取得商业授权。`openh264-7.dll` 的自行再分发涉及 Cisco 专利费条款，请法务评估。Qt 6（LGPL v3）动态链接可用，需随包附带许可证声明。

---

## 联系方式

**舟山渊视科技有限公司**  
开发者：渊视科技  

---

*本文档及 SDK 代码受版权法保护，未经书面授权不得复制或分发。*
