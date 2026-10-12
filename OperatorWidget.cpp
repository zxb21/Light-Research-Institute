#include "OperatorWidget.h"

#include "OperatorComponents.h"
#include "TelemetryStore.h"
#include "TrendChartWidget.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {

QLabel *makeLabel(const QString &text, const char *objectName, QWidget *parent = nullptr)
{
    // objectName 是 QSS 定位样式的名字；要改这一类标签外观可在 operator.qss 中搜索它。
    auto *result = new QLabel(text, parent);
    result->setObjectName(QLatin1String(objectName));
    return result;
}

QIcon fluentIcon(ushort glyph, const QColor &color, int pixels)
{
    // 顶部工具按钮和安全按钮复用 Windows 图标库，颜色由调用处明确指定。
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(color);
    QFont font(QStringLiteral("Segoe MDL2 Assets"));
    font.setPixelSize(pixels - 2);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(glyph)));
    return QIcon(pixmap);
}

QString stepName(ProcessStep step)
{
    // 七步骤中文标题集中在这里；改流程栏和详情标题文字时修改此表。
    static const QStringList names = {
        QStringLiteral("系统准备"),
        QStringLiteral("粗抽真空（干泵）"),
        QStringLiteral("高真空建立（分子泵）"),
        QStringLiteral("预冷"),
        QStringLiteral("深冷至目标温度"),
        QStringLiteral("恒温与实验运行"),
        QStringLiteral("放空与恢复")
    };
    return names.at(static_cast<int>(step));
}

QString stepDescription(ProcessStep step)
{
    // 详情页“本步骤工作”的固定说明；不参与状态机判断。
    switch (step) {
    case ProcessStep::Preparation:
        return QStringLiteral("确认通信、真空计、温控仪和保护条件正常。系统满足启动条件后，自动进入粗抽真空。");
    case ProcessStep::RoughVacuum:
        return QStringLiteral("启动干泵并打开前级阀，将腔体从大气压力抽至粗抽门限。达到门限后自动启动分子泵。");
    case ProcessStep::HighVacuum:
        return QStringLiteral("保持干泵和分子泵运行，持续降低腔体压力。达到高真空门限后关闭前级阀并建立主真空通路。");
    case ProcessStep::PreCooling:
        return QStringLiteral("启动冷水机、压缩机和温控仪，确认制冷系统工作稳定后进入受控降温。");
    case ProcessStep::DeepCooling:
        return QStringLiteral("按操作员设定的降温速率降低样品温度，同时持续监控真空度和设备状态。达到目标温度后进入稳定阶段。");
    case ProcessStep::StabilizeAndRun:
        return QStringLiteral("维持目标温度和高真空状态，等待温度稳定后进入实验运行。此阶段持续记录温度、真空和设备动作。");
    case ProcessStep::VentAndRecover:
        return QStringLiteral("停止制冷和泵组，完成受控回温。满足室温和安全条件后，允许操作员确认放空并恢复待机。");
    }
    return QString();
}

QString stepCondition(ProcessStep step)
{
    // 此处只说明被查看步骤的切换门限；实时状态说明不再伪装成“下一步条件”。
    switch (step) {
    case ProcessStep::Preparation:
        return QStringLiteral("通信、真空计、温控仪和保护条件均正常后，自动进入粗抽真空。");
    case ProcessStep::RoughVacuum:
        return QStringLiteral("腔体压强降至粗抽门限以下后，自动启动分子泵并进入高真空建立。");
    case ProcessStep::HighVacuum:
        return QStringLiteral("腔体压强降至高真空门限以下后，关闭前级阀、打开主真空通路并进入预冷。");
    case ProcessStep::PreCooling:
        return QStringLiteral("制冷系统完成预冷准备后，自动进入深冷。");
    case ProcessStep::DeepCooling:
        return QStringLiteral("样品温度降至目标温度后，自动进入恒温稳定阶段。");
    case ProcessStep::StabilizeAndRun:
        return QStringLiteral("温度进入稳定区间并保持稳定后，自动进入实验运行。");
    case ProcessStep::VentAndRecover:
        return QStringLiteral("设备输出安全关闭且样品回到室温安全区后，允许确认放空；放空完成后自动恢复待机。");
    }
    return QString();
}

QString stepStateText(StepState state)
{
    switch (state) {
    case StepState::Completed: return QStringLiteral("已完成");
    case StepState::Active: return QStringLiteral("进行中");
    case StepState::Interrupted: return QStringLiteral("已中止");
    case StepState::Faulted: return QStringLiteral("故障");
    case StepState::Recovering: return QStringLiteral("恢复中");
    case StepState::Pending: return QStringLiteral("等待");
    }
    return QString();
}

TrendChartWidget::Mode trendMode(ProcessStep step)
{
    // 每步只显示与操作相关的曲线，主界面本身不绘制实时趋势。
    switch (step) {
    case ProcessStep::RoughVacuum:
    case ProcessStep::HighVacuum:
        return TrendChartWidget::Mode::Pressure;
    case ProcessStep::PreCooling:
    case ProcessStep::DeepCooling:
        return TrendChartWidget::Mode::Temperature;
    case ProcessStep::Preparation:
    case ProcessStep::StabilizeAndRun:
    case ProcessStep::VentAndRecover:
        return TrendChartWidget::Mode::Combined;
    }
    return TrendChartWidget::Mode::Combined;
}

QString trendTitle(ProcessStep step)
{
    // 与 trendMode() 保持成对修改，避免标题与实际图表模式不一致。
    switch (step) {
    case ProcessStep::RoughVacuum:
    case ProcessStep::HighVacuum:
        return QStringLiteral("腔体真空度变化趋势");
    case ProcessStep::PreCooling:
    case ProcessStep::DeepCooling:
        return QStringLiteral("样品温度变化趋势");
    case ProcessStep::Preparation:
        return QStringLiteral("系统准备检查结果");
    case ProcessStep::StabilizeAndRun:
    case ProcessStep::VentAndRecover:
        return QStringLiteral("温度与真空变化趋势");
    }
    return QString();
}

QString trendAccessibleName(ProcessStep step)
{
    switch (trendMode(step)) {
    case TrendChartWidget::Mode::Temperature:
        return QStringLiteral("步骤趋势：样品温度");
    case TrendChartWidget::Mode::Pressure:
        return QStringLiteral("步骤趋势：腔体真空度");
    case TrendChartWidget::Mode::Combined:
        return QStringLiteral("步骤趋势：样品温度与腔体真空度");
    }
    return QString();
}

} // namespace

OperatorWidget::OperatorWidget(QWidget *parent)
    : QWidget(parent)
    , m_store(new TelemetryStore)
{
    // 内部页面只构建一次；点击步骤时通过 m_pages 切换，而不是创建新窗口。
    setObjectName(QStringLiteral("OperatorRoot"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildOverviewPage());
    m_pages->addWidget(buildDetailPage());
    layout->addWidget(m_pages);

    m_snapshot.state = SystemState::Idle;
    m_snapshot.stateText = QStringLiteral("待机");
    m_snapshot.detailText = QStringLiteral("系统处于大气安全状态，可开始新任务");
    m_processFlow->setSnapshot(m_snapshot);
    refreshSummary();
    refreshDetail();
    refreshCommands();
}

QWidget *OperatorWidget::buildOverviewPage()
{
    // 总览是“左流程、右信息”的水平布局。布局尺寸修改优先从本函数开始找。
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("OperatorOverview"));
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(16, 14, 16, 16);
    root->setSpacing(14);

    m_processFlow = new ProcessFlowWidget(page);
    // 左侧流程固定宽度，防止七个中文步骤名称被右侧区域挤压。
    m_processFlow->setFixedWidth(370);
    // 流程控件只发出步骤编号；页面切换由本类统一处理。
    connect(m_processFlow, &ProcessFlowWidget::stepActivated,
            this, &OperatorWidget::showProcessDetail);
    root->addWidget(m_processFlow);

    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    // 右侧从上到下：摘要、设备状态、事件、控制区。
    right->addWidget(buildSystemStatusPanel());

    auto *healthPanel = new QFrame(page);
    healthPanel->setObjectName(QStringLiteral("IndustrialPanel"));
    auto *healthLayout = new QVBoxLayout(healthPanel);
    healthLayout->setContentsMargins(16, 12, 16, 12);
    healthLayout->setSpacing(8);
    healthLayout->addWidget(makeLabel(QStringLiteral("设备运行状态"), "PanelTitle", healthPanel));
    m_deviceHealth = new DeviceHealthWidget(healthPanel);
    healthLayout->addWidget(m_deviceHealth);
    right->addWidget(healthPanel);

    auto *activityPanel = new QFrame(page);
    activityPanel->setObjectName(QStringLiteral("IndustrialPanel"));
    auto *activityLayout = new QVBoxLayout(activityPanel);
    activityLayout->setContentsMargins(16, 12, 16, 12);
    activityLayout->setSpacing(6);
    m_overviewEventsTitle = makeLabel(QStringLiteral("最近一次运行"), "PanelTitle", activityPanel);
    activityLayout->addWidget(m_overviewEventsTitle);
    m_overviewEvents = new QListWidget(activityPanel);
    m_overviewEvents->setObjectName(QStringLiteral("EventList"));
    m_overviewEvents->addItem(QStringLiteral("暂无运行记录"));
    activityLayout->addWidget(m_overviewEvents);
    right->addWidget(activityPanel, 1);
    right->addWidget(buildControlPanel());
    root->addLayout(right, 1);
    return page;
}

QWidget *OperatorWidget::buildSystemStatusPanel()
{
    // 摘要区的标签在 refreshSummary() 中更新；这里仅决定三栏布局和初始样式名。
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("IndustrialPanel"));
    // 原 158 像素高度会在三栏文字下方留下大块空白；压缩后的空间会由设备栏和记录区使用。
    panel->setFixedHeight(132);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(20);

    auto *stateBlock = new QVBoxLayout;
    stateBlock->setSpacing(5);
    stateBlock->addStretch();
    stateBlock->addWidget(makeLabel(QStringLiteral("系统当前状态"), "MetricCaption", panel));
    m_stateValue = makeLabel(QStringLiteral("待机"), "SystemStateValue", panel);
    stateBlock->addWidget(m_stateValue);
    m_stateDetail = makeLabel(QString(), "SystemStateDetail", panel);
    m_stateDetail->setWordWrap(true);
    stateBlock->addWidget(m_stateDetail);
    stateBlock->addStretch();
    layout->addLayout(stateBlock, 4);

    auto *separator = new QFrame(panel);
    separator->setObjectName(QStringLiteral("VerticalSeparator"));
    separator->setFrameShape(QFrame::VLine);
    layout->addWidget(separator);

    // 温度与真空使用相同的“标题 + 数值 + 两行说明”结构，避免重复创建布局代码。
    const auto addMetric = [panel, layout](const QString &title, QLabel *&value,
                                            QLabel *&lineOne, QLabel *&lineTwo) {
        auto *block = new QVBoxLayout;
        block->setSpacing(5);
        block->addStretch();
        block->addWidget(makeLabel(title, "MetricCaption", panel));
        value = makeLabel(QStringLiteral("--"), "IndustrialMetricValue", panel);
        block->addWidget(value);
        lineOne = makeLabel(QString(), "IndustrialMetricMeta", panel);
        lineTwo = makeLabel(QString(), "IndustrialMetricMeta", panel);
        block->addWidget(lineOne);
        block->addWidget(lineTwo);
        block->addStretch();
        layout->addLayout(block, 3);
    };

    QLabel *temperatureStatus = nullptr;
    QLabel *pressureStatus = nullptr;
    addMetric(QStringLiteral("样品温度"), m_temperatureValue, m_temperatureTarget,
              m_temperatureRate);
    addMetric(QStringLiteral("腔体真空"), m_pressureValue, m_pressureLimit,
              pressureStatus);
    temperatureStatus = m_temperatureRate;
    Q_UNUSED(temperatureStatus)
    Q_UNUSED(pressureStatus)
    return panel;
}

QWidget *OperatorWidget::buildControlPanel()
{
    // 控制区采用“工具带 + 连续流程操作轨 + 独立安全区”。
    // 正常动作按真实顺序连成一个整体，安全停止单独隔开，避免再退化成两排按钮罗列。
    // 所有按钮只改变可用状态，不改变位置或可见性，具体权限由 refreshCommands() 决定。
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("IndustrialPanel"));
    // 恢复按钮布局改造前的 144 像素总高度，把垂直空间留给上方运行记录。
    // 内部仍保留“工具区 + 分隔线 + 流程区”的一甲结构，只压缩各部分高度。
    panel->setFixedHeight(144);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部只放参数和记录类工具，它们不与下方流程动作争夺视觉重点。
    auto *toolSection = new QWidget(panel);
    toolSection->setFixedHeight(53);
    auto *toolRow = new QHBoxLayout(toolSection);
    toolRow->setContentsMargins(26, 6, 20, 7);
    toolRow->setSpacing(10);
    toolRow->addWidget(makeLabel(QStringLiteral("降温速率"), "ControlLabel", toolSection));
    m_coolingRate = new QDoubleSpinBox(toolSection);
    m_coolingRate->setRange(0.1, 5.0);
    m_coolingRate->setDecimals(1);
    // 修改操作员每次点箭头增加多少温度速率，在这里调整。
    m_coolingRate->setSingleStep(0.5);
    m_coolingRate->setValue(1.0);
    m_coolingRate->setSuffix(QStringLiteral(" K/min"));
    m_coolingRate->setFixedWidth(144);
    m_coolingRate->setAccessibleName(QStringLiteral("降温速率，单位开尔文每分钟"));
    toolRow->addWidget(m_coolingRate);
    toolRow->addStretch();

    m_runHistoryButton = new QPushButton(QStringLiteral("运行记录"), toolSection);
    m_runHistoryButton->setObjectName(QStringLiteral("UtilityButton"));
    m_runHistoryButton->setFixedSize(141, 40);
    m_runHistoryButton->setIcon(fluentIcon(0xE9D5, QColor(QStringLiteral("#405169")), 18));
    m_runHistoryButton->setIconSize(QSize(18, 18));
    toolRow->addWidget(m_runHistoryButton);
    m_exportButton = new QPushButton(QStringLiteral("导出数据"), toolSection);
    m_exportButton->setObjectName(QStringLiteral("ExportButton"));
    m_exportButton->setFixedSize(166, 40);
    m_exportButton->setIcon(fluentIcon(0xE896, Qt::white, 18));
    m_exportButton->setIconSize(QSize(18, 18));
    toolRow->addWidget(m_exportButton);
    layout->addWidget(toolSection);

    // 目标图用一条完整横线分开参数工具区和主操作区。
    auto *toolDivider = new QFrame(panel);
    toolDivider->setObjectName(QStringLiteral("ControlToolDivider"));
    toolDivider->setFixedHeight(1);
    layout->addWidget(toolDivider);

    auto *actionSection = new QWidget(panel);
    auto *actionRow = new QHBoxLayout(actionSection);
    actionRow->setContentsMargins(16, 10, 20, 10);
    actionRow->setSpacing(12);

    // 三个正常动作使用零间距布局，共享 CommandRail 外框，形成一条连续的操作流程。
    // 修改三个动作的宽度比例、顺序或整体高度，应优先在这一段调整。
    auto *commandRail = new FlowCommandRail(panel);
    commandRail->setObjectName(QStringLiteral("CommandRail"));
    commandRail->setFixedHeight(70);
    commandRail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const auto addFlowButton = [commandRail](QPushButton *&button,
                                                         const QString &text,
                                                         const QString &objectName,
                                                         const QString &accessibleName,
                                                         int step,
                                                         FlowCommandButton::Position position,
                                                         FlowCommandButton::Symbol symbol) {
        button = new FlowCommandButton(text, step, position, symbol, commandRail);
        button->setObjectName(objectName);
        button->setFixedHeight(68);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setAccessibleName(accessibleName);
    };
    addFlowButton(m_startButton, QStringLiteral("一键启动"), QStringLiteral("FlowStartButton"),
                  QStringLiteral("一键启动系统"), 1, FlowCommandButton::Position::First,
                  FlowCommandButton::Symbol::Play);
    addFlowButton(m_stopButton, QStringLiteral("停止并受控回温"), QStringLiteral("FlowStopButton"),
                  QStringLiteral("停止并受控回温"), 2, FlowCommandButton::Position::Middle,
                  FlowCommandButton::Symbol::Stop);
    addFlowButton(m_ventButton, QStringLiteral("确认室温放空"), QStringLiteral("FlowVentButton"),
                  QStringLiteral("确认室温放空"), 3, FlowCommandButton::Position::Last,
                  FlowCommandButton::Symbol::Vent);
    commandRail->setButtons(static_cast<FlowCommandButton *>(m_startButton),
                            static_cast<FlowCommandButton *>(m_stopButton),
                            static_cast<FlowCommandButton *>(m_ventButton));
    actionRow->addWidget(commandRail, 1);

    // 安全停止不属于正常流程轨，使用单独的安全区隔离；复位固定在其下方。
    // 两个按钮在所有状态下都保留位置，仅通过 enabled 表达当前是否允许操作。
    auto *safetyArea = new QFrame(panel);
    safetyArea->setObjectName(QStringLiteral("SafetyCommandArea"));
    safetyArea->setMinimumWidth(150);
    safetyArea->setMaximumWidth(165);
    safetyArea->setFixedHeight(70);
    safetyArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *safetyLayout = new QVBoxLayout(safetyArea);
    safetyLayout->setContentsMargins(12, 0, 0, 0);
    safetyLayout->setSpacing(4);
    m_safetyStopButton = new QPushButton(QStringLiteral("安全停止"), safetyArea);
    m_safetyStopButton->setObjectName(QStringLiteral("SafetyStopButton"));
    m_safetyStopButton->setFixedHeight(40);
    // 系统图标库中的实心盾牌带有十字，和目标图不符；这里直接按目标绘制
    // 实心白盾牌与红色感叹号，不改变按钮本身的红底和文字排版。
    QPixmap safetyIconPixmap(24, 24);
    safetyIconPixmap.fill(Qt::transparent);
    QPainter safetyIconPainter(&safetyIconPixmap);
    safetyIconPainter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shield;
    shield.moveTo(12.0, 1.5);
    shield.lineTo(21.0, 5.0);
    shield.lineTo(21.0, 12.0);
    shield.cubicTo(21.0, 16.5, 16.7, 20.5, 12.0, 22.5);
    shield.cubicTo(7.3, 20.5, 3.0, 16.5, 3.0, 12.0);
    shield.lineTo(3.0, 5.0);
    shield.closeSubpath();
    safetyIconPainter.setPen(Qt::NoPen);
    safetyIconPainter.setBrush(Qt::white);
    safetyIconPainter.drawPath(shield);
    safetyIconPainter.setBrush(QColor(QStringLiteral("#C62828")));
    safetyIconPainter.drawRoundedRect(QRectF(11.0, 6.0, 2.0, 8.0), 1.0, 1.0);
    safetyIconPainter.drawEllipse(QRectF(11.0, 16.5, 2.0, 2.0));
    m_safetyStopButton->setIcon(QIcon(safetyIconPixmap));
    m_safetyStopButton->setIconSize(QSize(24, 24));
    m_safetyStopButton->setAccessibleName(QStringLiteral("安全停止"));
    safetyLayout->addWidget(m_safetyStopButton);
    m_resetButton = new QPushButton(QStringLiteral("复位"), safetyArea);
    m_resetButton->setObjectName(QStringLiteral("ResetButton"));
    m_resetButton->setFixedHeight(26);
    m_resetButton->setIcon(fluentIcon(0xE777, QColor(QStringLiteral("#8D99A8")), 17));
    m_resetButton->setIconSize(QSize(17, 17));
    safetyLayout->addWidget(m_resetButton);
    actionRow->addWidget(safetyArea);
    layout->addWidget(actionSection);

    // 点击后只发请求；控制器检查安全条件后才会真正推进流程。
    connect(m_startButton, &QPushButton::clicked, this, [this] {
        onLogMessage(QStringLiteral("已接受启动请求，正在进入自动流程"));
        emit startRequested(m_coolingRate->value());
    });
    connect(m_stopButton, &QPushButton::clicked, this, &OperatorWidget::stopRequested);
    connect(m_safetyStopButton, &QPushButton::clicked, this, &OperatorWidget::safetyStopRequested);
    connect(m_resetButton, &QPushButton::clicked, this, &OperatorWidget::resetRequested);
    connect(m_ventButton, &QPushButton::clicked, this, &OperatorWidget::confirmVent);
    connect(m_exportButton, &QPushButton::clicked, this, &OperatorWidget::showExportDialog);
    connect(m_runHistoryButton, &QPushButton::clicked, this, &OperatorWidget::runHistoryRequested);
    // Tab 顺序与视觉阅读顺序一致：先顶部工具带，再从左到右经过流程轨和安全区。
    QWidget::setTabOrder(m_coolingRate, m_runHistoryButton);
    QWidget::setTabOrder(m_runHistoryButton, m_exportButton);
    QWidget::setTabOrder(m_exportButton, m_startButton);
    QWidget::setTabOrder(m_startButton, m_stopButton);
    QWidget::setTabOrder(m_stopButton, m_ventButton);
    QWidget::setTabOrder(m_ventButton, m_safetyStopButton);
    QWidget::setTabOrder(m_safetyStopButton, m_resetButton);
    return panel;
}

QWidget *OperatorWidget::buildDetailPage()
{
    // 详情页固定为：顶部返回栏、左说明/条件、右趋势或检查结果、底部事件记录。
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("ProcessDetailPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(16, 14, 16, 16);
    root->setSpacing(12);

    auto *header = new QFrame(page);
    header->setObjectName(QStringLiteral("IndustrialPanel"));
    header->setMinimumHeight(72);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    auto *back = new QPushButton(QStringLiteral("返回主界面"), header);
    back->setObjectName(QStringLiteral("NeutralButton"));
    back->setMinimumSize(116, 40);
    headerLayout->addWidget(back);
    m_detailTitle = makeLabel(QStringLiteral("流程详情"), "DetailTitle", header);
    headerLayout->addWidget(m_detailTitle, 1);
    m_detailState = makeLabel(QString(), "DetailState", header);
    headerLayout->addWidget(m_detailState);
    root->addWidget(header);
    connect(back, &QPushButton::clicked, this, &OperatorWidget::showOverview);

    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    auto *description = new QFrame(page);
    description->setObjectName(QStringLiteral("IndustrialPanel"));
    description->setMinimumWidth(350);
    auto *descriptionLayout = new QVBoxLayout(description);
    descriptionLayout->setContentsMargins(18, 16, 18, 16);
    descriptionLayout->setSpacing(10);
    descriptionLayout->addWidget(makeLabel(QStringLiteral("本步骤工作"), "PanelTitle", description));
    m_detailDescription = makeLabel(QString(), "DetailDescription", description);
    m_detailDescription->setWordWrap(true);
    descriptionLayout->addWidget(m_detailDescription);
    descriptionLayout->addSpacing(8);
    descriptionLayout->addWidget(makeLabel(QStringLiteral("进入下一步条件"), "PanelTitle", description));
    m_detailCondition = makeLabel(QString(), "DetailCondition", description);
    m_detailCondition->setWordWrap(true);
    descriptionLayout->addWidget(m_detailCondition);
    descriptionLayout->addStretch();
    top->addWidget(description, 3);

    auto *trend = new QFrame(page);
    trend->setObjectName(QStringLiteral("IndustrialPanel"));
    auto *trendLayout = new QVBoxLayout(trend);
    trendLayout->setContentsMargins(14, 12, 14, 12);
    trendLayout->setSpacing(6);
    m_detailTrendTitle = makeLabel(QStringLiteral("温度与真空变化趋势"), "PanelTitle", trend);
    trendLayout->addWidget(m_detailTrendTitle);
    // 同一张图按步骤切换模式；它读取 m_store，而不是直接订阅控制器。
    m_detailChart = new TrendChartWidget(TrendChartWidget::Mode::Combined, trend);
    m_detailChart->setStore(m_store);
    trendLayout->addWidget(m_detailChart, 1);
    m_detailTrendPlaceholder = makeLabel(QStringLiteral("本步骤不产生温度或真空变化曲线。\n请确认通信、设备在线状态和保护条件后启动自动流程。"),
                                         "DetailDescription", trend);
    m_detailTrendPlaceholder->setAlignment(Qt::AlignCenter);
    m_detailTrendPlaceholder->setWordWrap(true);
    m_detailTrendPlaceholder->setFocusPolicy(Qt::StrongFocus);
    m_detailTrendPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    trendLayout->addWidget(m_detailTrendPlaceholder, 1);
    top->addWidget(trend, 5);
    root->addLayout(top, 1);

    auto *events = new QFrame(page);
    events->setObjectName(QStringLiteral("IndustrialPanel"));
    events->setMinimumHeight(170);
    auto *eventsLayout = new QVBoxLayout(events);
    eventsLayout->setContentsMargins(16, 12, 16, 12);
    eventsLayout->setSpacing(6);
    eventsLayout->addWidget(makeLabel(QStringLiteral("设备动作与事件记录"), "PanelTitle", events));
    m_eventList = new QListWidget(events);
    m_eventList->setObjectName(QStringLiteral("EventList"));
    m_eventList->setFocusPolicy(Qt::StrongFocus);
    m_eventList->addItem(QStringLiteral("等待设备动作记录"));
    eventsLayout->addWidget(m_eventList, 1);
    root->addWidget(events);
    return page;
}

void OperatorWidget::onProcessSnapshot(const ProcessSnapshot &snapshot)
{
    // 流程状态变化时同时更新左侧流程、摘要、详情和按钮权限。
    m_snapshot = snapshot;
    m_processFlow->setSnapshot(snapshot);
    refreshSummary();
    refreshDetail();
    refreshCommands();
}

void OperatorWidget::onTelemetryReady(const TelemetryPoint &point)
{
    // 这是温度与压强的界面数据入口：缓存给详情图，同时更新总览摘要。
    m_temperatureK = point.temperatureK;
    m_pressurePa = point.vacuumPa;
    m_store->appendPoint(point);
    refreshSummary();
    m_detailChart->update();
}

void OperatorWidget::onDeviceStatus(const DeviceStatus &device)
{
    // 七设备状态带只做显示；真空计 keyValue 的生成位置在 MockSystem::publishDevices()。
    m_deviceHealth->setDeviceStatus(device);
}

void OperatorWidget::onHighLimitChanged(double pa)
{
    m_highLimitPa = pa;
    refreshSummary();
}

void OperatorWidget::onCommunicationChanged(bool connected)
{
    // 通信状态来自控制器，首页只显示它，不自行推测设备通信是否恢复。
    m_communicationConnected = connected;
    refreshSummary();
}

void OperatorWidget::onLogMessage(const QString &message)
{
    // 总览记录保留本次界面生命周期内的全部消息，新增后自动滚到最新一条。
    if (m_overviewEvents->count() == 1
        && m_overviewEvents->item(0)->text() == QStringLiteral("暂无运行记录"))
        delete m_overviewEvents->takeItem(0);
    m_overviewEvents->addItem(message);
    m_overviewEvents->scrollToBottom();
    if (m_eventList->count() == 1
        && m_eventList->item(0)->text() == QStringLiteral("等待设备动作记录"))
        delete m_eventList->takeItem(0);
    m_eventList->addItem(message);
    while (m_eventList->count() > 200)
        delete m_eventList->takeItem(0);
    m_eventList->scrollToBottom();
}

void OperatorWidget::onExportAvailabilityChanged(bool available)
{
    m_hasExportableRun = available;
    m_exportButton->setEnabled(available);
    m_exportButton->setToolTip(available ? QString() : QStringLiteral("暂无有效运行数据，不能导出空文件"));
}

void OperatorWidget::onRunFinished(const RunSummary &summary)
{
    m_runFinished = true;
    m_overviewEventsTitle->setText(QStringLiteral("本次运行结果"));
    // 结束摘要继续追加到过程记录末尾，不能清空操作员刚刚看到的历史消息。
    const auto valueOrNone = [](double value, int decimals, const QString &unit) {
        return std::isfinite(value) ? QString::number(value, 'f', decimals) + unit
                                    : QStringLiteral("暂无数据");
    };
    m_overviewEvents->addItems({
        QStringLiteral("开始时间：%1").arg(summary.startedAt.isValid()
            ? summary.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("暂无数据")),
        QStringLiteral("结束时间：%1").arg(summary.endedAt.isValid()
            ? summary.endedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("暂无数据")),
        QStringLiteral("运行结果：%1").arg(summary.result.isEmpty() ? QStringLiteral("暂无数据") : summary.result),
        QStringLiteral("最后阶段：%1").arg(summary.lastPhase.isEmpty() ? QStringLiteral("暂无数据") : summary.lastPhase),
        QStringLiteral("最低样品温度：%1").arg(valueOrNone(summary.lowestTemperatureK, 1, QStringLiteral(" K"))),
        QStringLiteral("结束时真空度：%1").arg(std::isfinite(summary.finalVacuumPa)
            ? formatPressure(summary.finalVacuumPa) + QStringLiteral(" Pa") : QStringLiteral("暂无数据")),
        QStringLiteral("报警数量：%1").arg(summary.alarmCount),
        QStringLiteral("数据目录：%1").arg(summary.directory.isEmpty() ? QStringLiteral("暂无数据") : summary.directory)
    });
    m_overviewEvents->scrollToBottom();
}

void OperatorWidget::refreshSummary()
{
    // 真空摘要与真空计、趋势图共用 formatPressure()，修改压强显示格式只需改公共函数。
    const bool active = m_snapshot.state >= SystemState::Preparing
        && m_snapshot.state <= SystemState::Running;
    if (m_snapshot.state == SystemState::Fault || m_snapshot.state == SystemState::ResetRequired) {
        m_stateValue->setText(m_snapshot.state == SystemState::Fault
            ? QStringLiteral("故障待处理") : QStringLiteral("等待复位"));
    } else if (m_snapshot.state == SystemState::SafetyStopping) {
        m_stateValue->setText(QStringLiteral("安全停止中"));
    } else if (!m_communicationConnected) {
        m_stateValue->setText(QStringLiteral("通信异常"));
    } else if (m_snapshot.commands.canStart) {
        m_stateValue->setText(QStringLiteral("可启动"));
    } else if (active) {
        m_stateValue->setText(QStringLiteral("运行正常"));
    } else {
        m_stateValue->setText(QStringLiteral("未就绪"));
    }
    m_stateDetail->setText(m_snapshot.detailText);
    m_temperatureValue->setText(QStringLiteral("%1 K").arg(m_temperatureK, 0, 'f', 1));
    m_temperatureTarget->setText(QStringLiteral("目标温度：2.8 K"));
    m_temperatureRate->setText(QStringLiteral("设定速率：%1 K/min").arg(
        m_coolingRate ? m_coolingRate->value() : 1.0, 0, 'f', 1));
    m_pressureValue->setText(formatPressure(m_pressurePa)
                             + QStringLiteral(" Pa"));
    m_pressureLimit->setText(formatPressure(m_highLimitPa)
                             + QStringLiteral(" Pa 高真空门限"));
    if (m_snapshot.state == SystemState::Fault || m_snapshot.state == SystemState::ResetRequired) {
        m_overviewEventsTitle->setText(QStringLiteral("故障处理记录"));
    } else if (active) {
        m_runFinished = false;
        m_overviewEventsTitle->setText(QStringLiteral("本次运行记录"));
    } else if (!m_runFinished) {
        m_overviewEventsTitle->setText(QStringLiteral("最近一次运行"));
    }
}

void OperatorWidget::refreshCommands()
{
    // CommandAvailability 由状态机计算；此处只翻译为可用状态、工具提示和只读属性。
    // 安全停止与复位始终可见，不能在状态变化时调用 setVisible()，否则布局会跳动。
    const CommandAvailability &commands = m_snapshot.commands;
    m_startButton->setEnabled(commands.canStart);
    m_stopButton->setEnabled(commands.canStop);
    m_ventButton->setEnabled(commands.canVent);
    m_safetyStopButton->setEnabled(commands.canSafetyStop);
    m_resetButton->setEnabled(commands.canReset);
    m_coolingRate->setReadOnly(m_snapshot.state != SystemState::Idle);
    m_startButton->setToolTip(commands.canStart ? QString() : commands.startReason);
    m_stopButton->setToolTip(commands.canStop ? QString() : commands.stopReason);
    m_ventButton->setToolTip(commands.canVent ? QString() : commands.ventReason);
    m_safetyStopButton->setToolTip(commands.canSafetyStop
                                      ? QString()
                                      : QStringLiteral("当前没有需要安全停止的运行流程"));
    m_resetButton->setToolTip(commands.canReset ? QString() : commands.resetReason);
}

void OperatorWidget::refreshDetail()
{
    // 选中的详情步骤可能不同于正在执行步骤，所以标题用 m_detailStep，状态用快照中的对应项。
    const int index = static_cast<int>(m_detailStep);
    const bool hasTrend = m_detailStep != ProcessStep::Preparation;
    m_detailTitle->setText(QStringLiteral("流程详情：%1").arg(stepName(m_detailStep)));
    m_detailState->setText(QStringLiteral("步骤状态：%1").arg(
        stepStateText(m_snapshot.steps[static_cast<size_t>(index)])));
    m_detailDescription->setText(stepDescription(m_detailStep));
    m_detailCondition->setText(stepCondition(m_detailStep));
    m_detailTrendTitle->setText(trendTitle(m_detailStep));
    m_detailChart->setMode(trendMode(m_detailStep));
    m_detailChart->setAccessibleName(trendAccessibleName(m_detailStep));
    m_detailChart->setVisible(hasTrend);
    m_detailTrendPlaceholder->setVisible(!hasTrend);
}

void OperatorWidget::showProcessDetail(ProcessStep step)
{
    // 左侧流程点击后的唯一页面切换入口，并把焦点交给详情中的主要内容以方便键盘操作。
    m_detailStep = step;
    m_pages->setCurrentIndex(1);
    refreshDetail();
    if (m_detailStep == ProcessStep::Preparation)
        m_detailTrendPlaceholder->setFocus();
    else
        m_detailChart->setFocus();
}

void OperatorWidget::showOverview()
{
    m_pages->setCurrentIndex(0);
    m_processFlow->setFocus();
}

void OperatorWidget::showExportDialog()
{
    if (!m_hasExportableRun)
        return;
    // 系统准备阶段没有过程曲线，因此仅允许导出文本数据。
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("导出运行数据"));
    dialog.setMinimumWidth(430);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);
    layout->addWidget(makeLabel(QStringLiteral("请选择导出格式"), "DialogTitle", &dialog));
    const bool hasTrend = m_detailStep != ProcessStep::Preparation;
    layout->addWidget(makeLabel(hasTrend
                                    ? QStringLiteral("趋势图片导出当前步骤的%1；文本数据导出本次运行的遥测、设备动作和事件记录。")
                                          .arg(trendTitle(m_detailStep))
                                    : QStringLiteral("系统准备阶段没有过程曲线，请导出文本数据查看检查和设备记录。"),
                                "DialogDescription", &dialog));
    auto *image = new QPushButton(QStringLiteral("导出趋势图片"), &dialog);
    image->setObjectName(QStringLiteral("ExportButton"));
    image->setMinimumHeight(50);
    image->setEnabled(hasTrend);
    image->setToolTip(hasTrend ? QString() : QStringLiteral("系统准备阶段没有变化趋势图片"));
    auto *text = new QPushButton(QStringLiteral("导出文本数据"), &dialog);
    text->setObjectName(QStringLiteral("PrimaryButton"));
    text->setMinimumHeight(50);
    layout->addWidget(image);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(image, &QPushButton::clicked, &dialog, [this, &dialog] {
        exportTrendImage();
        dialog.accept();
    });
    connect(text, &QPushButton::clicked, &dialog, [this, &dialog] {
        emit dataExportRequested();
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void OperatorWidget::exportTrendImage()
{
    // 通过 render() 将当前详情图控件直接绘制为 PNG，不重新计算趋势数据。
    if (m_detailStep == ProcessStep::Preparation) {
        QMessageBox::information(this, QStringLiteral("导出趋势图片"),
                                 QStringLiteral("系统准备阶段没有温度或真空变化曲线，请选择导出文本数据。"));
        return;
    }
    const QString suggested = QStringLiteral("trend_%1.png").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出趋势图片"),
                                                      suggested, QStringLiteral("PNG 图片 (*.png)"));
    if (!path.isEmpty()) {
        QPixmap image(m_detailChart->size());
        image.fill(Qt::white);
        m_detailChart->render(&image);
        image.save(path, "PNG");
    }
}

void OperatorWidget::confirmVent()
{
    // 这是界面二次确认；真正是否允许放空仍由状态机的 canVent 判断。
    QMessageBox box(QMessageBox::Warning, QStringLiteral("确认室温放空"),
                    QStringLiteral("放空将使腔体恢复到大气压力。请确认样品已回到室温、泵组已停止且高真空主阀已关闭。"),
                    QMessageBox::NoButton, this);
    auto *confirm = box.addButton(QStringLiteral("确认放空"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton *>(box.buttons().last()));
    box.exec();
    if (box.clickedButton() == confirm)
        emit ventRequested();
}
