#include "MainWindow.h"

#include "DataLogger.h"
#include "EngineerWidget.h"
#include "MockSystem.h"
#include "OperatorWidget.h"
#include "SystemController.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

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

QString lastEventText(const QString &directory)
{
    QFile events(QDir(directory).filePath(QStringLiteral("events.csv")));
    if (!events.open(QIODevice::ReadOnly))
        return QStringLiteral("暂无数据");
    const QList<QByteArray> rows = events.readAll().split('\n');
    for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
        const QByteArray row = it->trimmed();
        if (!row.isEmpty() && !row.startsWith("time,"))
            return QString::fromUtf8(row);
    }
    return QStringLiteral("暂无数据");
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
    qRegisterMetaType<RunSummary>();
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
    m_operator->onExportAvailabilityChanged(m_logger->hasExportableRun());
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
    m_alarmLabel = new QPushButton(QStringLiteral("●  无当前报警"), header);
    m_alarmLabel->setObjectName(QStringLiteral("HeaderAlarm"));
    m_alarmLabel->setToolTip(QStringLiteral("单击查看报警中心"));
    setStateProperty(m_alarmLabel, QStringLiteral("success"));
    connect(m_alarmLabel, &QPushButton::clicked, this, &MainWindow::showAlarmCenter);
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
    // AlarmStatus 只给当前状态；这里在状态变化时补一条“发生/解除”历史。
    const bool changed = alarm.active != m_currentAlarm.active
        || alarm.text != m_currentAlarm.text || alarm.deviceIndex != m_currentAlarm.deviceIndex;
    if (changed && (alarm.active || m_currentAlarm.active)) {
        const AlarmStatus &source = alarm.active ? alarm : m_currentAlarm;
        m_alarmHistory.prepend({ source.occurredAt.isValid() ? source.occurredAt
                                                              : QDateTime::currentDateTime(),
                                 alarmDeviceText(source),
                                 source.text.isEmpty() ? QStringLiteral("报警内容未知") : source.text,
                                 alarm.active ? QStringLiteral("发生") : QStringLiteral("解除"),
                                 m_currentAlarmAcknowledged });
    }
    if (changed && alarm.active)
        m_currentAlarmAcknowledged = false;
    m_currentAlarm = alarm;
    m_alarmLabel->setText(alarm.active
        ? QStringLiteral("●  %1 条当前报警%2").arg(alarm.count).arg(
            m_currentAlarmAcknowledged ? QStringLiteral("（已确认）") : QString())
        : QStringLiteral("●  无当前报警"));
    setStateProperty(m_alarmLabel, alarm.active ? QStringLiteral("danger") : QStringLiteral("success"));
    m_alarmLabel->setToolTip(alarm.text.isEmpty() ? QStringLiteral("单击查看报警中心") : alarm.text);
}

QString MainWindow::alarmDeviceText(const AlarmStatus &alarm) const
{
    return alarm.deviceIndex >= 0 ? QStringLiteral("设备 %1").arg(alarm.deviceIndex + 1)
                                  : QStringLiteral("设备未知");
}

void MainWindow::acknowledgeCurrentAlarm()
{
    if (!m_currentAlarm.active || m_currentAlarmAcknowledged)
        return;
    m_currentAlarmAcknowledged = true;
    m_alarmHistory.prepend({ QDateTime::currentDateTime(), alarmDeviceText(m_currentAlarm),
                             m_currentAlarm.text.isEmpty() ? QStringLiteral("报警内容未知")
                                                           : m_currentAlarm.text,
                             QStringLiteral("确认"), true });
    m_logger->appendLog(QStringLiteral("操作员已确认报警：%1").arg(m_currentAlarm.text));
    onAlarmChanged(m_currentAlarm);
}

void MainWindow::showAlarmCenter()
{
    // 弹窗仅观察和确认报警，任何流程控制仍只能从底部控制区进入。
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("报警中心"));
    dialog.resize(720, 460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(QStringLiteral("报警中心"), &dialog);
    title->setObjectName(QStringLiteral("DialogTitle"));
    layout->addWidget(title);
    auto *current = new QLabel(&dialog);
    current->setObjectName(QStringLiteral("DialogDescription"));
    current->setWordWrap(true);
    layout->addWidget(current);
    auto *history = new QListWidget(&dialog);
    history->setObjectName(QStringLiteral("EventList"));
    layout->addWidget(history, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *acknowledge = buttons->addButton(QStringLiteral("确认当前报警"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    const auto refresh = [&] {
        const QString time = m_currentAlarm.occurredAt.isValid()
            ? m_currentAlarm.occurredAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("时间未知");
        current->setText(m_currentAlarm.active
            ? QStringLiteral("当前报警\n时间：%1\n设备：%2\n内容：%3\n确认：%4")
                  .arg(time, alarmDeviceText(m_currentAlarm),
                       m_currentAlarm.text.isEmpty() ? QStringLiteral("报警内容未知") : m_currentAlarm.text,
                       m_currentAlarmAcknowledged ? QStringLiteral("已确认") : QStringLiteral("未确认"))
            : QStringLiteral("当前没有活动报警。下方保留本次启动以来的报警历史。"));
        history->clear();
        for (const AlarmHistoryEntry &entry : m_alarmHistory) {
            history->addItem(QStringLiteral("%1｜%2｜%3｜%4%5")
                .arg(entry.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), entry.action,
                     entry.device, entry.text, entry.acknowledged ? QStringLiteral("｜已确认") : QString()));
        }
        acknowledge->setEnabled(m_currentAlarm.active && !m_currentAlarmAcknowledged);
    };
    refresh();
    connect(acknowledge, &QPushButton::clicked, &dialog, [&] { acknowledgeCurrentAlarm(); refresh(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

QString MainWindow::runSummaryText(const RunSummary &summary) const
{
    const auto dateText = [](const QDateTime &time) {
        return time.isValid() ? time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("暂无数据");
    };
    const auto numberText = [](double value, int decimals, const QString &unit) {
        return std::isfinite(value) ? QString::number(value, 'f', decimals) + unit
                                    : QStringLiteral("暂无数据");
    };
    return QStringLiteral("开始：%1\n结束：%2\n结果：%3\n最后阶段：%4\n最低温：%5\n结束真空：%6\n报警数：%7\n目录：%8")
        .arg(dateText(summary.startedAt), dateText(summary.endedAt),
             summary.result.isEmpty() ? QStringLiteral("暂无数据") : summary.result,
             summary.lastPhase.isEmpty() ? QStringLiteral("暂无数据") : summary.lastPhase,
             numberText(summary.lowestTemperatureK, 1, QStringLiteral(" K")),
             std::isfinite(summary.finalVacuumPa) ? formatPressure(summary.finalVacuumPa) + QStringLiteral(" Pa")
                                                  : QStringLiteral("暂无数据"))
        .arg(summary.alarmCount)
        .arg(summary.directory)
        + QStringLiteral("\n最后事件：%1").arg(lastEventText(summary.directory));
}

void MainWindow::showRunHistory()
{
    // 选择历史项会同步导出目标目录，不会重新打开或修改任何旧文件。
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("运行记录"));
    dialog.resize(760, 480);
    auto *layout = new QHBoxLayout(&dialog);
    auto *runs = new QListWidget(&dialog);
    runs->setObjectName(QStringLiteral("EventList"));
    auto *summary = new QLabel(QStringLiteral("暂无有效运行记录"), &dialog);
    summary->setObjectName(QStringLiteral("DialogDescription"));
    summary->setWordWrap(true);
    layout->addWidget(runs, 1);
    layout->addWidget(summary, 2);
    const QVector<RunSummary> runSummaries = m_logger->runSummaries();
    for (const RunSummary &item : runSummaries) {
        auto *row = new QListWidgetItem(QStringLiteral("%1｜%2")
            .arg(item.startedAt.isValid() ? item.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                          : QStringLiteral("时间未知"),
                 item.result.isEmpty() ? QStringLiteral("历史记录") : item.result), runs);
        row->setData(Qt::UserRole, item.directory);
    }
    connect(runs, &QListWidget::currentRowChanged, &dialog, [&, runSummaries](int row) {
        if (row < 0 || row >= runSummaries.size())
            return;
        m_logger->selectRunDirectory(runSummaries.at(row).directory);
        summary->setText(runSummaryText(runSummaries.at(row)));
    });
    if (!runSummaries.isEmpty())
        runs->setCurrentRow(0);
    dialog.exec();
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
    connect(m_operator, &OperatorWidget::safetyStopRequested,
            m_system, &SystemController::requestSafetyStop);
    connect(m_operator, &OperatorWidget::resetRequested,
            m_system, &SystemController::requestReset);
    connect(m_operator, &OperatorWidget::ventRequested,
            m_system, &SystemController::requestVent);
    connect(m_operator, &OperatorWidget::dataExportRequested,
            m_logger, &DataLogger::exportInteractive);
    connect(m_operator, &OperatorWidget::runHistoryRequested,
            this, &MainWindow::showRunHistory);

    // 第二段：控制器聚合数据 → 操作员界面刷新。
    connect(m_system, &SystemController::processSnapshotChanged,
            m_operator, &OperatorWidget::onProcessSnapshot);
    connect(m_system, &SystemController::processSnapshotChanged,
            m_logger, &DataLogger::onProcessSnapshot);
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
    connect(m_system, &SystemController::heartbeat,
            m_operator, &OperatorWidget::onCommunicationChanged);
    connect(m_system, &SystemController::communicationChanged,
            m_operator, &OperatorWidget::onCommunicationChanged);
    connect(m_system, &SystemController::alarmChanged,
            this, &MainWindow::onAlarmChanged);
    connect(m_system, &SystemController::alarmChanged,
            m_logger, &DataLogger::onAlarmChanged);

    // 第三段：每条样本和日志同时进入数据记录与可见日志区。
    connect(m_system, &SystemController::sampleReady,
            m_logger, &DataLogger::onSampleReady);
    connect(m_system, &SystemController::logMessage,
            m_logger, &DataLogger::appendLog);
    connect(m_system, &SystemController::logMessage,
            m_operator, &OperatorWidget::onLogMessage);
    connect(m_logger, &DataLogger::exportAvailabilityChanged,
            m_operator, &OperatorWidget::onExportAvailabilityChanged);
    connect(m_logger, &DataLogger::runFinished,
            m_operator, &OperatorWidget::onRunFinished);

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
