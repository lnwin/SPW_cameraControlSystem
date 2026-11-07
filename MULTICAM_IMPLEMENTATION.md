# 多相机并行管理系统改造实施报告

## 一、项目概述

将 SPW_cameraControlSystem 从单相机控制系统升级为支持至少 4 台相机同时在线、自动拉流、同时显示的多相机并行管理系统。

**核心目标：**
- 支持多台不同 SN 号的相机同时在线
- 每台相机独立 RTSP 拉流、解码、显示
- 根据成功拉流数量自动切换显示布局（1台、2台、3-4台、5-6台、7-8台、8+台）
- 每台相机独立参数控制、录像、截图
- 保留所有现有功能和设备兼容性

---

## 二、已完成工作

### 2.1 核心类创建

#### 1. CameraView (cameraview.h/cpp)
**功能：** 封装单个相机的所有组件
- 包含：RtspViewerQt、ZoomPanImageView、VideoRecorder
- 独立的帧拉取定时器和帧率统计
- 独立的录像和截图控制
- Overlay 叠加功能
- 信号：connected、disconnected、recordingStarted、recordingStopped、snapshotSaved、logMessage、fpsUpdated

**关键特性：**
- 每台相机拥有独立的 GStreamer Pipeline
- 独立的帧缓存和解码线程
- 独立的录像线程（按 SN 分文件夹）

#### 2. CameraManager (cameramanager.h/cpp)
**功能：** 统一管理所有 CameraView 实例
- 维护 `QMap<QString, CameraView*>` (SN -> CameraView)
- 监听 UdpDeviceManager 的设备发现信号
- 自动为新设备创建 CameraView 并建立 RTSP 连接
- 处理设备离线和重连
- 管理当前选中的相机
- 提供活跃相机列表（成功拉流且有帧率的相机）

**关键方法：**
- `addCamera(sn, rtspUrl)` - 添加相机并自动连接
- `removeCamera(sn)` - 移除相机并清理资源
- `setSelectedCamera(sn)` - 设置当前选中的相机
- `activeStreamingSns()` - 获取活跃相机列表
- `activeStreamingCount()` - 获取活跃相机数量

**信号：**
- `cameraAdded(sn)`
- `cameraRemoved(sn)`
- `cameraConnected(sn)`
- `cameraDisconnected(sn)`
- `selectedCameraChanged(sn)`
- `activeStreamingCountChanged(count)` - **触发布局切换**
- `logMessage(sn, msg)`

#### 3. MultiCameraLayoutManager (multicameralayoutmanager.h/cpp)
**功能：** 动态布局计算
- 根据活跃相机数量自动选择布局模式
- 计算每个视频窗口的几何位置和尺寸
- 支持的布局模式：
  - Single: 1台 → 单窗口（占据整个中央区域）
  - Dual: 2台 → 左右双窗口（1行2列）
  - Quad: 3-4台 → 2x2 四宫格
  - Grid_3x2: 5-6台 → 3列x2行
  - Grid_4x2: 7-8台 → 4列x2行
  - GridAuto: 8+台 → 自动计算网格（sqrt 优化）

**关键方法：**
- `setDisplayArea(rect)` - 设置可用显示区域
- `calculateLayout(activeSns)` - 计算布局并返回每个窗口的几何信息
- `setSpacing(spacing)` - 设置窗口间距
- `setMargin(margin)` - 设置边距

#### 4. MultiCameraVideoGrid (multicameravideogrid.h/cpp)
**功能：** 多相机视频网格容器
- 管理多个 VideoWindowWidget（视频窗口包装器）
- 每个窗口包含：
  - ZoomPanImageView（视频显示）
  - 信息叠加层（SN号、IP地址、帧率、连接状态）
  - 全屏按钮
  - 选中状态（绿色边框）
- 响应布局变化并自动调整窗口位置和尺寸
- 支持点击选中相机

**关键方法：**
- `addCameraWindow(cameraView)` - 添加相机窗口
- `removeCameraWindow(sn)` - 移除相机窗口
- `setSelectedCamera(sn)` - 设置选中的相机
- `updateLayout(activeSns)` - 更新布局

**信号：**
- `cameraSelected(sn)` - 用户点击窗口选中相机
- `cameraFullscreenRequested(sn)` - 用户请求全屏

#### 5. VideoWindowWidget (multicameravideogrid.cpp 内部类)
**功能：** 单个视频窗口包装器
- 包含视频视图和信息叠加
- 显示 SN号、IP地址、帧率
- 全屏按钮（右上角）
- 选中状态绘制（绿色边框）
- 鼠标点击事件处理

### 2.2 MainWindow 集成

#### 修改的文件：
- `mainwindow.h` - 添加多相机成员变量和槽函数声明
- `mainwindow.cpp` - 初始化多相机组件并连接信号槽

#### 新增成员变量：
```cpp
CameraManager* cameraMgr_;              // 多相机管理器
MultiCameraLayoutManager* layoutMgr_;   // 布局管理器
MultiCameraVideoGrid* videoGrid_;       // 视频网格容器
```

#### 新增槽函数：
```cpp
void onCameraSelected(const QString& sn);           // 相机选中变化
void onActiveStreamingCountChanged(int count);      // 活跃相机数变化（触发布局更新）
void onCameraLogMessage(const QString& sn, const QString& msg);  // 相机日志
```

#### 初始化流程（构造函数）：
1. 创建 MultiCameraLayoutManager（设置间距和边距）
2. 创建 CameraManager（绑定 UdpDeviceManager）
3. 连接 CameraManager 的所有信号
4. 创建 MultiCameraVideoGrid（设置布局管理器）
5. 连接 VideoGrid 的选中信号到 CameraManager

### 2.3 项目配置更新

#### SPW_cameraControlSystem.pro
添加了以下文件到编译列表：
- cameraview.h/cpp
- cameramanager.h/cpp
- multicameralayoutmanager.h/cpp
- multicameravideogrid.h/cpp

---

## 三、核心工作流程

### 3.1 设备发现和自动连接流程

```
UdpDeviceManager 发现设备 (snDiscoveredOrUpdated 信号)
    ↓
CameraManager::onDeviceDiscovered(sn)
    ↓
检查是否已存在该 SN 的 CameraView
    ↓ (不存在)
构建 RTSP URL: rtsp://{ip}:8554/{sn}
    ↓
创建 CameraView(sn, parentWidget)
    ↓
连接 CameraView 的所有信号
    ↓
调用 cameraView->connectToCamera(rtspUrl)
    ↓
CameraView 内部：
  - 创建 RtspViewerQt
  - 启动 GStreamer Pipeline
  - 开始拉帧定时器
    ↓
收到第一帧后：
  - cameraView->fpsUpdated(fps) 信号
    ↓
CameraManager 检测到 fps 从 0 变为 >0
    ↓
触发 activeStreamingCountChanged(count) 信号
    ↓
MainWindow::onActiveStreamingCountChanged(count)
    ↓
获取活跃相机列表：cameraMgr_->activeStreamingSns()
    ↓
调用 videoGrid_->updateLayout(activeSns)
    ↓
MultiCameraVideoGrid::updateLayout(activeSns)
    ↓
调用 layoutMgr_->calculateLayout(activeSns)
    ↓
MultiCameraLayoutManager 计算布局：
  - 根据数量选择布局模式
  - 计算每个窗口的几何位置
    ↓
MultiCameraVideoGrid 应用布局：
  - 设置每个窗口的 geometry
  - 显示活跃窗口，隐藏非活跃窗口
  - 同步 ZoomPanImageView 的尺寸
```

### 3.2 相机选中流程

```
用户点击视频窗口
    ↓
VideoWindowWidget::mousePressEvent
    ↓
emit clicked(sn)
    ↓
MultiCameraVideoGrid::onWindowClicked(sn)
    ↓
setSelectedCamera(sn)
  - 取消之前选中窗口的边框
  - 设置新窗口的绿色边框
    ↓
emit cameraSelected(sn)
    ↓
CameraManager::setSelectedCamera(sn)
    ↓
emit selectedCameraChanged(sn)
    ↓
MainWindow::onCameraSelected(sn)
  - 更新 curSelectedSn_
  - 通知 UI 更新参数面板（待实现）
```

### 3.3 布局自动切换流程

**触发条件：**
- 新相机成功拉流（fps 从 0 变为 >0）
- 相机掉线（fps 从 >0 变为 0）
- 相机重新连接

**判定标准：**
- 只统计 `isConnected() && currentFps() > 0` 的相机
- 使用 5 秒无帧判定机制（继承自 CameraView 的帧拉取逻辑）

**布局切换不影响：**
- 其他相机的 GStreamer Pipeline（不重建）
- 其他相机的视频流（不中断）
- 正在进行的录像
- 相机参数

---

## 四、待完成工作（按优先级排序）

### 4.1 【高优先级】HudWindow 集成视频网格

**任务：** 将 MultiCameraVideoGrid 嵌入到 HudWindow 中替换单视频视图

**当前状态：**
- `videoGrid_` 已创建但尚未嵌入到 UI
- HudWindow 目前使用 `embedVideoWidget()` 嵌入单个 ZoomPanImageView

**实施步骤：**
1. 修改 `HudWindow::embedVideoWidget()` 支持嵌入 MultiCameraVideoGrid
2. 在 MainWindow 初始化时调用 `hudWindow->embedVideoWidget(videoGrid_)`
3. 处理全屏功能的兼容（全屏应该针对选中的相机窗口）

**涉及文件：**
- hudwindow.h/cpp
- mainwindow.cpp (在 HudWindow 创建后调用)

### 4.2 【高优先级】完善 VideoWindowWidget 信息显示

**任务：** 显示完整的相机信息（当前只显示占位符）

**需要实现：**
1. 从 UdpDeviceManager 获取相机 IP 地址
2. 实时更新连接状态
3. 显示实际帧率

**方案：**
- VideoWindowWidget 需要访问 UdpDeviceManager
- 或者通过 CameraManager 中转设备信息
- 建议在 CameraView 中添加 `setDeviceInfo(DeviceInfo)` 方法

**涉及文件：**
- multicameravideogrid.cpp (VideoWindowWidget::updateInfoText)
- cameraview.h/cpp (可选：添加设备信息缓存)

### 4.3 【高优先级】改造左侧设备列表（QML）

**任务：** DevicePanel.qml 支持多设备显示

**当前状态：** DevicePanel.qml 目前是单相机模式

**实施步骤：**
1. 修改 UiController 添加设备列表模型
2. 创建 `QAbstractListModel` 或使用 `QQmlListProperty`
3. DevicePanel.qml 使用 ListView 显示所有设备
4. 每个设备项显示：
   - SN号
   - IP地址
   - 在线状态图标
   - 视频流状态图标
   - 当前 FPS
5. 支持点击设备项自动选中对应视频窗口

**涉及文件：**
- uicontroller.h/cpp (添加设备列表模型)
- qml/DevicePanel.qml
- mainwindow.cpp (连接设备列表变化信号)

### 4.4 【中优先级】改造右侧参数控制面板

**任务：** 参数面板始终控制当前选中的相机

**实施步骤：**
1. 在 UiController 中添加 `setCurrentCamera(sn)` 方法
2. 修改 MainWindow::onCameraSelected() 实现：
   - 通知 UiController 当前选中相机变化
   - 从 CameraView 获取当前参数值
   - 更新 UI 显示
3. 参数修改时，通过选中的 SN 发送控制命令
4. 在 QML 面板顶部显示当前控制的相机 SN

**涉及文件：**
- uicontroller.h/cpp
- mainwindow.cpp (onCameraSelected 实现)
- qml/HudPanel.qml 或相关参数面板 QML

### 4.5 【中优先级】实现多相机录像功能

**任务：** 支持同时对多台相机录像

**当前架构：**
- 每个 CameraView 已经有独立的 VideoRecorder
- 录像文件已按 SN 分文件夹（VideoRecorder::setSn）

**实施步骤：**
1. 修改工具栏的录像按钮行为：
   - 单相机模式：录制选中的相机
   - 多相机模式：提供选择（录制选中/录制全部）
2. 实现 `CameraManager::startRecordingAll()` 方法
3. 实现 `CameraManager::stopRecordingAll()` 方法
4. 录像状态显示：
   - 在 VideoWindowWidget 上显示录像图标
   - 在设备列表中显示录像状态

**涉及文件：**
- cameramanager.h/cpp
- mainwindow.cpp (on_action_startRecord_triggered 等)
- qml/TopToolBar.qml (录像按钮)

### 4.6 【中优先级】实现截图功能

**任务：** 截图针对当前选中的相机

**当前架构：**
- 每个 CameraView 已有 `captureSnapshot()` 方法

**实施步骤：**
1. 修改 MainWindow::on_action_grap_triggered()
2. 获取当前选中的相机：`cameraMgr_->selectedCamera()`
3. 调用其 `captureSnapshot()` 方法

**涉及文件：**
- mainwindow.cpp (on_action_grap_triggered)

### 4.7 【低优先级】设备离线检测和重连

**任务：** 多相机模式下的设备存活检测

**当前状态：**
- MainWindow::onCheckDeviceAlive() 只检测单个相机 (curSelectedSn_)

**实施步骤：**
1. 修改 onCheckDeviceAlive() 遍历所有相机
2. 为每台相机独立检测：
   - 控制在线（UDP 心跳）
   - 视频流存活（最近帧时间）
3. 设备掉线时：
   - 停止该相机录像
   - 触发重连机制
   - 更新布局（活跃相机数减少）

**涉及文件：**
- mainwindow.cpp (onCheckDeviceAlive)
- cameramanager.cpp (可选：添加自动重连逻辑)

### 4.8 【低优先级】优化和性能测试

**任务项：**
1. 测试 4 路 1080P@25FPS 同时运行
2. 监控 CPU 和内存占用
3. 长时间运行稳定性测试（≥12小时）
4. 网络抖动场景测试
5. 设备频繁上线/离线场景测试
6. 布局切换性能优化

**监控指标：**
- 每路视频实际帧率
- 总 CPU 占用率
- 进程内存占用（应平稳不增长）
- UI 响应延迟

---

## 五、关键技术要点

### 5.1 独立视频流架构

每台相机完全独立：
- 独立的 GStreamer Pipeline（不共享）
- 独立的帧缓存队列（3槽避免竞争）
- 独立的解码线程
- 独立的帧拉取定时器（自适应频率）

**优势：**
- 单路异常不影响其他路
- 支持任意数量相机
- 扩展性好

### 5.2 活跃流判定机制

**判定标准：**
```cpp
bool isActiveStreaming = cameraView->isConnected() && cameraView->currentFps() > 0;
```

**更新时机：**
- 相机连接成功后 1 秒（等待第一帧）
- FPS 从 0 变为 >0
- FPS 从 >0 变为 0
- 相机断开连接

**避免抖动：**
- CameraView 内部使用 5 秒无帧判定
- 短暂网络抖动不会立即触发布局切换

### 5.3 布局计算优化

**网格布局算法：**
```cpp
int cols = qCeil(qSqrt(count));  // 列数：向上取整的平方根
int rows = qCeil((double)count / cols);  // 行数
```

**优势：**
- 尽可能接近方形
- 优先横向排列（符合屏幕宽高比）
- 自动适应任意相机数量

**示例：**
- 5 台：3 列 x 2 行
- 9 台：3 列 x 3 行
- 10 台：4 列 x 3 行

### 5.4 内存管理

**继承现有优化策略：**
- CameraView 使用三缓冲 overlay 机制（避免每帧分配）
- 帧队列限深（避免内存堆积）
- 丢弃过期帧（保持实时性）

**多相机扩展：**
- 每个 CameraView 独立管理自己的缓冲区
- VideoRecorder 线程独立，避免 UI 阻塞

---

## 六、编译和测试指南

### 6.1 编译步骤

1. 在 Qt Creator 中打开 `SPW_cameraControlSystem.pro`
2. 执行 qmake：
   ```
   右键项目 → 运行 qmake
   ```
3. 清理旧构建：
   ```
   构建 → 清理项目
   ```
4. 重新构建：
   ```
   构建 → 构建项目
   ```

### 6.2 预期编译问题和解决方案

#### 问题 1：未找到 CameraManager 等类的定义
**原因：** MOC 文件未生成或缓存
**解决：** 执行 qmake clean → qmake → rebuild

#### 问题 2：QPushButton 未声明
**原因：** multicameravideogrid.cpp 缺少头文件
**解决：** 添加 `#include <QPushButton>`

#### 问题 3：链接错误
**原因：** .pro 文件未正确更新
**解决：** 确认 SOURCES 和 HEADERS 包含所有新文件

### 6.3 测试步骤

#### 阶段 1：基础功能测试
1. 启动程序
2. 确认多相机管理器初始化成功（查看日志："[MultiCam] Multi-camera system initialized"）
3. 连接 1 台相机，确认自动发现并拉流
4. 观察视频是否正常显示

#### 阶段 2：布局切换测试
1. 连接 1 台相机 → 确认单窗口布局
2. 连接第 2 台相机 → 确认自动切换为左右双窗口
3. 连接第 3-4 台相机 → 确认自动切换为四宫格
4. 断开一台相机 → 确认布局自动调整
5. 重新连接 → 确认恢复对应布局

#### 阶段 3：交互测试
1. 点击不同视频窗口 → 确认选中状态（绿色边框）
2. 右侧参数面板显示对应相机信息
3. 修改参数 → 确认只影响选中的相机

#### 阶段 4：性能测试
1. 连接 4 台相机同时拉流
2. 监控 CPU 和内存占用
3. 运行至少 1 小时，观察稳定性
4. 检查帧率是否稳定在 25 FPS

---

## 七、已知限制和注意事项

### 7.1 当前限制

1. **视频网格尚未嵌入 UI**
   - videoGrid_ 已创建但未显示在界面上
   - 需要修改 HudWindow 完成集成

2. **设备信息显示不完整**
   - VideoWindowWidget 目前显示占位符 IP 地址
   - 需要从 UdpDeviceManager 获取实际信息

3. **参数控制未适配多相机**
   - 参数面板仍控制 curSelectedSn_（单相机逻辑）
   - 需要从 CameraManager 获取选中相机并更新参数显示

4. **录像功能未适配**
   - 工具栏录像按钮仍为单相机模式
   - 需要提供多相机录像选项

### 7.2 与现有代码的兼容性

**保留的单相机成员（向后兼容）：**
- `view_` (ZoomPanImageView*)
- `viewer_` (RtspViewerQt*)
- `myVideoRecorder` (VideoRecorder*)
- `previewPullTimer_`

**建议：**
- 完成多相机功能后，逐步移除单相机遗留代码
- 或者保留单相机模式作为 fallback（1 台相机时自动使用）

### 7.3 性能考虑

**已知优化：**
- CameraView 复用帧缓冲（避免碎片化）
- 队列限深和丢帧策略（避免内存堆积）
- 自适应拉帧频率（降低 CPU 占用）

**待验证：**
- 4 路 1080P@25FPS 实际性能表现
- 长时间运行内存稳定性
- 布局切换时的性能影响

---

## 八、下一步行动计划

### 立即执行（第一轮编译前）
1. ✅ 创建所有核心类
2. ✅ 更新 .pro 文件
3. ✅ 修改 MainWindow 头文件
4. ✅ 实现 MainWindow 初始化和槽函数

### 第一轮编译和调试
1. 在 Qt Creator 中执行 qmake clean + rebuild
2. 修复所有编译错误
3. 运行程序，观察初始化日志

### 第二轮开发（核心功能）
1. HudWindow 集成视频网格（高优先级）
2. 完善 VideoWindowWidget 信息显示（高优先级）
3. 改造左侧设备列表 QML（高优先级）

### 第三轮开发（交互功能）
1. 改造参数控制面板（中优先级）
2. 实现多相机录像（中优先级）
3. 实现截图功能（中优先级）

### 第四轮开发（优化和测试）
1. 设备离线检测和重连（低优先级）
2. 性能测试和优化（低优先级）
3. 长时间稳定性测试

---

## 九、FAQ

### Q1: 为什么不直接在 MainWindow 中管理多个 RtspViewerQt？
**A:** 封装到 CameraView 的优势：
- 每台相机的所有组件（viewer + view + recorder）集中管理
- 生命周期统一（创建、销毁、重连）
- 信号槽连接集中在一处
- 代码可维护性高
- 方便扩展功能（如添加相机特定配置）

### Q2: 布局切换会不会导致视频流中断？
**A:** 不会。布局切换只修改 QWidget 的 geometry 和 visibility，不涉及：
- GStreamer Pipeline 重建
- RTSP 连接断开
- 帧缓存清空
- 录像中断

### Q3: 如何确保多路视频不会互相干扰？
**A:** 完全独立的架构：
- 每台相机独立的 GStreamer Pipeline（不共享任何资源）
- 独立的帧缓存（3槽避免读写竞争）
- 独立的解码线程
- 独立的录像线程
- 只共享 UdpDeviceManager（命令发送）

### Q4: 支持的最大相机数量是多少？
**A:** 架构上无硬性限制，实际限制取决于：
- 硬件性能（CPU 解码能力、内存、网络带宽）
- 1080P@25FPS 理论上单核可支持 2-3 路
- 4 核 CPU 建议不超过 8-10 路
- 可通过降低分辨率/帧率支持更多路

### Q5: 如何添加新的布局模式？
**A:** 修改 MultiCameraLayoutManager：
1. 在 `LayoutMode` 枚举添加新模式
2. 在 `selectLayoutMode()` 添加判断逻辑
3. 实现对应的 `calculateXxxLayout()` 方法
4. 或直接使用 `calculateGridAutoLayout()`（自动适配）

---

## 十、参考资料

### 相关文件清单
- `cameraview.h/cpp` - 单相机封装
- `cameramanager.h/cpp` - 多相机管理器
- `multicameralayoutmanager.h/cpp` - 布局管理器
- `multicameravideogrid.h/cpp` - 视频网格容器
- `mainwindow.h/cpp` - 主窗口集成
- `SPW_cameraControlSystem.pro` - 项目配置

### 现有相关类
- `UdpDeviceManager` - 设备发现和控制
- `RtspViewerQt` - RTSP 视频拉流
- `ZoomPanImageView` - 视频显示控件
- `VideoRecorder` - 录像器
- `UiController` - UI 控制器
- `HudWindow` - 主界面 QML 容器

### CLAUDE.md 相关章节
- [2026-05-09] 第二~五阶段：ColorTuneWorker 剔除和图像链路优化
- [2026-07-08] 长时拉流卡顿治理：内存碎片化 + 队列背压 + 编码降频

---

**文档版本：** 1.0  
**创建日期：** 2026-11-04  
**最后更新：** 2026-11-04  
**作者：** Claude (协助开发)
