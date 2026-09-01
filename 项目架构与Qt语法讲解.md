# 真空深低温控制系统 —— 项目架构与 Qt 语法讲解

> 适用对象：对 Qt 语法和架构有些生疏的开发者。
> 本文档逐文件、逐机制讲解本工程的组成与实现，配合源码阅读效果最佳。
> 工程根目录：`/home/lin/Workspace/cryo_low_vib`

---

## 目录

1. [项目总览](#1-项目总览)
2. [构建系统：CMakeLists.txt 逐段解读](#2-构建系统cmakeliststxt-逐段解读)
3. [程序入口与事件循环：main.cpp](#3-程序入口与事件循环maincpp)
4. [Qt 核心机制速览（生疏者必读）](#4-qt-核心机制速览生疏者必读)
5. [主窗口与双模式架构：MainWindow](#5-主窗口与双模式架构mainwindow)
6. [模拟控制器（占位 FSM）：MockSystem](#6-模拟控制器占位-fsmmocksystem)
7. [傻瓜模式界面：OperatorWidget](#7-傻瓜模式界面operatorwidget)
8. [工程师模式界面：EngineerWidget](#8-工程师模式界面engineerwidget)
9. [亮色工业风 QSS 详解](#9-亮色工业风-qss-详解)
10. [代码中值得注意的语法点（问答式）](#10-代码中值得注意的语法点问答式)
11. [扩展指引：如何接入真实 FSM 与硬件](#11-扩展指引如何接入真实-fsm-与硬件)

---

## 1. 项目总览

这是一套「真空深低温控制系统」的上位机界面程序，用于激光剪切散斑干涉等实验设备的真空与低温控制。

**双模式架构**：操作员（傻瓜模式，界面极简高防呆）与工程师（调试后台，可单动硬件、看诊断、改参数）通过密码切换。

**技术栈**：

| 项目 | 选型 |
|------|------|
| 语言 | C++17 |
| 框架 | Qt 6 Widgets（兼容 Qt 5.15） |
| 构建 | CMake（≥ 3.19） |
| UI 方式 | 纯 C++ 代码布局 + QSS 样式表，**零 .ui 文件** |
| 控制逻辑 | MockSystem 模拟控制器（占位 FSM），后续可无缝替换为真实控制逻辑 |

**文件清单**：

| 文件 | 职责 |
|------|------|
| `CMakeLists.txt` | 构建配置：找 Qt、开 AUTOMOC、编译选项 |
| `main.cpp` | 程序入口：创建 QApplication、应用 Fusion 风格、启动事件循环 |
| `MainWindow.h` / `.cpp` | 主窗口：顶栏（标题/模式/时钟/心跳/切换按钮）+ QStackedWidget 双页面 + **MockSystem 模拟控制器**（声明在 MainWindow.h，实现于 MainWindow.cpp） |
| `OperatorWidget.h` / `.cpp` | 页面 0：傻瓜模式（操作员） |
| `EngineerWidget.h` / `.cpp` | 页面 1：工程师模式（QTabWidget 三页签） |

**对象关系总览**：

```
main.cpp
  └─ QApplication
       └─ MainWindow (QMainWindow)
            ├─ m_header (QFrame，顶栏)
            ├─ m_stack (QStackedWidget)
            │    ├─ page 0: OperatorWidget   ← 傻瓜模式
            │    └─ page 1: EngineerWidget   ← 工程师模式
            ├─ m_system (MockSystem，模拟控制器)
            └─ m_clockTimer (QTimer，1s 刷时钟)
```

UI 与"控制器"完全解耦：两个页面**不认识** MockSystem，只暴露自己的信号（命令）和槽（状态显示），由 `MainWindow::wireSystem()` 统一接线。这正是本工程最重要的架构约定。

---

## 2. 构建系统：CMakeLists.txt 逐段解读

```cmake
cmake_minimum_required(VERSION 3.19)
project(cryo_low_vib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)          # C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 编译器必须支持 C++17，否则报错
set(CMAKE_CXX_EXTENSIONS OFF)       # 不用 GNU 扩展（-std=c++17 而非 gnu++17）

# 纯 C++ 手写布局 + QSS，不使用 .ui / .qrc / .qss 资源文件
set(CMAKE_AUTOMOC ON)   # 自动运行 moc（见下文说明）
set(CMAKE_AUTOUIC OFF)  # 不处理 .ui（uic）
set(CMAKE_AUTORCC OFF)  # 不处理 .qrc（rcc）
```

**AUTOMOC 是什么**：Qt 的类只要写了 `Q_OBJECT` 宏，就需要一个叫 **moc（Meta-Object Compiler，元对象编译器）** 的工具为它生成额外的 C++ 代码（实现信号槽、属性、类名反射等）。`CMAKE_AUTOMOC ON` 让 CMake 自动扫描源码里的 `Q_OBJECT` 并调用 moc，不需要手动写 moc 命令。注意：**任何包含 `Q_OBJECT` 的头文件都要列进 `add_executable`**，否则 AUTOMOC 扫不到。

```cmake
# 优先 Qt6（>=6.2），找不到时回退 Qt5.15
find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(QT_LIBS Qt6::Widgets)
    message(STATUS "Using Qt ${Qt6_VERSION} (Widgets)")
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    set(QT_LIBS Qt5::Widgets)
    message(STATUS "Using Qt ${Qt5_VERSION} (Widgets)")
endif()
```

`find_package` 是 CMake 查找库的标准方式。`Qt6::Widgets` 是 Qt 提供的 **imported target**（导入目标），`target_link_libraries` 链接它时自动带上头文件路径和库文件。这样同一份代码在 Qt6/Qt5 下都能编。

```cmake
add_executable(cryo_low_vib
    main.cpp
    MainWindow.h
    MainWindow.cpp
    OperatorWidget.h
    OperatorWidget.cpp
    EngineerWidget.h
    EngineerWidget.cpp
)

target_include_directories(cryo_low_vib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cryo_low_vib PRIVATE ${QT_LIBS})

if(MSVC)
    target_compile_options(cryo_low_vib PRIVATE /W4)
else()
    target_compile_options(cryo_low_vib PRIVATE -Wall -Wextra)  # 零警告要求
endif()
```

编译命令：`cmake -B build && cmake --build build`，产物为 `build/cryo_low_vib`。

---

## 3. 程序入口与事件循环：main.cpp

```cpp
#include "MainWindow.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);            // ① 全局唯一的 QApplication
    app.setApplicationName(QStringLiteral("真空深低温控制系统"));
    app.setOrganizationName(QStringLiteral("CryoLab"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));  // ② 基础风格

    MainWindow w;
    w.show();                                // ③ 显示主窗口
    return app.exec();                       // ④ 进入事件循环
}
```

- **① QApplication**：管理 GUI 应用程序的一切——事件循环、剪贴板、全局样式等。一个进程只能有一个，必须在创建任何窗口之前创建。所有 Qt 程序入口都是这个模式。
- **② Fusion 风格**：Qt 的跨平台基础风格，控件绘制一致，配合 QSS 效果稳定（尤其 QSpinBox 的上下箭头）。
- **③ `w.show()`**：窗口默认隐藏，必须显式 show。
- **④ `app.exec()`**：**事件循环**。程序在这里"卡住"，但内部不断处理事件（鼠标、键盘、定时器、信号槽……）。窗口关闭时 exec() 返回，程序结束。**所有定时器、信号槽回调都发生在这个循环里**——所以 main 之后你再也看不到"while(1)"。

---

## 4. Qt 核心机制速览（生疏者必读）

### 4.1 QObject 与 Q_OBJECT 宏

Qt 的"对象系统"（信号槽、属性、父子内存管理）由 `QObject` 基类提供。要使用信号槽，类必须：

```cpp
class MockSystem : public QObject {
    Q_OBJECT            // ← 关键宏：声明该类使用元对象系统
public:
    explicit MockSystem(QObject *parent = nullptr);
signals:                // 信号区
    void vacuumChanged(double pa);
public slots:           // 槽区（Qt6 中 public slots 等价于普通成员函数）
    void startSystem();
};
```

`Q_OBJECT` 会触发 moc 生成元数据。**写了 Q_OBJECT 的类，构造函数第一参数惯例是 `QObject *parent = nullptr`**。

### 4.2 信号与槽：两种 connect 语法

**旧语法**（字符串匹配，运行期才检查，写错不报编译错误）：

```cpp
connect(button, SIGNAL(clicked()), this, SLOT(onClick()));
```

**新语法**（函数指针，编译期检查，推荐；本工程全部使用）：

```cpp
connect(m_btnStart, &QPushButton::clicked, this, &OperatorWidget::startRequested);
```

要点：
- 信号是"广播"，用 `emit` 触发，任何连接的槽都会被调用。
- **信号可以连接信号**（信号转发）：

  ```cpp
  connect(m_heaterChk, &QCheckBox::toggled, this, &EngineerWidget::heaterSet);
  // toggled(bool) → 本类的 heaterSet(bool) 信号 → 再转给 MockSystem
  ```

- **信号参数多于槽参数时自动截断**：`clicked(bool)` 可以连到无参的 `startRequested()`。
- **lambda 作槽**（本工程大量使用）：

  ```cpp
  connect(m_gateRow.onBtn, &QPushButton::clicked, this, [this] { emit gateValveSet(true); });
  ```

  `[this]` 捕获 this 指针以便访问成员。lambda 与信号参数匹配时也可带参数。

- **信号→槽的"接线"与调用线程**：默认同线程直接调用，跨线程自动排队（本工程不需要关心）。

### 4.3 父子对象与内存管理

```cpp
auto *led = new QLabel(parent);   // parent 传入，控件归属父对象
```

Qt 的黄金法则：**new 出来的 QObject/QWidget 只要指定了 parent，就由父对象负责析构**。父对象销毁时自动 delete 所有子对象。所以本工程几乎看不到 delete——窗口关闭，整棵树自动释放。`m_clockTimer = new QTimer(this);` 同理。

注意：**布局管理器（QLayout）不是 QWidget**，它只负责排列；`addWidget` 的控件仍需要 parent 参数（或 addWidget 时自动 reparent 到 layout 所属 widget）。

### 4.4 布局系统：QLayout 家族

| 布局类 | 用途 | 本工程示例 |
|--------|------|-----------|
| `QVBoxLayout` | 垂直排列 | 操作员页根布局、卡片内部 |
| `QHBoxLayout` | 水平排列 | 顶栏、底部按钮行 |
| `QGridLayout` | 网格排列 | 工程师 Tab1 的 2×2 区域、DI/传感器面板 |
| `QFormLayout` | "标签:控件"表单 | 阀门/泵/加热/参数页 |

基本模式：

```cpp
auto *root = new QVBoxLayout(this);          // 布局直接作用于 this
root->setContentsMargins(24, 24, 24, 24);    // 外边距
root->setSpacing(16);                        // 控件间距
root->addWidget(widget, 1);                  // 第二个参数是拉伸系数
root->addLayout(subLayout, 2);               // 布局也可以嵌套
```

`addWidget(w, 1)` 的 `1` 是 **stretch（拉伸因子）**：同一布局里多个 stretch 按比例分配多余空间。操作员页 `cards` 区 stretch=2、相态区=1、按钮区=1，即卡片区占更高比例。

`addStretch(1)` 是"弹性空白"，把后续控件推到另一端（顶栏用它把右侧内容推到右边）。

### 4.5 QSS 样式表

QSS 语法类似 CSS，作用于 Qt 控件：

```css
QPushButton {                          /* 类型选择器：所有按钮 */
    background-color: #ffffff;
    border: 1px solid #d0d4d9;
}
QPushButton:hover { background-color: #f2f4f7; }   /* 伪状态：悬停 */
QPushButton:pressed { ... }                        /* 按下 */
QPushButton:disabled { ... }                       /* 禁用 */
QPushButton#StartButton { ... }                    /* #objectName 选择器 */
```

- **全局注入**：`qApp->setStyleSheet(...)` 在 MainWindow 构造函数里执行一次，整个程序生效（包括对话框）。
- **objectName 选择器**：代码里 `widget->setObjectName("Card")` 后，QSS 用 `QFrame#Card` 精确命中。这是"代码与样式解耦"的关键。
- **内联样式**：`label->setStyleSheet("color:red;")` 只作用于该控件，优先级高于全局 QSS。本工程用内联样式做**动态变色**（数值达标变绿等）。

### 4.6 QTimer：界面刷新的心脏

```cpp
m_timer = new QTimer(this);
m_timer->setInterval(250);                       // 250ms 一次
connect(m_timer, &QTimer::timeout, this, &MockSystem::tick);
m_timer->start();
```

QTimer 靠事件循环驱动，**不占线程**。本工程两个定时器：
- MockSystem 的 250ms 心跳（模拟数据刷新）；
- MainWindow 的 1s 时钟（顶栏时间显示）。

### 4.7 字符串：QStringLiteral 与 arg 格式化

- `QStringLiteral("中文")`：编译期生成 QString，避免运行时转换，也保证源码 UTF-8 中文不乱码。**Qt 代码里别写裸的 "..."**。
- `QString::arg()` 是 Qt 的 printf 替代（`%1`、`%2` 占位）：

  ```cpp
  QStringLiteral("真空 %1 Pa").arg(m_vacuum, 0, 'e', 2)
  // arg(数值, 最小宽度0, 格式'e', 精度2) → "1.20e-03 Pa"
  ```

  `'e'` 科学计数、`'f'` 定点小数、`'g'` 自动；`.arg(a).arg(b)` 链式调用，`%1`→a、`%2`→b。
- 数字转字符串还有 `QString::number(pa, 'e', 1)`。

---

## 5. 主窗口与双模式架构：MainWindow

### 5.1 头文件结构（MainWindow.h）

```cpp
class QLabel;      // 前置声明：头文件里只出现指针，不需要完整定义
class QPushButton; // 减少头文件依赖，加快编译

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
private slots:                 // 槽：被信号触发的成员函数
    void updateClock();
    void onHeartbeat(bool ok);
    void onToggleModeClicked();
private:
    void buildHeaderBar();
    void setEngineerMode(bool engineer);
    void wireSystem();
    // ...成员指针（控件句柄），一律带 = nullptr 初始化
    QStackedWidget *m_stack = nullptr;
    OperatorWidget *m_operator = nullptr;
    MockSystem *m_system = nullptr;
    bool m_engineerMode = false;
};
```

**前置声明技巧**：头文件里只声明 `class QLabel;`，实际 include 放在 .cpp——这样改一个 Qt 头文件不会牵连所有包含 MainWindow.h 的文件。

**同一个头文件里声明了两个 Q_OBJECT 类**（MainWindow 和 MockSystem），moc 一次处理，没问题。

### 5.2 构造函数：组装整机

```cpp
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("真空深低温控制系统"));
    resize(1280, 800);
    setMinimumSize(1100, 700);

    qApp->setStyleSheet(QString::fromUtf8(kDarkQss));   // 注入全局亮色 QSS

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildHeaderBar();                                    // 顶栏

    m_stack = new QStackedWidget(central);               // 双页面容器
    m_operator = new OperatorWidget(m_stack);
    m_engineer = new EngineerWidget(m_stack);
    m_stack->addWidget(m_operator);                      // index 0
    m_stack->addWidget(m_engineer);                      // index 1

    root->addWidget(m_header);
    root->addWidget(m_stack, 1);
    setCentralWidget(central);                           // QMainWindow 必须有 central widget

    m_system = new MockSystem(this);                     // 模拟控制器
    wireSystem();                                        // ← 全部信号槽接线

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start();
    updateClock();

    statusBar()->setSizeGripEnabled(false);              // QMainWindow 自带状态栏
    statusBar()->showMessage(QStringLiteral("Mock 控制器在线 · 当前数据为模拟值，仅供界面联调"));
}
```

**QStackedWidget 是什么**：多页面"叠放"容器，同一时刻只显示一页，`setCurrentIndex(0/1)` 切换。这是双模式切换的核心。两个页面在构造时都创建好，切换只是"翻牌"，不重建控件、状态不丢失。

### 5.3 顶栏构建（buildHeaderBar）

```cpp
m_header = new QFrame(this);
m_header->setObjectName(QStringLiteral("HeaderBar"));  // 供 QSS 定位
m_header->setFixedHeight(64);

auto *h = new QHBoxLayout(m_header);
h->setContentsMargins(16, 0, 16, 0);
h->setSpacing(12);

auto *title = new QLabel(QStringLiteral("真空深低温控制系统"), m_header);
title->setObjectName(QStringLiteral("AppTitle"));

m_modeLabel = new QLabel(QStringLiteral("[操作员模式]"), m_header);
m_modeLabel->setObjectName(QStringLiteral("ModeChip"));

m_clockLabel = new QLabel(m_header);
m_clockLabel->setObjectName(QStringLiteral("ClockLabel"));

m_heartbeatLed = new QLabel(m_header);      // 心跳指示灯：12×12 圆点
m_heartbeatLed->setFixedSize(12, 12);
m_heartbeatText = new QLabel(QStringLiteral("心跳连接中…"), m_header);

m_modeButton = new QPushButton(QStringLiteral("切换模式 / 🔒"), m_header);
m_modeButton->setObjectName(QStringLiteral("PrimaryButton"));

h->addWidget(title);
h->addSpacing(8);
h->addWidget(m_modeLabel);
h->addStretch(1);                 // ← 弹性空白：把后面内容推到最右
h->addWidget(m_clockLabel);
h->addSpacing(8);
h->addWidget(m_heartbeatLed);
h->addWidget(m_heartbeatText);
h->addSpacing(8);
h->addWidget(m_modeButton);

connect(m_modeButton, &QPushButton::clicked, this, &MainWindow::onToggleModeClicked);
```

"LED"其实就是一个 `QLabel` 加固定尺寸和圆角背景色，通过 `setStyleSheet` 变色（绿/红）——Qt 里做状态灯的最常见手法，不需要自定义控件。

### 5.4 模式切换与密码验证

```cpp
void MainWindow::onToggleModeClicked()
{
    if (m_engineerMode) {
        // 工程师 → 操作员：直接切回，无需密码
        setEngineerMode(false);
        statusBar()->showMessage(QStringLiteral("已切回操作员模式（简单防呆界面）"), 4000);
        return;
    }

    // 操作员 → 工程师：弹密码框（QInputDialog 静态函数，一行搞定）
    bool ok = false;
    const QString pwd = QInputDialog::getText(this, QStringLiteral("切换到工程师模式"),
                                              QStringLiteral("请输入工程师密码："),
                                              QLineEdit::Password, QString(), &ok);
    if (!ok)          // 用户点了取消
        return;
    if (pwd == QStringLiteral("888888")) {
        setEngineerMode(true);
        statusBar()->showMessage(QStringLiteral("密码验证通过，已进入工程师模式（调试后台）"), 4000);
    } else {
        QMessageBox::warning(this, QStringLiteral("密码错误"),
                             QStringLiteral("密码错误，无法进入工程师模式。\n默认密码：888888"));
    }
}

void MainWindow::setEngineerMode(bool engineer)
{
    m_engineerMode = engineer;
    m_stack->setCurrentIndex(engineer ? 1 : 0);   // ← 翻页
    if (engineer) {
        m_modeLabel->setText(QStringLiteral("[工程师模式]"));
        m_modeLabel->setStyleSheet(QStringLiteral("color:#c97a1c;"));   // 琥珀色
    } else {
        m_modeLabel->setText(QStringLiteral("[操作员模式]"));
        m_modeLabel->setStyleSheet(QStringLiteral("color:#1e9e56;"));   // 绿色
    }
}
```

- `QInputDialog::getText(...)` 是静态便捷函数：弹输入框、返回字符串、`&ok` 输出是否确认。
- `QLineEdit::Password` 让输入显示为圆点。
- `statusBar()->showMessage(text, 4000)`：状态栏提示 4 秒后自动消失。

### 5.5 wireSystem：全量信号槽接线表

这是整个工程"解耦"的枢纽。**界面只发命令信号、收状态信号；控制器只管模拟，不知道界面存在**。

方向一：**控制器状态 → 界面刷新**（MockSystem 信号 → Widget 槽）

| 信号（MockSystem） | 槽 | 作用 |
|---|---|---|
| `vacuumChanged(double)` | `OperatorWidget::onVacuumChanged` / `EngineerWidget::onVacuumChanged` | 真空度上屏 |
| `temperatureChanged(double)` | `OperatorWidget::onTemperatureChanged` / `EngineerWidget::onTemperatureChanged` | 温度上屏 + 防呆锁判断 |
| `phaseChanged(QString)` | 两个页面的 `onPhaseChanged` | 相态大字 |
| `highLimitChanged(double)` | `OperatorWidget::onHighLimitChanged` | 高真空门限同步到卡片判定 |
| `heartbeat(bool)` | `MainWindow::onHeartbeat` | 顶栏心跳灯 |
| `gateValveChanged / foreValveChanged / ventValveChanged(bool)` | 工程师页对应槽 | 阀门状态灯 |
| `dryPumpChanged / tmpChanged / tmpSpeedChanged / cryocoolerChanged(bool/int)` | 工程师页对应槽 | 泵/制冷状态 |
| `heaterChanged(bool)` | `EngineerWidget::onHeaterChanged` | 加热勾选框回显 |
| `diChanged(int,bool)` / `sensorRawChanged(int,QString)` / `logMessage(QString)` | 工程师页诊断槽 | DI/传感器/日志 |

方向二：**界面命令 → 控制器**（Widget 信号 → MockSystem 槽）

| 信号 | 槽 |
|---|---|
| `OperatorWidget::startRequested()` | `MockSystem::startSystem()` |
| `OperatorWidget::warmupRequested()` | `MockSystem::stopAndWarmUp()` |
| `OperatorWidget::ventRequested()` | `MockSystem::confirmVent()` |
| `EngineerWidget::gateValveSet(bool)` 等 3 个阀门 | `setGateValve` 等 |
| `EngineerWidget::dryPumpSet / tmpSet / tmpSpeedSet / cryocoolerSet` | 对应槽 |
| `EngineerWidget::heaterSet(bool)` / `pidParamsSet(4×double)` | `setHeater` / `setPidParams` |
| `EngineerWidget::configApplied(4×double)` / `configResetRequested()` | `applyConfig` / `resetConfig` |

一个信号可以连多个槽（如 vacuumChanged 同时喂两个页面），体现信号槽"一对多"。

---

## 6. 模拟控制器（占位 FSM）：MockSystem

### 6.1 设计意图

真实系统最终会有一个 FSM（有限状态机）控制干泵→分子泵→制冷机……但写 UI 时硬件未就绪。所以写一个 **MockSystem** 模拟控制器：250ms 心跳，用数学公式模拟真空度/温度变化，遵守与真实 FSM 相同的**信号接口**。将来替换时，只要新类保持这些信号和槽签名，UI 代码一行不用改。

### 6.2 状态机：Phase 枚举

```cpp
enum class Phase {          // enum class：强类型枚举，必须 Phase::xxx 访问
    Booting,  // 控制器上电自检
    Idle,     // 待机（大气）
    Rough,    // 粗抽真空阶段
    HV,       // 高真空抽气阶段
    Cooling,  // 深度降温阶段
    Running,  // 深低温长效运行中
    Warming,  // 受控回温阶段
    Safe,     // 系统已安全回温
    Vented    // 系统已放空（大气）
};
```

状态机由 `tick()` 驱动，**每个 tick 只做三件事**：更新物理量 → 判断是否换相（`setPhase`）→ 广播信号。

### 6.3 tick()：模拟物理过程（节选讲解）

```cpp
void MockSystem::tick()
{
    ++m_ticks;
    ++m_phaseTick;

    // 心跳：前 8 个周期（约 2s）模拟控制器自检（红灯），之后绿灯
    const bool hbOk = m_ticks > 8;
    emit heartbeat(hbOk);

    switch (m_phase) {
    case Phase::Rough: {
        // 对数线性粗抽：101325 Pa → 粗抽门限，约 24 tick（6s）
        const double l0 = std::log10(101325.0);
        const double l1 = std::log10(m_roughLimit);
        const double frac = std::min(1.0, m_phaseTick / 24.0);
        m_vacuum = std::pow(10.0, l0 + (l1 - l0) * frac);
        if (m_phaseTick >= 24) {
            setPhase(Phase::HV);          // 换相
            pushLog(QStringLiteral("达到粗抽门限 %1 Pa，转入高真空抽气").arg(m_roughLimit, 0, 'g', 3));
        }
        break;
    }
    // ...其他相位类似：HV（对数高抽）、Cooling（按速率降温）、
    //    Running（小正弦抖动模拟波动）、Warming（按速率升温）、Safe/Vented/Idle（稳态）
    }

    if (m_phase != Phase::Booting) {
        emit vacuumChanged(m_vacuum);       // ← 广播，接线好的界面自动刷新
        emit temperatureChanged(m_temp);
        emit phaseChanged(phaseText());
        updateSensors();
    }
}
```

- 真空用**对数坐标**插值（真空跨 5 个数量级，线性插值没意义），`std::log10` / `std::pow` 来自 `<cmath>`。
- `std::sin` 制造微小波动，模拟真实信号抖动。
- `m_phaseTick` 记录当前相位已持续多少 tick，驱动换相判断。
- **换相通过 `setPhase`**：它重置计时、发 `phaseChanged` 信号，界面大字随之变化。

### 6.4 三个主流程命令

- `startSystem()`：幂等检查（已在运行则拒绝）→ 依次"开干泵→开前级阀+启动 TMP→开主阀"→ 进入 `Rough`。每一步 `pushLog` 并 `emit xxxChanged`，工程师页的状态灯随之点亮。后续自动流程由 tick() 接力（粗抽→高抽→制冷→Running）。
- `stopAndWarmUp()`：抽气阶段按下=取消流程回待机；制冷/运行阶段按下=关制冷机、使能加热器、进入 `Warming`，温度按速率回升，≥290 K 时自动进入 `Safe`（防呆解锁）。
- `confirmVent()`：三重守卫——已放空/已在大气/温度不足 290 K 都拒绝并记录日志；通过则关主阀、开放空阀、停泵，`Vented` 相位充气至大气压。

### 6.5 手动单动槽的"防抖"写法

```cpp
void MockSystem::setGateValve(bool open)
{
    if (m_gate == open)     // 状态没变就直接返回
        return;             // 防止信号→槽→信号回环（工程师页勾选框回显会触发 toggled）
    m_gate = open;
    emit gateValveChanged(open);
    pushLog(open ? QStringLiteral("高真空气动主阀 → 开启（手动）")
                 : QStringLiteral("高真空气动主阀 → 关闭（手动）"));
}
```

所有手动槽都是这个模式：**先判同值返回，再改状态、发信号、记日志**。这既避免了无意义广播，也切断了潜在的回环（例如 `onHeaterChanged` 里 `setChecked` 触发 `toggled` 信号重新发给 Mock）。

### 6.6 其他细节

- `pushLog()`：统一加 `[HH:mm:ss]` 时间戳再发 `logMessage`，工程师页日志框直接显示。
- `updateSensors()`：6 个传感器每 tick 广播一条模拟量字符串（真空规电压、温度传感器、加热电流、制冷机功耗）。
- DI 数组 `m_di[DiCount]`：构造时全部置 true（正常），首个 tick 广播一次。

---

## 7. 傻瓜模式界面：OperatorWidget

### 7.1 布局树

```
OperatorWidget (QVBoxLayout root, margin 24)
├─ HBox cards (stretch 2)
│   ├─ QFrame#Card 真空度卡片
│   │   ├─ QLabel#CardTitle "腔体真空度"
│   │   ├─ HBox: [m_vacuumValue 40px大字] [单位 Pa]
│   │   └─ m_vacuumStatus "达标/未达标"
│   └─ QFrame#Card 温度卡片（结构同上，冰蓝色大字）
├─ QFrame#Card 相态区 (stretch 1)
│   ├─ QLabel#CardTitle "系统当前相态"
│   └─ m_phaseLabel 30px 大字
└─ HBox btns (stretch 1)
    ├─ m_btnStart  QPushButton#StartButton "1. 一键启动系统"
    ├─ m_btnWarm   QPushButton#WarmButton  "2. 停止并受控回温"
    └─ m_btnVent   QPushButton#VentButton  "3. 确认室温放空"
        （ventHint 独立一行：小号灰色提示"样品温度 ≥ 290 K 后可用"）
```

三个大按钮**直接同层放进同一个 QHBoxLayout**（stretch 1:1:1），保证顶部边缘、高度完全一致；防呆提示文字另起一行放在放空按钮下方（`hintRow` 布局里 `addStretch(2)` + `addWidget(ventHint, 1)` 把它推到偏右，正对放空按钮），并设 `font-size:10px` 小字号。

### 7.2 动态变色：内联 setStyleSheet

```cpp
void OperatorWidget::onVacuumChanged(double pa)
{
    m_vacuumValue->setText(QString::number(pa, 'e', 1));   // 科学计数法 1 位小数
    const bool ok = pa <= m_highLimit;                      // 是否达到高真空门限
    m_vacuumStatus->setText(ok ? QStringLiteral("达标（高真空）") : QStringLiteral("未达标"));
    m_vacuumStatus->setStyleSheet(ok ? QStringLiteral("color:#1e9e56; font-size:13px;")
                                     : QStringLiteral("color:#b3791d; font-size:13px;"));
    m_vacuumValue->setStyleSheet(
        ok ? QStringLiteral("font-size:40px; font-weight:bold; color:#1e9e56;")
           : QStringLiteral("font-size:40px; font-weight:bold; color:#b3791d;"));
}
```

每次数据刷新，**按条件重设内联样式**——达标绿、未达标琥珀。这是 Qt 动态配色的标准做法。

温度卡片同理：`k < 100.0` 判定深低温，显示冰蓝 `#1f7fb8` 大字；否则深灰。

### 7.3 相态颜色映射

```cpp
void OperatorWidget::onPhaseChanged(const QString &phase)
{
    m_phaseLabel->setText(phase);
    QString color = QStringLiteral("#5b6472");   // 默认灰
    if (phase.contains(QStringLiteral("运行")) || phase.contains(QStringLiteral("安全")))
        color = QStringLiteral("#1e9e56");       // 绿
    else if (phase.contains(QStringLiteral("回温")) || phase.contains(QStringLiteral("粗抽"))
             || phase.contains(QStringLiteral("高真空")))
        color = QStringLiteral("#b3791d");       // 琥珀
    else if (phase.contains(QStringLiteral("降温")))
        color = QStringLiteral("#1f7fb8");       // 冰蓝
    m_phaseLabel->setStyleSheet(
        QStringLiteral("font-size:30px; font-weight:bold; color:%1;").arg(color));
}
```

用 `QString::contains` 做**关键词匹配**（"系统已安全回温"同时含"回温"和"安全"，先判"安全"得绿），比精确比较字符串更省事、更抗改词。

### 7.4 防呆锁（放空按钮）

```cpp
// 构造时：
m_btnVent->setEnabled(false);    // 初始灰暗禁用

// 温度刷新槽内：
if (k >= 290.0 && !m_ventEnabled) {
    m_ventEnabled = true;
    m_btnVent->setEnabled(true);     // 解锁
} else if (k < 290.0 && m_ventEnabled) {
    m_ventEnabled = false;
    m_btnVent->setEnabled(false);    // 温度回落则重新上锁
}
```

界面层的锁 + MockSystem 层的三重守卫，双保险。禁用态外观由 QSS `QPushButton#VentButton:disabled` 控制（灰化）。

### 7.5 命令出口

```cpp
connect(m_btnStart, &QPushButton::clicked, this, &OperatorWidget::startRequested);
// clicked(bool) → 无参信号 startRequested()，多余参数自动丢弃
```

按钮点击只发出**语义命令信号**，不直接操作任何控制逻辑。

---

## 8. 工程师模式界面：EngineerWidget

### 8.1 整体结构

```cpp
EngineerWidget (QVBoxLayout)
└─ QTabWidget m_tabs
    ├─ Tab 0 "硬件单动"   ← buildManualTab()
    ├─ Tab 1 "IO 与诊断"  ← buildDiagnosticsTab()
    └─ Tab 2 "系统参数"   ← buildConfigTab()
```

QTabWidget 自带页签栏，`addTab(widget, 标题)` 加入页面。

### 8.2 文件顶部匿名命名空间：工具函数

```cpp
namespace {   // 匿名命名空间 = 本文件私有的静态函数

void setLedState(QLabel *led, bool on, const QString &colorOn = QStringLiteral("#1e9e56"))
{
    const int r = led->width() / 2;
    led->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #b7bcc4;"
                                      " border-radius:%2px;")
                           .arg(on ? colorOn : QStringLiteral("#d8dde2")).arg(r));
}

QLabel *makeLed(QWidget *parent, int size = 14) { ... }        // 造一个灰灯
QWidget *makeRow(QWidget *parent, QLabel *led, QPushButton *b1, QPushButton *b2) { ... }
QDoubleSpinBox *makeDoubleSpin(QWidget *parent, double lo, double hi, int dec, double val) { ... }

} // namespace
```

`setLedState` 的 `r = led->width()/2` 把圆角半径设为尺寸一半 = 正圆。**默认参数** `colorOn = "#1e9e56"` 让调用方不用传绿色。这些辅助函数只在本文件用，匿名命名空间等价于 static，避免污染全局符号。

### 8.3 Tab1 硬件单动

**阀门控制区**（QFormLayout）：每行 = 名称标签 + [LED][开启][关闭]。

```cpp
struct ValveRow {                       // 一个阀门的"句柄包"
    QLabel *led = nullptr;
    QPushButton *onBtn = nullptr;
    QPushButton *offBtn = nullptr;
};

ValveRow EngineerWidget::makeValveRow(const QString &name, QFormLayout *form)
{
    ValveRow r;
    QWidget *parent = form->parentWidget();      // 布局所属的 GroupBox
    auto *nameLbl = new QLabel(name, parent);
    r.led = makeLed(parent);
    r.onBtn = new QPushButton(QStringLiteral("开启"), parent);
    r.offBtn = new QPushButton(QStringLiteral("关闭"), parent);
    form->addRow(nameLbl, makeRow(parent, r.led, r.onBtn, r.offBtn));
    return r;
}
```

返回的 `ValveRow` 存进成员，之后接线与状态回显都用它——**把"一组相关控件"打包成结构体**是本工程的小设计模式。

接线用 lambda 转发为语义信号：

```cpp
connect(m_gateRow.onBtn, &QPushButton::clicked, this, [this] { emit gateValveSet(true); });
connect(m_gateRow.offBtn, &QPushButton::clicked, this, [this] { emit gateValveSet(false); });
```

**泵类与制冷区**：干泵/TMP 启动停止 + LED；TMP 转速 `QSpinBox`（0~90000 rpm，步进 1000，后缀 " rpm"）+ "设置转速"按钮；制冷机使能/禁用。

**加热控制区**：`QCheckBox` 使能 + 三个 PID `QDoubleSpinBox` + 目标温度 + "应用 PID 参数"按钮。

```cpp
connect(m_heaterChk, &QCheckBox::toggled, this, &EngineerWidget::heaterSet);
// 勾选状态变化 → 转发 heaterSet(bool) → MockSystem::setHeater
```

**系统状态区**：三个标签实时显示相态/真空/温度（复用 `onPhaseChanged` 等槽）。

布局用 `QGridLayout` 2×2：

```cpp
grid->addWidget(valveBox, 0, 0);   // 行, 列
grid->addWidget(pumpBox, 0, 1);
grid->addWidget(heatBox, 1, 0);
grid->addWidget(stBox, 1, 1);
grid->setColumnStretch(0, 1);      // 两列等宽
grid->setColumnStretch(1, 1);
```

### 8.4 Tab2 IO 与诊断

- **DI 数字量输入**：`QGridLayout` 3 列 × 2 行，每项 [名称][LED][状态文字]。名称用 `static const char *kDiNames[]` 数组存，循环生成控件：

  ```cpp
  static const char *kDiNames[kDiCount] = {
      "水冷流量开关", "急停按钮", "看门狗心跳",
      "前级门联锁", "加热超温保护", "制冷机高压保护"
  };
  for (int i = 0; i < kDiCount; ++i) {
      auto *name = new QLabel(QString::fromUtf8(kDiNames[i]), diBox);
      m_diLed[i] = makeLed(diBox, 12);
      m_diText[i] = new QLabel(QStringLiteral("—"), diBox);
      diGrid->addWidget(name, i / 3, (i % 3) * 3);        // 3列排布
      diGrid->addWidget(m_diLed[i], i / 3, (i % 3) * 3 + 1);
      diGrid->addWidget(m_diText[i], i / 3, (i % 3) * 3 + 2);
  }
  ```

  `QString::fromUtf8` 把 const char* 中文转成 QString（源码 UTF-8 下直接用 QStringLiteral 也行，这里展示 fromUtf8 用法）。

- **传感器模拟量**：6 个 [名称][值] 标签，值右对齐等宽字体（`Qt::AlignRight | Qt::AlignVCenter` 位或组合对齐）。
- **原始报文/事件日志**：只读 `QPlainTextEdit`，`setMaximumBlockCount(300)` 自动滚动丢旧行，`NoWrap` 不换行。

```cpp
void EngineerWidget::onLogMessage(const QString &msg)
{
    if (m_log)
        m_log->appendPlainText(msg);
}
```

### 8.5 Tab3 系统参数

`QFormLayout` 四个 `QDoubleSpinBox`（粗抽门限 5.0 Pa / 高真空门限 0.001 Pa / 降温速率 / 回温速率），"恢复默认"和"保存参数"按钮。保存时把四个值打包成 `configApplied(4×double)` 信号发给 Mock：

```cpp
connect(saveBtn, &QPushButton::clicked, this, [this] {
    emit configApplied(m_cfgRough->value(), m_cfgHigh->value(),
                       m_cfgCoolRate->value(), m_cfgWarmRate->value());
});
```

### 8.6 状态回显槽（简短版）

```cpp
void EngineerWidget::onGateValveChanged(bool open) { setLedState(m_gateRow.led, open); }
void EngineerWidget::onTmpSpeedChanged(int rpm)    { m_tmpSpeed->setValue(rpm); }
void EngineerWidget::onHeaterChanged(bool on)      { m_heaterChk->setChecked(on); }
```

一行一个槽：收到状态 → 点亮/熄灭对应 LED、回写控件值。注意 `setValue/setChecked` 会触发控件的信号（valueChanged/toggled），但因为 Mock 的槽有"同值早退"保护，不会形成死循环。

---

## 9. 亮色工业风 QSS 详解

全局 QSS 是 `MainWindow.cpp` 里的一个原始字符串常量（C++11 的 `R"(...)"` 语法，引号内原样输出，不需要转义），构造时注入：

```cpp
const char *kDarkQss = R"(
* { font-family: "Noto Sans CJK SC", ..., sans-serif; }   /* 通配选择器：全局字体 */
QMainWindow, QDialog, ... { background-color: #eef0f3; color: #1f2430; }
QLabel#AppTitle { font-size: 20px; font-weight: bold; }
QFrame#Card { background-color: #ffffff; border: 1px solid #d0d4d9; border-radius: 10px; }
QPushButton { ... }
QPushButton:hover { ... }      /* 悬停 */
QPushButton:pressed { ... }    /* 按下 */
QPushButton:disabled { ... }   /* 禁用：灰化，防呆锁的视觉表现 */
QPushButton#StartButton { background-color: #1e7d4f; }   /* #objectName 精确命中 */
QGroupBox::title { ... }       /* 子控件选择器 */
QTabBar::tab:selected { border-top: 2px solid #2d7dd2; }
QSpinBox::up-button { ... }    /* QSpinBox 内部的上下箭头子控件 */
)";
```

**亮色主题配色约定**（当前版本）：

| 角色 | 色值 |
|------|------|
| 主背景 | `#eef0f3` |
| 卡片/顶栏/页签选中底 | `#ffffff` |
| GroupBox 底 | `#fafafa` |
| 边框 | `#d0d4d9` |
| 主文字 | `#1f2430` |
| 次级文字 | `#5b6472` |
| 主操作蓝（PrimaryButton/焦点） | `#2d7dd2` |
| 启动绿 | `#1e7d4f` |
| 回温琥珀 | `#c97a1c` |
| 放空红 | `#c53434` |
| 禁用灰 | `#d8dde2`（文字 `#98a0aa`） |
| 状态绿/琥珀/冰蓝（内联） | `#1e9e56` / `#b3791d` / `#1f7fb8` |

**样式优先级**（从低到高）：全局 QSS 类型选择器 → `#objectName` 选择器 → 控件内联 setStyleSheet。内联样式只覆盖它声明的属性，其余属性仍从全局 QSS 继承（例如 ModeChip 的边框背景来自 QSS，颜色由内联动态覆盖）。

---

## 10. 代码中值得注意的语法点（问答式）

**Q1：头文件里 `class QLabel;` 是什么意思？**
前置声明。头文件里只用指针（`QLabel *m_x`）时不需要完整定义，能显著减少编译依赖。.cpp 里再 `#include <QLabel>`。

**Q2：`enum class` 和 `enum` 有什么区别？**
`enum class Phase { ... }` 是强类型枚举：必须写 `Phase::Rough`，且不能隐式转 int，避免命名冲突和误用。MockSystem 的 `DiIndex`/`SensorIndex` 用普通 `enum` 是因为要当数组下标（`m_di[DiCount]`）并与其他 int 混算，普通枚举的 `DiCount` 值可直接作常量用。

**Q3：`R"(...)"` 是什么？**
C++11 原始字符串字面量：`R"(...)"` 之间所有字符原样保留，写 QSS 不用转义引号/反斜杠。

**Q4：为什么构造函数总写 `: QWidget(parent)`？**
初始化列表，调用基类构造函数传入 parent。Qt 控件类都要求 `QWidget *parent = nullptr` 参数，用于父子内存管理。

**Q5：`auto *v = new QVBoxLayout(this);` 为什么 layout 也传 this？**
布局接管了 this 的排版，且随 this 一起销毁。控件则通过 `new QLabel(parent)` 或 `addWidget` 指定归属。

**Q6：信号和槽重名怎么办？**
本工程就有：MockSystem 的 `setHeater(bool)` 槽 与 EngineerWidget 的 `heaterSet(bool)` 信号。只要不冲突即可；刻意用"命令动词 + 状态过去式"区分：`setXxx`（命令）/ `xxxChanged`（状态反馈）。

**Q7：`QStringLiteral` 和 `QString::fromUtf8` 何时用哪个？**
源码内直接写中文用 `QStringLiteral`（编译期构造，最快）；从 `const char*` 变量（如 `kDiNames` 数组）转换用 `QString::fromUtf8`。

**Q8：为什么 `.arg()` 链式调用要小心？**
`QStringLiteral("%1 %2").arg(a).arg(b)` 中 `%1`→a、`%2`→b。但 `%` 后跟数字的格式串里若 `%2` 的替换文本含 `%1` 会再被替换（少数字符串有坑）。本工程用法都是安全的。**带格式的数值**用 `arg(数值, 宽度, 格式字符, 精度)`，如 `arg(pa, 0, 'e', 2)`。

**Q9：`std::min` / `std::max` 与 Qt 混用？**
标准库 `<algorithm>` 的 `std::min/max` 用于数值夹取（`std::max(2.8, m_temp - step)`）。与 Qt 不冲突。注意两者参数类型要一致。

**Q10：`const char *kDiNames[kDiCount]` 里 `kDiCount` 从哪来？**
`EngineerWidget` 里的 `static constexpr int kDiCount = 6;`（C++17 内联常量，可直接作数组大小）。与 MockSystem 的 `DiCount` 枚举值保持一致（都是 6）。

**Q11：为什么 OperatorWidget 的头文件里没有 `Q_OBJECT` 之外的特殊声明？**
它的信号（startRequested 等）和槽（onVacuumChanged 等）都要靠 Q_OBJECT 的元对象系统工作，所以必须有 Q_OBJECT；其余和普通类无异。

**Q12：`qApp` 是什么？**
`QApplication` 的全局单例指针宏（等价 `QCoreApplication::instance()`），任何地方可用。`qApp->setStyleSheet(...)` 设置全局样式。

---

## 11. 扩展指引：如何接入真实 FSM 与硬件

**当前约束**：UI 只认识两组接口——
1. 从控制器发出的**状态信号**（vacuumChanged、temperatureChanged、phaseChanged、heartbeat、各 xxxChanged、diChanged、sensorRawChanged、logMessage、highLimitChanged）；
2. 接收的**命令槽**（startSystem、stopAndWarmUp、confirmVent、setGateValve…、setPidParams、applyConfig、resetConfig）。

**替换步骤**：
1. 新建 `RealController`（QObject），保持与 MockSystem **完全相同的信号与槽签名**（可从 MainWindow.h 复制声明）；
2. 在槽内部把"数学模拟"换成真实动作：串口/网口下发指令、读取传感器、跑真实 FSM 状态表；
3. 把 `MainWindow` 里的 `new MockSystem(this)` 换成 `new RealController(this)`；
4. `wireSystem()` 一行不用改（信号槽按名字解析的是编译期类型，签名一致即可）。

**扩展提示**：
- 需要更多传感器时：在两边枚举（SensorIndex / kSnsNames）各加一项，加一行 emit 与一行槽逻辑；
- 需要串口：链接 `Qt6::SerialPort`，`find_package(Qt6 COMPONENTS SerialPort)`；
- 需要持久化参数：在 `applyConfig` 里写 `QSettings`（Qt 自带 INI/注册表存储）。

---

*文档生成日期：2026-08-11 · 对应工程当前状态（亮色主题版）。*
