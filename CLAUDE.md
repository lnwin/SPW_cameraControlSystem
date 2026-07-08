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
