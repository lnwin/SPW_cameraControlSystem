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
