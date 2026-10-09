# SPW_cameraControlSystem — 工程上下文记录

## 工程目标
上位机只负责：接收 RTSP 流、解码、显示、录像、设备控制。
相机已在硬件侧完成色彩调整并直接输出 1920x1080，上位机不做任何二次处理。

## 禁止项
- 禁止重新引入 ColorTuneWorker 或任何色彩调整逻辑（Lab/HSV/RGB 校正、白平衡、CCM、gamma）
- 禁止对已是 1920x1080 的图像做额外 resize / letterbox / 补黑边 / 画布拼接
- 禁止在录像路径保存经过二次缩放的图像
- 禁止在 UI 线程做重型图像处理

## 常量定义（集中位置）
```cpp
// mainwindow.cpp 顶部（或独立头文件）
constexpr int CAMERA_W   = 1920;
constexpr int CAMERA_H   = 1080;
constexpr int CAMERA_FPS = 25;
```

## 修改记录

---
### [2026-05-09] 第一阶段：建立上下文记录
**修改目标：** 创建 CLAUDE.md，记录工程目标、禁止项、修改格式。
**涉及文件：** CLAUDE.md（新建）
**具体改动：** 无代码改动，仅建立记录文件。
**编译结果：** N/A
**运行验证：** N/A
**风险点：** 无
**下一步：** 第二阶段——彻底剔除 ColorTuneWorker

---
### [2026-05-09] 第二阶段：彻底剔除 ColorTuneWorker + 第三阶段：去掉 letterbox/resize
**修改目标：**
1. 从编译系统和所有引用中剔除 ColorTuneWorker。
2. 帧路径短路：takeLatestFrameIfNew() 直接 → 显示 + 录像，不再经过 ColorTuneWorker 线程。
3. 删除 videorecorder.cpp 中的 letterboxTo1080pRGB888 函数及所有调用点。
4. 录像/截图直接使用原始帧（相机已输出 1080P）。

**涉及文件：**
- SPW_cameraControlSystem.pro（移除 colortuneworker.h/cpp）
- mainwindow.h（删除 ColorTuneWorker 相关成员、信号、槽、函数声明）
- mainwindow.cpp（删除 ColorTuneWorker 线程启停、短路帧路径）
- videorecorder.cpp（删除 letterboxTo1080pRGB888 及调用）

**具体改动：** 见各文件 diff。
**编译结果：** 代码修改完成，需在 Qt Creator 中执行 qmake clean + rebuild 验证（release/ 下旧 moc 文件会自动重新生成）
**运行验证：** 待用户编译后验证
**风险点：** 帧路径改变后需确认显示和录像均正常。
**下一步：** 第四~六阶段优化与验证。

---
### [2026-05-09] 第四~五阶段：图像链路优化
**修改目标：**
1. 删除 mainwindow.cpp 中注释死代码块（约 30 行）。
2. videorecorder.cpp：`openEncoderLockedForImage` 改为从首帧读取实际尺寸，不再硬编码 1920x1080。
3. videorecorder.cpp：sws 输入格式从 `AV_PIX_FMT_RGB24` 改为 `AV_PIX_FMT_BGRA`，`encodeImageLocked` 省掉每帧 `convertToFormat(RGB888)`，直接使用 ARGB32（内存布局与 BGRA 相同）。
4. videorecorder.cpp：默认 FPS 从 22 改为 25（与相机一致）。
5. rtspviewerqt.cpp：PERF 统计日志用 `#ifndef QT_NO_DEBUG` 包裹，Release 模式不输出。

**涉及文件：** mainwindow.cpp、videorecorder.cpp、rtspviewerqt.cpp
**编译结果：** 通过（两轮，第一轮修复了 debug 宏边界问题）
**运行验证：** 录像颜色正确，显示正常。
**风险点：** 无。
**下一步：** 工程优化完成。

---
### [2026-05-29] 录像自动分段 + 断流自动保存
**修改目标：**
1. 录像超过 30 分钟自动切换新文件，不中断录制。
2. 网络断流时若正在录像，自动触发停止并保存当前文件。

**涉及文件：**
- videorecorder.h（新增 `kMaxSegmentMs = 30*60*1000` 常量）
- videorecorder.cpp（`receiveFrame2Record` 里检测 `lastPtsMs_ >= kMaxSegmentMs`，flush 旧文件后立即开新文件）
- mainwindow.cpp（`onCheckDeviceAlive` 断流分支里，若 `isRecording_` 则调用 `on_action_stopRecord_triggered()`）

**编译结果：** 通过
**运行验证：** 测试通过
**风险点：** 无

---
### [2026-06-24] V4.2.5 硬件触发状态闭环 + 系统日志告警
**修改目标：**
1. 硬件触发切换严格状态闭环：点击后锁定开关，等待下位机 TRIGGER_STATUS 确认，收到 fallback 自动退回软件触发，5秒超时保护。
2. 修复 TRIGGER_STATUS 仅在 port 8888 (HB socket) 到达时被丢弃的问题（onReadyReadHb 补充 emit datagramReceived）。
3. 修复硬件触发切换期间视频中断弹窗误弹（WaitingAck 状态下抑制）。
4. 系统日志区域橙红色加粗告警：fallback 时输出【硬件触发不可用】，超时时输出【硬件触发状态未知】。
5. datagramReceived lambda 改为 appendLog 输出 [UDP_RAW]，确保 JSON 包是否到达在 UI 可见。

**涉及文件：** uicontroller.h/cpp、mainwindow.h/cpp、qml/Main.qml、udpserver.cpp
**编译结果：** 通过
**运行验证：** 测试通过
**风险点：** 无

---
### [2026-06-24] V4.2.6 跨网段改IP链路打通 + ACK 状态闭环
**修改目标：**
1. `sendSetIp` 增加本地路由可达性检查：遍历所有网卡，判断是否有接口与目标设备同子网；若无直连路由则输出 warning 日志并补发 `255.255.255.255` 广播（Windows 下走所有网卡，确保同二层设备可收到）。
2. `onReadyRead` 新增 `CMD_SET_IP_ACK` 解析，emit `setIpAckReceived(sn, status)` 信号。
3. `mainwindow.cpp` 新增 `onSetIpAckReceived` 槽：`accepted` → 延长超时至20秒并更新 UI；`success` → 直接完成；`failed` → 直接报错。
4. `onIpChangeTimeout` 按 `ipAckAccepted_` 区分 `no_ack` 和 `reconnect_timeout` 两种超时原因。
5. 全链路新增 `[IPCFG-PC]` 诊断日志，覆盖本地网卡选择、路由警告、发送结果、ACK 接收、最终状态。

**涉及文件：** udpserver.h、udpserver.cpp、mainwindow.h、mainwindow.cpp
**编译结果：** 通过
**运行验证：** 测试通过
**风险点：** 无

---
### [2026-07-08] 长时拉流卡顿治理：内存碎片化 + 队列背压 + 编码降频
**修改目标：** 排查并修复「RTSP 连续拉流 10+ 小时后画面延迟/卡顿/越来越慢」。按概率定位到三类根因并修复。

1. **UI 热路径每帧堆分配（主因，内存碎片化）**
   - 原 overlay 叠字每帧对 1080p ARGB32（约 8MB）做深拷贝，录像开启时每帧分配两次（约 16MB），25fps 下每秒 ~400MB alloc/free，长时导致 Windows 堆碎片化、malloc 延迟劣化。
   - 新增 `applyOverlayInto(dst, src, text)`：复用外部缓冲 memcpy + 叠字，替代每帧 `src.copy()`。
   - 显示走双缓冲、录像走双缓冲（`use_count==1` 判定避免跨线程覆盖，占用时降级临时分配）；截图为冷路径保持单次分配。稳态下 UI 线程每帧堆分配降为零。

2. **GStreamer 队列无界背压（次因，延迟累积）**
   - 两条中间 queue 由 `max-size-buffers=0 leaky=no`（无上限+满则反压上游）改为 `max-size-buffers=4 leaky=downstream`（限深+丢旧不反压），消除网络抖动→解码变慢→积压→背压→rtspsrc 停拉的恶性循环。
   - `kDropOnLatency` 由 `false` 改 `true`，防止 jitter buffer 延迟单调增长。
   - 帧池 3 槽扩到 5 槽；选槽跳过消费者仍持有（`use_count>1`）的槽，全占用则临时分配，消除写读竞争导致的偶发花屏。

3. **录像全 I 帧编码 CPU 长时降频**
   - `gop_size` 由 `1`（全 I 帧）改为 `25`，大幅降低 1080p@25 编码 CPU 负荷，避免热累积降频拖慢录像线程。

**涉及文件：** rtspviewerqt.cpp、mainwindow.cpp、mainwindow.h、videorecorder.cpp
**编译结果：** 待 Qt Creator qmake clean + rebuild 验证（Qt 5.15.2；用到 sizeInBytes/constBits/use_count 均兼容）
**运行验证：** 待长时压测（建议 ≥12h）：进程提交内存应平稳不增；`[PERF]` 日志 p99/gt120/stall_max 长时稳定不攀升；录像回放时长正确无变慢。
**风险点：**
- 帧池临时分配仅在消费者持续落后时触发，属降级保护，不影响正确性。
- `gop_size=25` 在传输端丢包时最坏一个 GOP（1 秒）内可能残留马赛克；马赛克根因在传输而非编码，观感应无明显变化；若要求录像零马赛克可折中回退（如 12）。
- 录像双缓冲依赖 `use_count` 为保守估计，只会多分配、不会误覆盖，线程安全。
**下一步：** 长时压测确认后，视情况优化 `calcNextPullIntervalMs` 自适应降速的轻微正反馈。

---

### [2026-08-04] V4.2.12 新增相机曝光时间控制
**修改目标：**
1. 在右侧状态面板新增曝光时间滑块（range 10000~30000 µs，step 1000），操作方式与增益（亮度）一致。
2. 曝光滑块右侧不显示数值，改用 短/较短/中/较长/长 五档描述文字，实际发送数值不变。
3. 接入多语言切换：标签 "曝光时间" 及五档文字均使用 `qsTr()`，语言切换时自动 retranslate。

**涉及文件：**
- `uicontroller.h`（新增 `exposureUs` Q_PROPERTY、`setExposureUs` 槽、`exposureUsChanged` 信号、`exposureUs_` 成员，默认 20000）
- `mainwindow.cpp`（`brightnessChanged` lambda 补传 `exposureUs`；新增 `exposureUsChanged` lambda 触发发送）
- `qml/Main.qml`（亮度滑块下方新增曝光滑块；右侧显示五档标签；版本号 → V4.2.12）
- `translations/app_en_US.ts`（`Main` context 新增：Exposure / Short / Short+ / Medium / Long+ / Long）
- `translations/app_ko_KR.ts`（`Main` context 新增：노출 시간 / 짧게 / 약간 짧게 / 보통 / 약간 길게 / 길게）

**编译结果：** 待 Qt Creator qmake → Rebuild + lrelease 验证
**运行验证：** 连接设备后拖动曝光滑块，日志区应出现 `CMD_SET_CAMERA exposure_us=xxxxx` 发送记录；切换语言后标签同步变化
**风险点：** 无

---

### [2026-09-03] V4.3 视频区全屏切换按钮 + 全屏态拖动/四角缩放窗口
**修改目标：**
1. 视频显示区右上角新增全屏切换按钮，点击后视频 widget 铺满整个主界面（标题栏/工具栏/侧栏/日志区全部被覆盖），图像比例不变；再次点击恢复原布局。
2. 全屏态下按 Esc 亦可恢复。
3. 全屏态下标题栏被视频盖住，改为在视频上左键拖动移动窗口：仅在图像未放大（zoom==1，此时左键本无平移功能）时生效；已放大时左键拖动仍为平移图像。与标题栏一致，最大化状态下不移动。
4. 全屏态下鼠标拖拽视频四角（16px 热区）缩放窗口：左/上侧的角同时移动窗口位置，固定对角不动；最小尺寸 640×400 与原右下角 resize 一致；悬停四角时光标变为对角箭头；四角判定优先于窗口拖动。

**实现要点：**
- 视频是原生 `ZoomPanImageView`（QWidget）叠在 QQuickWidget 之上，QML 内绘制的按钮会被遮挡，因此按钮做在 C++ 侧，作为视频 widget 的子 `QToolButton`，始终浮在图像上方。
- `HudWindow::syncVideoGeometry()` 增加全屏分支：`videoFullscreen_` 为真时 `setGeometry(rect())`，否则照旧对齐 QML `videoArea`。
- `ZoomPanImageView::paintEvent` 本身按 fit 比例绘制（`sFit = min(w/iw, h/ih)`），铺满窗口后自动保持比例，无需改动绘制逻辑；不涉及任何帧数据处理。
- 图标由 `QPainter` 程序绘制（四角括号，expand 朝外 / restore 朝内），配色 `#00cc88` / hover `#00ff99`，与 HUD 风格一致。
- 对视频 widget 安装 eventFilter：`Resize` → 重排按钮位置；全屏态 `Esc` → 退出全屏；全屏态左键 Press/Move/Release → 先用 `cornerAt()` 判定四角热区，命中则按 globalPos 增量 `setGeometry()` 缩放窗口（左/上角移动 left/top 边，最小尺寸只回收移动边）；未命中则通过 `qobject_cast<ZoomPanImageView*>` 读取 `zoom()`，未放大时 `move()` 窗口。悬停仅在无按键时接管光标，不干扰平移手形光标；退出全屏时复位全部状态。进入全屏时把焦点交给视频 widget，保证 Esc 直接可用。
- 按钮边距 8 → 18，避开右上角缩放热区。

**涉及文件：**
- `hudwindow.h`（新增 `setVideoFullscreen/isVideoFullscreen`、`eventFilter`、`layoutFullscreenButton`、`updateFullscreenButtonUi`；成员 `fsBtn_`、`videoFullscreen_`）
- `hudwindow.cpp`（`embedVideoWidget` 创建按钮并安装过滤器；`syncVideoGeometry` 全屏分支；新增上述函数与图标绘制辅助）
- `translations/app_zh_CN.ts`、`app_en_US.ts`、`app_ko_KR.ts`、`app_nl_NL.ts`（新增 `HudWindow` context：全屏显示 / 恢复布局 (Esc)）
- `qml/Main.qml`（版本号 → V4.3）

**编译结果：** 通过（MSVC2019_64 + Qt 5.15.2 Release，jom 全量构建，exe 已重新链接；lrelease 106 条全部 finished）。第一轮修复了 `QPolygonF` 在 Qt 5.15 下不支持初始化列表构造的问题。
**运行验证：** 待用户验证：拉流后点击视频区右上角按钮 → 视频铺满窗口且比例正确；再点击或按 Esc → 恢复；全屏态未放大时左键拖动视频 → 窗口跟随移动；滚轮放大后左键拖动 → 平移图像（不移动窗口）；全屏态鼠标移到四角光标变对角箭头，拖动 → 窗口缩放、视频随之铺满、比例不变，缩到 640×400 不再变小。
**风险点：**
- 全屏态下 QML 的 Toast / IP 等待遮罩会被原生视频 widget 盖住，不可见（恢复布局后正常）。
- 全屏态覆盖了自绘标题栏，最小化/关闭需先恢复布局。
- 按钮 tooltip 用 C++ `tr()`，运行时切换语言后需切换一次全屏状态才刷新文案（QML 部分不受影响）。

---

### [2026-09-07] SDK v1.0.0 严格审查 + 阻断级缺陷修复（sdk/ 目录，主工程源码未改动）
**修改目标：** 审查 `sdk/`（第三方 C/C++/Python SDK）发现 7 项阻断级、9 项严重问题，全部修复并以洁净环境（PATH 仅系统目录）+ 本地 gst-rtsp-server 1080p25 流做端到端回归。

**核心修复：**
1. **打包缺 9 个 GStreamer 依赖 DLL**（gstaudio/gsttag/z-1/gstd3d11/gstd3dshader/gstdxva/gstd3d12/gstcuda/gstgl）→ 洁净机器 13 个插件加载失败、完全无法拉流。`package_sdk.bat` 补齐，新增 `check_deps.ps1` 用 dumpbin 做递归依赖闭包门禁，缺失即打包失败。
2. **录像状态机死循环**：`pumpFrame` 仅在 `rec.recording` 为真时投帧，而该标志要等编码器打开（需首帧）后才置真。新增 `recordRequested_` 标志驱动投帧；`rec.recording` 仅作对外状态。
3. **截图与帧泵竞争同一 `takeLatestFrameIfNew()`**（实测 10 次仅 4 张）。worker 缓存最近一帧 `lastFrame_` 供截图；无帧时 `spwcam_snapshot` 返回 `SPWCAM_ERR_NO_FRAME` 而非 OK。
4. **回调内调 SDK API 直接崩溃（0xC0000409）**：`sdk_log/sdk_event` 持 `cbs.lock` 期间调用用户回调 → 同线程重入 std::mutex → terminate。改为锁内拷贝指针、锁外调用；头文件/文档改为"允许回调内调用"。
5. **`spwcam::Camera` move 后崩溃（0xC0000005）**：回调 `user_data` 绑定 `this`。改为堆上 `Handlers` 结构（`unique_ptr`）按地址注册，移动后无需重绑；新增 `wait_for_stream/wait_for_device`、`set_trigger_mode(bool)` 重载。
6. **Python 绑定 Py≥3.8 无法导入**：DLL 副本被放进 `python/spwcam/` 而依赖在 `bin/`。改为从 `<sdk>/bin` 加载 + `os.add_dll_directory`，支持 `SPWCAM_SDK_BIN` 环境变量；打包不再复制 DLL 进包；`setup.py` 去掉 `package_data`。
7. **隐藏 ABI 缺陷（新发现）**：GStreamer `include/` 根目录含 FFmpeg 7.x 头（avcodec 61）且 include 顺序在 FFmpeg 8.0 之前 → `AVCodecContext` 按 61 布局编译、运行加载 62 DLL → 编码器打开失败（"Picture size 1080x0"）。CMake 去掉该根目录、FFmpeg 头优先，加编译期 `static_assert` + 运行期 `avcodec_version()` 校验。
8. 其他：多 context 引用计数（`SdkQtApp::acquire/release`）；deinit 先阻塞停录像再停线程（文件必有 moov）；`discovery_start` bind 失败返回 `ERR_IO`；补齐 6 个从未触发的事件（STREAM_CONNECTED/LOST、DEVICE_LOST、RECORD_SEGMENT、TRIGGER_STATUS、IP_CHANGED）与 `STREAM_ERROR` 状态（5 s 无帧 → ERROR，恢复自动回 RUNNING）；PATH 只前插一次；`gst_plugins` 不存在时不接管 GStreamer 环境（构建树可测）；帧泵 33 ms → 16 ms PreciseTimer（投递帧率 21→25 fps）；公共头纯 ASCII；C++ 示例编译错误；examples CMake 直接链接 target；批处理 LF→CRLF（`^` 续行原本失效）；文档全面修正（latency 实为 350 ms 钳制 [300,600]、录像参数 v1.x 固定、异步语义、线程模型、许可证风险）。

**涉及文件：** `sdk/src/*`（qt_worker.h/cpp 重写、spwcam_core/device/record/internal）、`sdk/include/spwcam/spwcam.h、spwcam.hpp`、`sdk/python/*`、`sdk/examples/*`、`sdk/scripts/build_sdk.bat、package_sdk.bat、check_deps.ps1（新）`、`sdk/tests/test_init.cpp、test_stream.cpp（新）、CMakeLists.txt`、`sdk/CMakeLists.txt`、`sdk/README.md`、`sdk/docs/*`
**编译结果：** `scripts\build_sdk.bat` 通过；ctest 3/3（abi_smoke、init_deinit 39 项、stream_e2e 33 项）；e2e 连跑 5 次稳定。
**运行验证：** `scripts\package_sdk.bat` 依赖闭包 66 DLL 全通过；洁净环境（PATH 仅 System32）用新 dist：拉流 25 fps、RECORD_STARTED/STOPPED、MP4 ffprobe 可解码、截图 10/10、回调内调 API 无崩溃、move 后回调可达、断流→ERROR→恢复→RUNNING、Python 3.8 导入并完整流程、C/C++ 示例编译并运行通过。
**风险点：**
- **许可证（未解决，需决策）**：随包 FFmpeg 为 `--enable-gpl --enable-libx264` 构建，录像编码实际落在 libx264；闭源分发违反 GPL。需换 LGPL 构建并把编码器改为 h264_mf/nvenc/libopenh264，或取得商业授权；openh264 再分发涉及 Cisco 专利费条款。已写入 README 注意事项第 9 条。
- 上游 `VideoRecorder` 分段切换失败时只发 `sendMSG2ui` 不发 `recordingFailed`，SDK 状态会滞留 recording=1（上游限制，未改主工程）。
- `fps/bitrate_kbps/segment_minutes` 仍为上游固定值，SDK 只记录 WARN，文档已明示。
- 需用 RTSP 源的 e2e 测试通过环境变量 `SPWCAM_TEST_RTSP_URL` 启用，未设置时 SKIP。

---

### [2026-09-07] SDK V2.0 自包含分发包
**修改目标：** 版本 1.0.0 → 2.0.0，作者改为"渊视科技"（ASCII 文件中为 Yuanshi Technology）；分发包解压即用。
**涉及文件：** `sdk/CMakeLists.txt`、`include/spwcam/spwcam.h(.hpp)`（`SPWCAM_VERSION_*`）、`python/setup.py`、`src/spwcam_core.cpp`、`examples/*`、`README.md`、`docs/*`、`scripts/package_sdk.bat`（版本号、作者、`bin\` 内附预编译示例 exe、QUICKSTART 增加"解压即试"段落）。
**编译结果：** `build_sdk.bat` 通过，ctest 3/3。
**运行验证：** `package_sdk.bat` 闭包检查 66 DLL 通过 → 产物 `sdk/dist/spwcam_sdk_v2.0.0.zip`（62 MB，86 项）。模拟新电脑：把 zip 解压到空目录、PATH 仅 Windows 系统目录，`bin\example_c_basic.exe`、`bin\example_cpp_raii.exe`、`examples\python\example_basic.py`（Py3.8）三者对本地 RTSP 流均完整跑通（拉流/录像/截图事件齐全），用户程序按 `include\`+`lib\` 编译后 `spwcam_version()` 返回 2.0.0、init 成功。
**风险点：** 许可证事项（仅内部测试，用户已确认不外发）；其余同上一条记录。

---

---

### [2026-10-09] V4.3.1 多语言补全 + 断流弹窗英文硬编码
**修改目标：** 全面修复英文模式下仍出现中文的所有 UI 文字，确保语言切换后所有用户可见文字均跟随切换。

**根因排查结果：**
1. `mainwindow.cpp` 多处 `appendLog` 调用使用裸字符串（无 `tr()`）
2. `videorecorder.cpp` 两处 `QStringLiteral` 直接输出到 UI 日志，未走翻译
3. `qml/RecordStatusPanel.qml` 全部文字硬编码，未用 `qsTr()`
4. `qml/Main.qml` 窗口控制按钮 tooltip 硬编码中文
5. `languagemanager.cpp` 默认语言为 `zh_CN`，且每次切换后写入 QSettings，导致旧安装启动仍显示中文；改为固定启动英文、不再读写 QSettings
6. 四个 `.ts` 文件缺少 `VideoRecorder` context、窗口按钮条目、`"未连接"` 条目

**涉及文件：**
- `mainwindow.cpp`（`"正在连接相机..."` / `"连接超时..."` / `"断开相机连接"` / `"相机连接成功，视频流已建立"` / `"开始录像："` / `"录像已保存："` / `"未连接"` 补 `tr()`）
- `videorecorder.cpp`（两处 `QStringLiteral` 改 `tr()`）
- `themedmessagedialog.h/.cpp`（新增 `changeEvent`，语言切换时刷新 OK 按钮文本；补 `#include <QEvent>`）
- `qml/ChangeIpDialog.qml`（标题/设备信息行/按钮全部改 `qsTr()`）
- `qml/RecordStatusPanel.qml`（全部文字改 `qsTr()`）
- `qml/Main.qml`（窗口按钮 tooltip 改 `qsTr()`）
- `languagemanager.cpp`（`loadSaved()` 固定加载 `en_US`，移除 QSettings 读写，移除 `#include <QSettings>`）
- `translations/app_zh_CN.ts`（补 `UiController`/`ChangeIpDialog`/`VideoRecorder` context，补曝光五档、窗口按钮、`"未连接"` 等条目）
- `translations/app_en_US.ts`（同上，补完整英文翻译）
- `translations/app_ko_KR.ts`（同上，补完整韩文翻译）
- `translations/app_nl_NL.ts`（同上，补完整荷兰文翻译）

**编译结果：** 待 Qt Creator qmake → Rebuild + lrelease 验证
**运行验证：** 启动后界面应为英文；触发超时日志、断流弹窗、IP 修改弹窗、录像状态面板、窗口按钮 tooltip 均应显示英文；切换语言后上述文字同步切换
**风险点：** 无

---

## 代码与商业机密保密规则
**修改目标：**
1. 信息栏"硬件触发状态未知"日志四个语言版本补全翻译。
2. 修改相机IP弹窗文字随语言切换。
3. 断流/设备中断弹窗"确定"按钮随语言切换。

**涉及文件：**
- `qml/ChangeIpDialog.qml`（标题、设备信息行、确定/取消按钮全部改用 `qsTr()`）
- `themedmessagedialog.h`（新增 `changeEvent` 声明）
- `themedmessagedialog.cpp`（实现 `changeEvent`，`LanguageChange` 时刷新 OK 按钮文本）
- `translations/app_zh_CN.ts`（补 `UiController` / `ChangeIpDialog` context；补 V4.2.12 曝光五档条目）
- `translations/app_en_US.ts`（补 `UiController` / `ChangeIpDialog` context）
- `translations/app_ko_KR.ts`（补 `UiController` / `ChangeIpDialog` context）
- `translations/app_nl_NL.ts`（补 `UiController` / `ChangeIpDialog` context）

**编译结果：** 待 Qt Creator qmake → Rebuild + lrelease 验证
**运行验证：** 切换至英文/韩文/荷兰文后：1) 触发超时日志应以目标语言出现；2) 点击"修改IP"弹窗标题/按钮应翻译；3) 断流弹窗"确定"按钮应翻译
**风险点：** 无

---

## 代码与商业机密保密规则

> **最高优先级规则，适用于本工程及后续所有工程。**

1. 本工程及所有工程代码均属于公司内部资产，必须严格保密。
2. Claude 不得将完整源码、核心算法、通信协议细节、工程目录结构、关键实现逻辑、业务数据、客户信息、设备参数、接口密钥、编译配置、部署细节等内容外泄。
3. Claude 不得主动上传、发布、分享、复制到外部平台，或在回复中大段展示源代码。
4. 修改代码时只允许在本地工程内操作。
5. 对外回复或总结时，只能说明：
   - 修改了哪些文件；
   - 修改了哪些函数；
   - 改动目的；
   - 核心逻辑摘要；
   - 编译结果；
   - 测试方法。
6. 除非明确要求，否则不要粘贴完整函数、完整类、完整文件或核心算法代码。
7. 如果必须展示代码，只能给最小必要片段，并隐藏敏感名称、路径、密钥、客户信息和专有协议细节。
8. 所有工程默认按商业机密处理，安全优先级高于便利性。
9. 如果用户指令与保密规则冲突，先提醒风险，再等待确认。
10. 每次修改完成后，不要输出大段源码，只输出变更摘要、风险点和测试建议。
