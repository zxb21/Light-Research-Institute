#include "MainWindow.h"

#include "DataLogger.h"
#include "EngineerWidget.h"
#include "MockSystem.h"
#include "OperatorWidget.h"
#include "SystemController.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

void setStateProperty(QWidget *widget, const QString &state)
{
    // QSS 按 state 属性选择颜色。手动刷新样式是为了让运行中属性变化立即生效。
    if (!widget || widget->property("state").toString() == state)
        return;
    widget->setProperty("state", state);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_operator(new OperatorWidget(this))
    , m_engineer(new EngineerWidget(this))
    , m_clockTimer(new QTimer(this))
    , m_system(new MockSystem(this))
    , m_logger(new DataLogger(this))
{
    // 自定义结构经信号跨对象传递前需要注册，避免 Qt 只能识别基础类型。
    qRegisterMetaType<SystemState>();
    qRegisterMetaType<ProcessStep>();
    qRegisterMetaType<ProcessSnapshot>();
    qRegisterMetaType<DeviceStatus>();
    qRegisterMetaType<AlarmStatus>();
    qRegisterMetaType<TelemetryPoint>();

    // 全局外观来自资源文件；布局代码只负责结构，颜色和字体优先在 QSS 中修改。
    QFile styleFile(QStringLiteral(":/cryo/resources/operator.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    setWindowTitle(QStringLiteral("超低振动深低温恒温器系统"));
    resize(1280, 800);
    setMinimumSize(1280, 800);

    // 窗口固定由“顶部栏 + 可切换的主内容”组成。
    auto *shell = new QWidget(this);
    shell->setObjectName(QStringLiteral("AppShell"));
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    buildHeaderBar(shellLayout);
    m_stack = new QStackedWidget(shell);
    // 页面索引约定必须与 setEngineerMode() 一致。
    m_stack->addWidget(m_operator);
    m_stack->addWidget(m_engineer);
    shellLayout->addWidget(m_stack, 1);
    setCentralWidget(shell);

    // 顶部时钟与控制器无关，独立每秒刷新一次。
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start(1000);
    updateClock();
    wireSystem();
    setEngineerMode(false);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 退出前主动刷盘，减少最后一批遥测和日志停留在内存中的风险。
    m_logger->flush();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildHeaderBar(QVBoxLayout *shellLayout)
{
    // 顶部栏仅显示全局信息，不承载流程页面内部的控件。
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("HeaderBar"));
    header->setFixedHeight(56);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(20, 0, 16, 0);
    layout->setSpacing(18);

    auto *title = new QLabel(QStringLiteral("超低振动深低温恒温器系统"), header);
    title->setObjectName(QStringLiteral("AppTitle"));
    layout->addWidget(title);
    layout->addStretch();

    m_modeLabel = new QLabel(QStringLiteral("●  操作员模式"), header);
    m_modeLabel->setObjectName(QStringLiteral("HeaderStatus"));
    layout->addWidget(m_modeLabel);
    m_communicationLabel = new QLabel(QStringLiteral("●  通信正常"), header);
    m_communicationLabel->setObjectName(QStringLiteral("HeaderStatus"));
    setStateProperty(m_communicationLabel, QStringLiteral("success"));
    layout->addWidget(m_communicationLabel);
    auto *recording = new QLabel(QStringLiteral("●  自动记录中"), header);
    recording->setObjectName(QStringLiteral("HeaderStatus"));
    setStateProperty(recording, QStringLiteral("primary"));
    layout->addWidget(recording);
    m_alarmLabel = new QLabel(QStringLiteral("●  无未确认报警"), header);
    m_alarmLabel->setObjectName(QStringLiteral("HeaderStatus"));
    setStateProperty(m_alarmLabel, QStringLiteral("success"));
    layout->addWidget(m_alarmLabel);

    m_clockLabel = new QLabel(header);
    m_clockLabel->setObjectName(QStringLiteral("ClockLabel"));
    m_clockLabel->setMinimumWidth(155);
    layout->addWidget(m_clockLabel);
    m_modeButton = new QPushButton(QStringLiteral("工程师模式"), header);
    m_modeButton->setObjectName(QStringLiteral("HeaderButton"));
    connect(m_modeButton, &QPushButton::clicked,
            this, &MainWindow::onToggleModeClicked);
    layout->addWidget(m_modeButton);
    shellLayout->addWidget(header);
}

void MainWindow::updateClock()
{
    m_clockLabel->setText(QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
}

void MainWindow::onHeartbeat(bool connected)
{
    // 心跳与通信状态目前都复用同一显示位置；真实控制器可分别发布更精确状态。
    m_communicationLabel->setText(connected ? QStringLiteral("●  通信正常")
                                             : QStringLiteral("●  通信中断"));
    setStateProperty(m_communicationLabel,
                     connected ? QStringLiteral("success") : QStringLiteral("danger"));
}

void MainWindow::onAlarmChanged(const AlarmStatus &alarm)
{
    m_alarmLabel->setText(alarm.active
                              ? QStringLiteral("●  %1 条未确认报警").arg(alarm.count)
                              : QStringLiteral("●  无未确认报警"));
    setStateProperty(m_alarmLabel,
                     alarm.active ? QStringLiteral("danger") : QStringLiteral("success"));
    m_alarmLabel->setToolTip(alarm.text);
}

void MainWindow::onToggleModeClicked()
{
    // 从工程师模式返回操作员模式不需要再次输入密码。
    if (m_engineerMode) {
        setEngineerMode(false);
        return;
    }

    bool accepted = false;
    const QString password = QInputDialog::getText(
        this, QStringLiteral("切换到工程师模式"), QStringLiteral("请输入工程师密码："),
        QLineEdit::Password, QString(), &accepted);
    if (!accepted)
        return;

    // 工程师模式密码是已确认的现有业务规则；没有明确需求时不要在此修改。
    if (password == QStringLiteral("888888")) {
        setEngineerMode(true);
    } else {
        QMessageBox::warning(this, QStringLiteral("密码错误"),
                             QStringLiteral("密码错误，无法进入工程师模式。\n默认密码：888888"));
    }
}

void MainWindow::setEngineerMode(bool enabled)
{
    // 只切换可见页面和顶部文字，不创建或销毁页面，因此两页状态会保留。
    m_engineerMode = enabled;
    m_stack->setCurrentIndex(enabled ? 1 : 0);
    m_modeLabel->setText(enabled ? QStringLiteral("●  工程师模式")
                                 : QStringLiteral("●  操作员模式"));
    m_modeButton->setText(enabled ? QStringLiteral("返回操作员模式")
                                  : QStringLiteral("工程师模式"));
}

void MainWindow::wireSystem()
{
    // 第一段：操作员的高层请求 → 控制器。界面不直接操作 MockSystem 的内部变量。
    connect(m_operator, &OperatorWidget::startRequested,
            m_system, &SystemController::requestStart);
    connect(m_operator, &OperatorWidget::stopRequested,
            m_system, &SystemController::requestStop);
    connect(m_operator, &OperatorWidget::ventRequested,
            m_system, &SystemController::requestVent);
    connect(m_operator, &OperatorWidget::dataExportRequested,
            m_logger, &DataLogger::exportInteractive);

    // 第二段：控制器聚合数据 → 操作员界面刷新。
    connect(m_system, &SystemController::processSnapshotChanged,
            m_operator, &OperatorWidget::onProcessSnapshot);
    connect(m_system, &SystemController::telemetryReady,
            m_operator, &OperatorWidget::onTelemetryReady);
    connect(m_system, &SystemController::deviceStatusChanged,
            m_operator, &OperatorWidget::onDeviceStatus);
    connect(m_system, &SystemController::highLimitChanged,
            m_operator, &OperatorWidget::onHighLimitChanged);
    connect(m_system, &SystemController::heartbeat,
            this, &MainWindow::onHeartbeat);
    connect(m_system, &SystemController::communicationChanged,
            this, &MainWindow::onHeartbeat);
    connect(m_system, &SystemController::alarmChanged,
            this, &MainWindow::onAlarmChanged);

    // 第三段：每条样本和日志同时进入数据记录与可见日志区。
    connect(m_system, &SystemController::sampleReady,
            m_logger, &DataLogger::onSampleReady);
    connect(m_system, &SystemController::logMessage,
            m_logger, &DataLogger::appendLog);
    connect(m_system, &SystemController::logMessage,
            m_operator, &OperatorWidget::onLogMessage);

    // 第四段：工程师页需要更细粒度的原始状态和手动控制回显。
    connect(m_system, &SystemController::vacuumChanged,
            m_engineer, &EngineerWidget::onVacuumChanged);
    connect(m_system, &SystemController::temperatureChanged,
            m_engineer, &EngineerWidget::onTemperatureChanged);
    connect(m_system, &SystemController::phaseChanged,
            m_engineer, &EngineerWidget::onPhaseChanged);
    connect(m_system, &SystemController::gateValveChanged,
            m_engineer, &EngineerWidget::onGateValveChanged);
    connect(m_system, &SystemController::foreValveChanged,
            m_engineer, &EngineerWidget::onForeValveChanged);
    connect(m_system, &SystemController::ventValveChanged,
            m_engineer, &EngineerWidget::onVentValveChanged);
    connect(m_system, &SystemController::dryPumpChanged,
            m_engineer, &EngineerWidget::onDryPumpChanged);
    connect(m_system, &SystemController::tmpChanged,
            m_engineer, &EngineerWidget::onTmpChanged);
    connect(m_system, &SystemController::tmpSpeedChanged,
            m_engineer, &EngineerWidget::onTmpSpeedChanged);
    connect(m_system, &SystemController::cryocoolerChanged,
            m_engineer, &EngineerWidget::onCryocoolerChanged);
    connect(m_system, &SystemController::heaterChanged,
            m_engineer, &EngineerWidget::onHeaterChanged);
    connect(m_system, &SystemController::diChanged,
            m_engineer, &EngineerWidget::onDiChanged);
    connect(m_system, &SystemController::sensorRawChanged,
            m_engineer, &EngineerWidget::onSensorRawChanged);
    connect(m_system, &SystemController::logMessage,
            m_engineer, &EngineerWidget::onLogMessage);

    // 第五段：工程师页的手动请求仍要经控制器处理，避免绕过未来硬件联锁。
    connect(m_engineer, &EngineerWidget::gateValveSet,
            m_system, &SystemController::setGateValve);
    connect(m_engineer, &EngineerWidget::foreValveSet,
            m_system, &SystemController::setForeValve);
    connect(m_engineer, &EngineerWidget::ventValveSet,
            m_system, &SystemController::setVentValve);
    connect(m_engineer, &EngineerWidget::dryPumpSet,
            m_system, &SystemController::setDryPump);
    connect(m_engineer, &EngineerWidget::tmpSet,
            m_system, &SystemController::setTmp);
    connect(m_engineer, &EngineerWidget::tmpSpeedSet,
            m_system, &SystemController::setTmpSpeed);
    connect(m_engineer, &EngineerWidget::cryocoolerSet,
            m_system, &SystemController::setCryocooler);
    connect(m_engineer, &EngineerWidget::heaterSet,
            m_system, &SystemController::setHeater);
    connect(m_engineer, &EngineerWidget::pidParamsSet,
            m_system, &SystemController::setPidParams);
    connect(m_engineer, &EngineerWidget::configApplied,
            m_system, &SystemController::applyConfig);
    connect(m_engineer, &EngineerWidget::configResetRequested,
            m_system, &SystemController::resetConfig);
}
