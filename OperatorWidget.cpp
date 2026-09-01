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
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QLabel *makeLabel(const QString &text, const char *objectName, QWidget *parent = nullptr)
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(QLatin1String(objectName));
    return result;
}

QString stepName(ProcessStep step)
{
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

QString stepCondition(const ProcessSnapshot &snapshot)
{
    if (snapshot.state == SystemState::Fault || snapshot.state == SystemState::ResetRequired)
        return QStringLiteral("当前条件：存在故障或复位条件，自动流程已停止。请检查设备事件记录。");
    if (snapshot.state == SystemState::Running)
        return QStringLiteral("当前条件：温度和真空稳定，实验运行中。");
    if (snapshot.state == SystemState::ReadyToVent)
        return QStringLiteral("当前条件：已回到室温安全区，可确认室温放空。");
    return QStringLiteral("当前条件：%1").arg(snapshot.detailText);
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
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("OperatorOverview"));
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(16, 14, 16, 16);
    root->setSpacing(14);

    m_processFlow = new ProcessFlowWidget(page);
    m_processFlow->setFixedWidth(370);
    connect(m_processFlow, &ProcessFlowWidget::stepActivated,
            this, &OperatorWidget::showProcessDetail);
    root->addWidget(m_processFlow);

    auto *right = new QVBoxLayout;
    right->setSpacing(12);
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
    activityLayout->addWidget(makeLabel(QStringLiteral("最近运行记录"), "PanelTitle", activityPanel));
    m_overviewEvents = new QListWidget(activityPanel);
    m_overviewEvents->setObjectName(QStringLiteral("EventList"));
    m_overviewEvents->addItem(QStringLiteral("等待自动流程启动"));
    activityLayout->addWidget(m_overviewEvents);
    right->addWidget(activityPanel, 1);
    right->addWidget(buildControlPanel());
    root->addLayout(right, 1);
    return page;
}

QWidget *OperatorWidget::buildSystemStatusPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("IndustrialPanel"));
    panel->setMinimumHeight(158);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(20);

    auto *stateBlock = new QVBoxLayout;
    stateBlock->setSpacing(5);
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

    const auto addMetric = [panel, layout](const QString &title, QLabel *&value,
                                            QLabel *&lineOne, QLabel *&lineTwo) {
        auto *block = new QVBoxLayout;
        block->setSpacing(5);
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
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("IndustrialPanel"));
    panel->setMinimumHeight(144);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto *commandRow = new QHBoxLayout;
    commandRow->setSpacing(10);
    commandRow->addWidget(makeLabel(QStringLiteral("降温速率"), "ControlLabel", panel));
    m_coolingRate = new QDoubleSpinBox(panel);
    m_coolingRate->setRange(0.1, 5.0);
    m_coolingRate->setDecimals(1);
    m_coolingRate->setSingleStep(0.1);
    m_coolingRate->setValue(1.0);
    m_coolingRate->setSuffix(QStringLiteral(" K/min"));
    m_coolingRate->setMinimumWidth(132);
    m_coolingRate->setAccessibleName(QStringLiteral("降温速率，单位开尔文每分钟"));
    commandRow->addWidget(m_coolingRate);
    commandRow->addStretch();

    const auto addButton = [panel, commandRow](QPushButton *&button, const QString &text,
                                        const QString &objectName, int width) {
        button = new QPushButton(text, panel);
        button->setObjectName(objectName);
        button->setMinimumSize(width, 42);
        commandRow->addWidget(button);
    };
    addButton(m_startButton, QStringLiteral("一键启动系统"), QStringLiteral("PrimaryButton"), 150);
    addButton(m_stopButton, QStringLiteral("停止并受控回温"), QStringLiteral("WarningButton"), 166);
    addButton(m_ventButton, QStringLiteral("确认室温放空"), QStringLiteral("VentButton"), 150);
    layout->addLayout(commandRow);

    auto *footerRow = new QHBoxLayout;
    footerRow->setSpacing(14);
    m_commandReason = makeLabel(QString(), "CommandReason", panel);
    m_commandReason->setWordWrap(true);
    m_commandReason->setMinimumHeight(42);
    footerRow->addWidget(m_commandReason, 1);
    auto *exportButton = new QPushButton(QStringLiteral("导出运行数据"), panel);
    exportButton->setObjectName(QStringLiteral("ExportButton"));
    exportButton->setMinimumSize(220, 46);
    footerRow->addWidget(exportButton);
    layout->addLayout(footerRow);
    connect(m_startButton, &QPushButton::clicked, this, [this] {
        emit startRequested(m_coolingRate->value());
    });
    connect(m_stopButton, &QPushButton::clicked, this, &OperatorWidget::stopRequested);
    connect(m_ventButton, &QPushButton::clicked, this, &OperatorWidget::confirmVent);
    connect(exportButton, &QPushButton::clicked, this, &OperatorWidget::showExportDialog);
    QWidget::setTabOrder(m_coolingRate, m_startButton);
    QWidget::setTabOrder(m_startButton, m_stopButton);
    QWidget::setTabOrder(m_stopButton, m_ventButton);
    QWidget::setTabOrder(m_ventButton, exportButton);
    return panel;
}

QWidget *OperatorWidget::buildDetailPage()
{
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
    descriptionLayout->addWidget(makeLabel(QStringLiteral("进入下一步条件"), "PanelSubTitle", description));
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
    m_snapshot = snapshot;
    m_processFlow->setSnapshot(snapshot);
    refreshSummary();
    refreshDetail();
    refreshCommands();
}

void OperatorWidget::onTelemetryReady(const TelemetryPoint &point)
{
    m_temperatureK = point.temperatureK;
    m_pressurePa = point.vacuumPa;
    m_store->appendPoint(point);
    refreshSummary();
    m_detailChart->update();
}

void OperatorWidget::onDeviceStatus(const DeviceStatus &device)
{
    m_deviceHealth->setDeviceStatus(device);
}

void OperatorWidget::onHighLimitChanged(double pa)
{
    m_highLimitPa = pa;
    refreshSummary();
}

void OperatorWidget::onLogMessage(const QString &message)
{
    if (m_overviewEvents->count() == 1
        && m_overviewEvents->item(0)->text() == QStringLiteral("等待自动流程启动"))
        delete m_overviewEvents->takeItem(0);
    m_overviewEvents->addItem(message);
    while (m_overviewEvents->count() > 5)
        delete m_overviewEvents->takeItem(0);
    m_overviewEvents->scrollToBottom();
    if (m_eventList->count() == 1
        && m_eventList->item(0)->text() == QStringLiteral("等待设备动作记录"))
        delete m_eventList->takeItem(0);
    m_eventList->addItem(message);
    while (m_eventList->count() > 200)
        delete m_eventList->takeItem(0);
    m_eventList->scrollToBottom();
}

void OperatorWidget::refreshSummary()
{
    m_stateValue->setText(m_snapshot.stateText);
    m_stateDetail->setText(m_snapshot.detailText);
    m_temperatureValue->setText(QStringLiteral("%1 K").arg(m_temperatureK, 0, 'f', 1));
    m_temperatureTarget->setText(QStringLiteral("目标温度：2.8 K"));
    m_temperatureRate->setText(QStringLiteral("设定速率：%1 K/min").arg(
        m_coolingRate ? m_coolingRate->value() : 1.0, 0, 'f', 1));
    m_pressureValue->setText(TrendChartWidget::formatPressureTick(m_pressurePa)
                             + QStringLiteral(" Pa"));
    m_pressureLimit->setText(TrendChartWidget::formatPressureTick(m_highLimitPa)
                             + QStringLiteral(" Pa 高真空门限"));
}

void OperatorWidget::refreshCommands()
{
    const CommandAvailability &commands = m_snapshot.commands;
    m_startButton->setEnabled(commands.canStart);
    m_stopButton->setEnabled(commands.canStop);
    m_ventButton->setEnabled(commands.canVent);
    m_coolingRate->setReadOnly(m_snapshot.state != SystemState::Idle);
    m_startButton->setToolTip(commands.canStart ? QString() : commands.startReason);
    m_stopButton->setToolTip(commands.canStop ? QString() : commands.stopReason);
    m_ventButton->setToolTip(commands.canVent ? QString() : commands.ventReason);
    const bool waitingVent = m_snapshot.state == SystemState::Warming
        || m_snapshot.state == SystemState::ReadyToVent;
    m_commandReason->setText(waitingVent && !commands.canVent ? commands.ventReason
                                                               : m_snapshot.detailText);
}

void OperatorWidget::refreshDetail()
{
    const int index = static_cast<int>(m_detailStep);
    const bool hasTrend = m_detailStep != ProcessStep::Preparation;
    m_detailTitle->setText(QStringLiteral("流程详情：%1").arg(stepName(m_detailStep)));
    m_detailState->setText(QStringLiteral("步骤状态：%1").arg(
        stepStateText(m_snapshot.steps[static_cast<size_t>(index)])));
    m_detailDescription->setText(stepDescription(m_detailStep));
    m_detailCondition->setText(stepCondition(m_snapshot));
    m_detailTrendTitle->setText(trendTitle(m_detailStep));
    m_detailChart->setMode(trendMode(m_detailStep));
    m_detailChart->setAccessibleName(trendAccessibleName(m_detailStep));
    m_detailChart->setVisible(hasTrend);
    m_detailTrendPlaceholder->setVisible(!hasTrend);
}

void OperatorWidget::showProcessDetail(ProcessStep step)
{
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
