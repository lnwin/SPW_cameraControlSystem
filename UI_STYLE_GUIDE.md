# UI 风格规范 · 暗色荧光绿 HUD 主题

> 通用设计系统，可直接复用于新 Qt Quick / QML 工程。
> 风格定位：**工业级暗色控制台 / 军事 HUD 风**——纯黑背景 + 荧光绿描边 + 细边框 + 直角为主小圆角，无阴影、无渐变、无拟物。

---

## 1. 色板（Design Tokens）

| 语义 | 色值 | 用途 |
|------|------|------|
| **背景-主** | `#000000` | 窗口/视频区/中央画布底色 |
| **背景-面板** | `#020806` | 标题栏、对话框、HUD 面板底色（近黑带微绿） |
| **背景-面板2** | `#07110e` | 日志区、次级标题栏底色 |
| **背景-输入框** | `#0a1a12` / `#0a1a12` | 输入框、滑块槽底色 |
| **主色-描边** | `#00cc88` | 主边框、主文字、常态图标（品牌绿） |
| **主色-高亮** | `#00ff99` | hover/激活/焦点/有效值（荧光绿） |
| **激活背景** | `#0d2a1e` | 开关开启、导航选中、按钮 hover 底 |
| **hover 背景（弱）** | `#0d1f17` / `#081a10` | 按钮/导航悬停微亮 |
| **按下背景** | `#0a2a1e` | 按钮 pressed |
| **文字-次要** | `#9aa0a6` | 标签、说明文字（中性灰） |
| **文字-禁用** | `#3a4a42` / `#3a5a4a` | 禁用态标签、关闭态开关滑块 |
| **边框-禁用/关** | `#2a4a3a` | 关闭态开关边框 |
| **警告/危险** | `#ff3040` | 关闭按钮 hover、错误 toast、危险边框 |
| **危险背景** | `#3a0808` / `#2a0a0a` | 关闭/错误 hover 底色 |
| **警告-橙** | `#ff6600` | 日志告警行（⚠ 前缀高亮） |
| **成功背景** | `#0a2a1a` | 成功 toast 底 |
| **遮罩** | `#80000000` | 模态半透明遮罩（50% 黑） |

**核心配色逻辑**：常态用 `#00cc88`，一旦交互（hover/focus/active/有效数据）跃迁到更亮的 `#00ff99`。危险动作（关闭、错误）走红色系 `#ff3040`。

---

## 2. 字体

- **字族**：`Microsoft YaHei UI`（全局统一，中英文皆用）
- **字号阶梯**（`font.pixelSize`）：
  | 场景 | px |
  |------|----|
  | 面板标题 / 按钮 / 输入值 | 13 |
  | 状态标签 / 普通正文 | 12 |
  | 小字 / 文件名 / 日志普通行 / 附属值 | 11 |
- **加粗**：仅面板小标题（"系统状态""录像状态""系统日志"）和告警行 `font.bold: true`。
- 正文一律不加粗，靠颜色区分层级。

---

## 3. 形状与间距

- **圆角**：`radius: 2`（按钮、面板、输入框、导航项）；开关/圆点用全圆 `radius: height/2`。
- **边框**：统一 `border.width: 1`，靠颜色变化表达状态；**从不使用阴影**。
- **分隔线**：`height: 1` 的 Rectangle，`color: "#00cc88"`，`opacity: 0.2~0.4`（主分隔 0.4，次分隔 0.2）。
- **间距**：面板内 `spacing: 5`，`anchors.margins: 10`；日志区 margins 6 / spacing 2；对话框内容 margins 16 / spacing 10。
- **固定尺寸参考**：标题栏 32、工具栏 46、左设备栏 240、右状态栏 260、底部日志 110、导航项高 44、开关 42×20（滑块 14 圆）、窗口控制按钮 32×32。

---

## 4. 布局骨架

```
ColumnLayout (spacing:0, 填满窗口)
├── 自绘标题栏 (h:32, #020806, 绿边)  — Logo + 标题 + 自绘窗口按钮(min/max/close)
├── 顶部工具栏 (h:46)
├── RowLayout (fillHeight, spacing:0)
│   ├── 左侧面板 (w:240)      — 设备/导航
│   ├── 中央画布 (fill, #000, 绿边)  — 主内容/视频，空态居中提示文字 opacity:0.3
│   └── 右侧 HUD 面板 (w:260) — 状态项 + 控制开关
└── 底部日志区 (h:110, #07110e, 绿边)
```

- 全部用 `ColumnLayout`/`RowLayout`/`anchors`，`spacing: 0` 让边框贴合形成"网格舱"效果。
- 无边框窗口自绘：标题栏 `MouseArea` 拖拽 + Canvas 手绘最小化/最大化/关闭图标。

---

## 5. 组件规范（可直接复制）

### 5.1 面板容器（HudPanel）
```qml
Rectangle { color: "#020806"; border.color: "#00cc88"; border.width: 1; radius: 2 }
```

### 5.2 按钮（三态：常态/hover/pressed）
```qml
Button {
    property color borderColor: "#00cc88"
    property color glowColor:   "#00ff99"
    contentItem: Text {
        text: parent.text
        color: parent.hovered ? glowColor : borderColor
        font.pixelSize: 13; font.family: "Microsoft YaHei UI"
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: parent.pressed ? "#0a2a1e" : (parent.hovered ? "#0d1f17" : "transparent")
        border.color: parent.hovered ? glowColor : borderColor
        border.width: 1; radius: 2
    }
}
```

### 5.3 导航项（选中态填充绿底 + 亮边）
- 常态：透明底、`#00cc88` 文字；hover：`#081a10` 微亮底；选中：`#0d2a1e` 底 + `#00ff99` 边和文字。

### 5.4 状态项（标签-值 行）
- `RowLayout spacing:8`：标签 `#9aa0a6` / 值 `#00ff99`，值 `elide: ElideRight` + 溢出 ToolTip。

### 5.5 开关（Toggle，滑块动画）
- 轨道 42×20 `radius:10`；开：底 `#0d2a1e` 边 `#00ff99`，滑块靠右亮绿；关：透明底 边 `#2a4a3a`，滑块靠左暗绿。
- 滑块位移 `Behavior on x { NumberAnimation { duration: 120 } }`。
- 右侧配"开/关"文字，颜色随状态。

### 5.6 滑块（Slider）
- 槽 `height:3 radius:1` 底 `#0a1a12`，已填充段 `#00cc88`；手柄 10×10 圆，`pressed` 时 `#00ff99`。

### 5.7 输入框（TextInput）
- 外套 Rectangle：底 `#0a1a12`，边 `activeFocus ? "#00ff99" : "#00cc88"`，`radius:2`；内文字 `#00ff99`，`selectByMouse: true`。

### 5.8 对话框（居中卡片）
- `#020806` 底 + 绿边；内含次级标题栏（`#07110e` + 右上角 ✕ 关闭，hover 变红）；底部按钮右对齐 72×28。

### 5.9 Toast 提示
- 底部居中，`radius:4`，成功=绿系(`#0a2a1a`/`#00ff99`)，失败=红系(`#2a0a0a`/`#ff3040`)，`visible` 属性绑定驱动。

### 5.10 加载/等待遮罩
- 全屏 `#80000000` 遮罩 + 居中卡片；用呼吸圆点动画（`SequentialAnimation on opacity` 0.2↔1.0 各 500ms）表示进行中。

---

## 6. 动效规范

- **时长**：状态切换 120ms（开关滑块）、150ms（透明度）、500ms（呼吸循环单程）。
- **缓动**：默认 `NumberAnimation`（线性），不用弹性曲线，保持工业冷感。
- **禁用态**：`opacity: 0.45` + `Behavior on opacity`，而非直接隐藏。
- 原则：动效只用于**状态反馈**（开关、焦点、加载），绝不做装饰性入场动画。

---

## 7. 交互约定

- **hover 必反馈**：所有可点元素 hover 时边框/文字从 `#00cc88` → `#00ff99`。
- **危险操作红色化**：关闭、删除、错误一律红系，且仅在 hover 时显红（常态仍绿），避免误触视觉。
- **空态提示**：中央区无内容时居中放低透明度(0.3)提示文字。
- **日志分级**：普通行 `#9aa0a6` 11px；告警行（含 `⚠`）`#ff6600` 12px 加粗。日志上限 200 行，超出移除最旧。

---

## 8. 落地清单（新工程起步）

1. 建 `qml/` 目录，复制本规范第 5 节组件：`HudPanel.qml`、`HudButton.qml`、`SideNavButton.qml`、`StatusItem.qml`、`Toggle`、`ChangeIpDialog.qml` 作为基础组件库。
2. 顶层 `Main.qml` 用第 4 节骨架搭"标题栏 + 工具栏 + 三栏 + 日志"网格布局。
3. 全局字体设 `Microsoft YaHei UI`；把第 1 节色板定义为 QML 单例 `Theme.qml`（`property color bgPanel: "#020806"` …）便于统一维护换肤。
4. 无边框窗口：`Qt::FramelessWindowHint`，标题栏自绘拖拽 + Canvas 手绘窗口按钮。

---

*本规范仅描述通用视觉/交互设计语言，不含任何业务逻辑、通信协议或专有数据，可安全用于其他工程。*
