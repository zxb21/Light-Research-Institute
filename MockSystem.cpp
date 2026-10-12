#include "MockSystem.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kTickSeconds = 0.25;
// 模拟器将物理时间压缩，真实硬件控制器不得复用此倍率。
constexpr double kSimulationTimeScale = 80.0;

double temperatureDeltaForTick(double rateKPerMin)
{
    // 将 K/min 换算为本次 250 毫秒定时周期的温度变化，并乘演示用时间倍率。
    return rateKPerMin * kTickSeconds / 60.0 * kSimulationTimeScale;
}

} // namespace

MockSystem::MockSystem(QObject *parent)
    : SystemController(parent)
    , m_timer(new QTimer(this))
{
    // 初始假定数字量和七设备健康；真实控制器应从实际设备读回这些条件。
    m_di = { true, true, true, true, true, true };
    m_deviceHealthy.fill(true);
    for (QString &message : m_deviceMessage)
        message = QStringLiteral("通信正常");

    // 每 250 毫秒推进一次模拟流程并发布数据。
    connect(m_timer, &QTimer::timeout, this, &MockSystem::tick);
    m_timer->start(250);
    // 事件循环启动后立即发布一次初始画面，避免界面等待第一个定时周期才有数据。
    QTimer::singleShot(0, this, [this] {
        emit heartbeat(true);
        emit communicationChanged(true);
        emit highLimitChanged(m_highLimit);
        publishSnapshot();
        publishDevices();
        updateSensors();
    });
}

void MockSystem::requestStart(double coolingRateKPerMin)
{
    // 先刷新安全条件再请求状态机启动，保证按钮显示和实际准入规则一致。
    updateSafetyConditions();
    if (!m_process.requestStart()) {
        pushLog(QStringLiteral("启动请求被拒绝：%1")
                    .arg(m_process.snapshot().commands.startReason));
        return;
    }
    // 即使外部传入异常值，模拟控制器也会限制在允许范围内。
    m_coolingRate = std::clamp(coolingRateKPerMin, 0.1, 5.0);
    m_stateTicks = 0;
    pushLog(QStringLiteral("一键启动，降温速率 %1 K/min").arg(m_coolingRate, 0, 'f', 1));
    publishSnapshot();
}

void MockSystem::requestStop()
{
    // 普通停止保留流程位置；是否必须经过回温由当前温度和来源状态共同决定。
    const SystemState sourceState = m_process.snapshot().state;
    if (!m_process.requestStop(m_temperature, m_vacuum)) {
        pushLog(QStringLiteral("停止请求被拒绝：%1")
                    .arg(m_process.snapshot().commands.stopReason));
        return;
    }
    m_stopRequiresWarming = sourceState == SystemState::PreCooling
        || sourceState == SystemState::DeepCooling
        || sourceState == SystemState::Stabilizing
        || sourceState == SystemState::Running;
    m_stateTicks = 0;
    pushLog(QStringLiteral("操作员请求普通停止，流程位置已保留"));
    publishSnapshot();
}

void MockSystem::requestSafetyStop()
{
    // 操作员页的安全停止入口；状态机仍会再次检查当前流程是否允许执行保护停机。
    if (!m_process.requestSafetyStop(QStringLiteral("操作员触发安全停止")))
        return;
    m_stateTicks = 0;
    pushLog(QStringLiteral("安全停止已触发"));
    AlarmStatus alarm;
    alarm.active = true;
    alarm.count = 1;
    alarm.text = QStringLiteral("操作员触发安全停止");
    alarm.occurredAt = QDateTime::currentDateTime();
    emit alarmChanged(alarm);
    publishSnapshot();
}

void MockSystem::requestVent()
{
    // 状态机先验证室温、泵组和阀门条件，验证成功后才打开模拟放空阀。
    updateSafetyConditions();
    if (!m_process.requestVent()) {
        pushLog(QStringLiteral("放空请求被拒绝：%1")
                    .arg(m_process.snapshot().commands.ventReason));
        publishSnapshot();
        return;
    }
    setVentValve(true);
    m_stateTicks = 0;
    pushLog(QStringLiteral("已确认室温放空"));
    publishSnapshot();
}

void MockSystem::requestReset()
{
    // 复位前再次同步安全条件，防止故障尚未解除时仅靠点击就回到待机。
    updateSafetyConditions();
    if (!m_process.requestReset()) {
        pushLog(QStringLiteral("复位请求被拒绝：%1")
                    .arg(m_process.snapshot().commands.resetReason));
        publishSnapshot();
        return;
    }
    AlarmStatus alarm;
    emit alarmChanged(alarm);
    pushLog(QStringLiteral("故障复位完成，系统回到待机"));
    publishSnapshot();
}

void MockSystem::tick()
{
    // 模拟器的核心：按当前状态改变内部数值和设备开关，再统一发布界面数据。
    ++m_ticks;
    ++m_stateTicks;
    const SystemState state = m_process.snapshot().state;

    switch (state) {
    case SystemState::Preparing:
        // 准备完成后启动干泵和前级阀，进入粗抽。
        if (m_stateTicks >= 5) {
            setDryPump(true);
            setForeValve(true);
            enterState(SystemState::Roughing);
        }
        break;
    case SystemState::Roughing:
        // 压强采用指数下降，达到粗抽门限后启动分子泵。
        m_vacuum = std::max(m_roughLimit, m_vacuum * 0.85);
        if (m_vacuum <= m_roughLimit + 1.0e-9) {
            setTmp(true);
            enterState(SystemState::HighVacuum);
        }
        break;
    case SystemState::HighVacuum:
        // 继续抽气至高真空门限，并切换到主真空通路。
        m_vacuum = std::max(m_highLimit, m_vacuum * 0.75);
        if (m_vacuum <= m_highLimit + 1.0e-12) {
            setForeValve(false);
            setGateValve(true);
            enterState(SystemState::PreCooling);
        }
        break;
    case SystemState::PreCooling:
        // 预冷阶段只确认制冷设备运行，尚未按设定速率降低样品温度。
        if (!m_cryocooler)
            setCryocooler(true);
        if (m_stateTicks >= 6)
            enterState(SystemState::DeepCooling);
        break;
    case SystemState::DeepCooling:
        // 演示版按压缩后的时间推进温度，同时维持高真空。
        m_temperature = std::max(2.8, m_temperature - temperatureDeltaForTick(m_coolingRate));
        m_vacuum = std::max(1.0e-5, m_vacuum * 0.98);
        if (m_temperature <= 2.8)
            enterState(SystemState::Stabilizing);
        break;
    case SystemState::Stabilizing:
        // 在目标温度附近引入小幅波动，模拟稳定阶段的测量变化。
        m_temperature = 2.8 + std::sin(m_ticks * 0.35) * 0.04;
        if (m_stateTicks >= 12)
            enterState(SystemState::Running);
        break;
    case SystemState::Running:
        m_temperature = 2.8 + std::sin(m_ticks * 0.25) * 0.03;
        m_vacuum = std::max(1.0e-5, m_vacuum * (1.0 + std::sin(m_ticks * 0.2) * 0.01));
        break;
    case SystemState::Stopping:
        // 停止时先关闭输出；低温运行过的任务必须转入受控回温。
        setOutputsSafe();
        if (m_stopRequiresWarming || m_temperature < 290.0) {
            if (m_process.advanceTo(SystemState::Warming)) {
                m_stateTicks = 0;
                setHeater(true);
                m_stopRequiresWarming = false;
            }
        } else {
            enterState(SystemState::ReadyToVent);
        }
        break;
    case SystemState::Warming:
        m_temperature = std::min(295.0, m_temperature + temperatureDeltaForTick(m_warmingRate));
        if (m_temperature >= 295.0) {
            setHeater(false);
            enterState(SystemState::ReadyToVent);
        }
        break;
    case SystemState::Venting:
        // 放空只模拟腔体压强回升，不代表真实阀门开度或流量模型。
        m_vacuum = std::min(101325.0, std::max(1.0, m_vacuum) * 1.45);
        if (m_vacuum >= 101325.0) {
            setVentValve(false);
            m_process.enterRecovering();
            m_stateTicks = 0;
            publishSnapshot();
        }
        break;
    case SystemState::Recovering:
        updateSafetyConditions();
        if (m_stateTicks >= 5) {
            m_process.completeRecovery();
            publishSnapshot();
        }
        break;
    case SystemState::SafetyStopping:
        // 安全停止先让输出归零，再回温、回大气，最后交给状态机进入故障状态。
        if (m_stateTicks == 1)
            setOutputsSafe();
        if (m_temperature < 295.0) {
            setHeater(true);
            m_temperature = std::min(295.0,
                                     m_temperature + temperatureDeltaForTick(m_warmingRate));
        } else {
            setHeater(false);
            if (m_vacuum < 90000.0)
                m_vacuum = std::min(101325.0, std::max(1.0, m_vacuum) * 1.45);
            else if (m_process.advanceTo(SystemState::Fault)) {
                m_vacuum = 101325.0;
                m_stateTicks = 0;
                publishSnapshot();
            }
        }
        break;
    case SystemState::Fault:
        updateSafetyConditions();
        if (m_stateTicks >= 3) {
            m_process.enterResetRequired();
            publishSnapshot();
        }
        break;
    default:
        break;
    }

    // 所有分支结束后统一发布最新遥测、设备状态和工程师诊断数据。
    updateSafetyConditions();
    publishTelemetry();
    publishDevices();
    updateSensors();
    if (m_ticks % 4 == 0) {
        emit heartbeat(true);
        emit communicationChanged(true);
    }
}

void MockSystem::enterState(SystemState state)
{
    // 只接受状态机允许的相邻转换；成功后重置本状态计数并记录事件。
    if (!m_process.advanceTo(state))
        return;
    m_stateTicks = 0;
    pushLog(m_process.snapshot().stateText);
    publishSnapshot();
}

void MockSystem::publishSnapshot()
{
    // 操作员页接收完整快照，工程师页只需其中的中文状态文字。
    const ProcessSnapshot snapshot = m_process.snapshot();
    emit processSnapshotChanged(snapshot);
    emit phaseChanged(snapshot.stateText);
}

void MockSystem::publishDevices()
{
    // 这是七设备状态带的数据来源。若某设备文字显示不对，优先从此函数查起。
    const QStringList names = {
        QStringLiteral("干泵"), QStringLiteral("分子泵"), QStringLiteral("冷水机"),
        QStringLiteral("真空计"), QStringLiteral("温控仪"), QStringLiteral("压缩机"),
        QStringLiteral("真空电磁阀")
    };
    for (int i = 0; i < DevCount; ++i) {
        DeviceStatus device;
        const QDateTime now = QDateTime::currentDateTime();
        device.index = i;
        device.name = names.at(i);
        device.updatedAt = now;
        device.startedAt = m_deviceStartedAt[static_cast<size_t>(i)];
        device.stoppedAt = m_deviceStoppedAt[static_cast<size_t>(i)];
        if (m_deviceRunning[static_cast<size_t>(i)] && device.startedAt.isValid())
            device.runningSeconds = device.startedAt.secsTo(now);
        if (!m_deviceHealthy[static_cast<size_t>(i)]) {
            device.health = DeviceHealth::Fault;
            device.message = m_deviceMessage[static_cast<size_t>(i)];
        } else {
            device.health = i == DevVacuumGauge ? DeviceHealth::Normal
                                                : m_deviceRunning[static_cast<size_t>(i)]
                                                      ? DeviceHealth::Running
                                                      : DeviceHealth::Standby;
            device.message = QStringLiteral("通信正常");
        }
        // keyValue 是设备状态卡片第二行。真空计统一调用 formatPressure()，
        // 因而需要改真空计、摘要和趋势的显示格式时只改 SystemTypes.h 的该函数。
        switch (i) {
        case DevDryPump: device.keyValue = m_dryPump ? QStringLiteral("已启动") : QStringLiteral("已停止"); break;
        case DevMolecularPump: device.keyValue = m_tmp ? QStringLiteral("%1 rpm").arg(m_tmpSpeed) : QStringLiteral("已停止"); break;
        case DevChiller: device.keyValue = m_cryocooler ? QStringLiteral("18.0 ℃") : QStringLiteral("待机"); break;
        case DevVacuumGauge: device.keyValue = formatPressure(m_vacuum) + QStringLiteral(" Pa"); break;
        case DevTemperatureController:
            device.keyValue = m_process.snapshot().state == SystemState::Idle
                ? QStringLiteral("待机") : QStringLiteral("目标 2.8 K");
            break;
        case DevCompressor: device.keyValue = m_cryocooler ? QStringLiteral("运行") : QStringLiteral("停止"); break;
        case DevSolenoidValve: device.keyValue = (m_gate || m_fore || m_vent) ? QStringLiteral("已打开") : QStringLiteral("已关闭"); break;
        }
        emit deviceStatusChanged(device);
    }
}

void MockSystem::publishTelemetry()
{
    // 一条采样同时服务操作员趋势图、自动数据记录和工程师实时读数。
    TelemetryPoint point;
    point.epochSec = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    point.vacuumPa = m_vacuum;
    point.temperatureK = m_temperature;
    point.phase = phaseText();
    point.state = m_process.snapshot().state;
    point.step = m_process.snapshot().currentStep;
    emit telemetryReady(point);
    emit sampleReady(point.epochSec, point.vacuumPa, point.temperatureK, point.phase);
    emit vacuumChanged(m_vacuum);
    emit temperatureChanged(m_temperature);
}

void MockSystem::updateSensors()
{
    // 工程师页的原始传感器文字是演示占位，不应被当作真实仪器通信报文。
    emit sensorRawChanged(SnsVgCold, QStringLiteral("%1 V").arg(sensorVoltage(m_vacuum), 0, 'f', 3));
    emit sensorRawChanged(SnsVgPirani, QStringLiteral("%1 Pa").arg(m_vacuum, 0, 'e', 2));
    emit sensorRawChanged(SnsTempA, QStringLiteral("%1 K").arg(m_temperature, 0, 'f', 2));
    emit sensorRawChanged(SnsTempB, QStringLiteral("%1 K").arg(m_temperature + 0.12, 0, 'f', 2));
    emit sensorRawChanged(SnsHeaterCurrent, m_heater ? QStringLiteral("1.20 A") : QStringLiteral("0.00 A"));
    emit sensorRawChanged(SnsCompressorPower, m_cryocooler ? QStringLiteral("4.8 kW") : QStringLiteral("0.0 kW"));
    for (int i = 0; i < DiCount; ++i)
        emit diChanged(i, m_di[static_cast<size_t>(i)]);
}

void MockSystem::updateSafetyConditions()
{
    // 将模拟开关状态转换为状态机需要的安全条件；真实控制器应改为读取硬件反馈。
    const bool allHealthy = std::all_of(m_deviceHealthy.cbegin(), m_deviceHealthy.cend(),
                                        [](bool healthy) { return healthy; });
    const bool pumpsStopped = !m_dryPump && !m_tmp;
    const bool outputsSafe = pumpsStopped && !m_cryocooler && !m_heater && !m_vent;
    m_process.updateSafetyConditions(true, allHealthy, m_temperature, m_vacuum,
                                     outputsSafe, pumpsStopped, !m_gate);
    publishSnapshot();
}

void MockSystem::setOutputsSafe()
{
    // 安全停止和普通停止共用的输出归零顺序。
    setGateValve(false);
    setForeValve(false);
    setVentValve(false);
    setTmp(false);
    setDryPump(false);
    setCryocooler(false);
    setHeater(false);
}

void MockSystem::setDeviceRunning(DeviceIndex index, bool running, const QString &action)
{
    // 统一记录设备启停时刻和事件，供七设备状态带及运行记录使用。
    const size_t position = static_cast<size_t>(index);
    const QDateTime now = QDateTime::currentDateTime();
    if (m_deviceRunning[position] != running) {
        m_deviceRunning[position] = running;
        if (running)
            m_deviceStartedAt[position] = now;
        else
            m_deviceStoppedAt[position] = now;
    }
    if (!action.isEmpty())
        pushLog(action);
}

void MockSystem::pushLog(const QString &message)
{
    // 日志在控制器侧统一添加时间前缀，再同时流向界面和 DataLogger。
    emit logMessage(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + message);
}

double MockSystem::sensorVoltage(double pa) const
{
    // 仅用于模拟冷阴极真空规的 0–10 V 原始反馈，非真实仪表标定公式。
    return std::clamp((std::log10(std::max(pa, 1.0e-6)) + 6.0) / 11.0 * 10.0, 0.0, 10.0);
}

QString MockSystem::phaseText() const
{
    return m_process.snapshot().stateText;
}

void MockSystem::setGateValve(bool open)
{
    // 状态未变化时不重复发信号和日志，避免界面与记录出现重复事件。
    if (m_gate == open) return;
    m_gate = open;
    setDeviceRunning(DevSolenoidValve, m_gate || m_fore || m_vent,
                     QStringLiteral("真空电磁阀：高真空阀%1").arg(open ? QStringLiteral("打开") : QStringLiteral("关闭")));
    emit gateValveChanged(open);
}

void MockSystem::setForeValve(bool open)
{
    if (m_fore == open) return;
    m_fore = open;
    setDeviceRunning(DevSolenoidValve, m_gate || m_fore || m_vent,
                     QStringLiteral("真空电磁阀：前级阀%1").arg(open ? QStringLiteral("打开") : QStringLiteral("关闭")));
    emit foreValveChanged(open);
}

void MockSystem::setVentValve(bool open)
{
    if (m_vent == open) return;
    m_vent = open;
    setDeviceRunning(DevSolenoidValve, m_gate || m_fore || m_vent,
                     QStringLiteral("真空电磁阀：放空阀%1").arg(open ? QStringLiteral("打开") : QStringLiteral("关闭")));
    emit ventValveChanged(open);
}

void MockSystem::setDryPump(bool on)
{
    if (m_dryPump == on) return;
    m_dryPump = on;
    setDeviceRunning(DevDryPump, on, QStringLiteral("干泵%1").arg(on ? QStringLiteral("启动") : QStringLiteral("停止")));
    emit dryPumpChanged(on);
}

void MockSystem::setTmp(bool on)
{
    if (m_tmp == on) return;
    m_tmp = on;
    setDeviceRunning(DevMolecularPump, on, QStringLiteral("分子泵%1").arg(on ? QStringLiteral("启动") : QStringLiteral("停止")));
    emit tmpChanged(on);
}

void MockSystem::setTmpSpeed(int rpm)
{
    m_tmpSpeed = std::clamp(rpm, 0, 90000);
    pushLog(QStringLiteral("分子泵转速设定为 %1 rpm").arg(m_tmpSpeed));
    emit tmpSpeedChanged(m_tmpSpeed);
}

void MockSystem::setCryocooler(bool on)
{
    // 冷水机、压缩机和温控仪在演示中关联启停，真实设备的依赖关系待硬件确认。
    if (m_cryocooler == on) return;
    m_cryocooler = on;
    setDeviceRunning(DevChiller, on, QStringLiteral("冷水机%1").arg(on ? QStringLiteral("启动") : QStringLiteral("停止")));
    setDeviceRunning(DevCompressor, on, QStringLiteral("压缩机%1").arg(on ? QStringLiteral("启动") : QStringLiteral("停止")));
    setDeviceRunning(DevTemperatureController, on || m_heater,
                     QStringLiteral("温控仪%1").arg(on || m_heater ? QStringLiteral("启动") : QStringLiteral("停止")));
    emit cryocoolerChanged(on);
}

void MockSystem::setHeater(bool on)
{
    if (m_heater == on) return;
    m_heater = on;
    setDeviceRunning(DevTemperatureController, m_cryocooler || on,
                     QStringLiteral("温控仪加热输出%1").arg(on ? QStringLiteral("开启") : QStringLiteral("关闭")));
    emit heaterChanged(on);
}

void MockSystem::setPidParams(double p, double i, double d, double target)
{
    // 当前仅保存模拟参数和记录日志，尚未实现真实 PID 闭环。
    m_pidP = p; m_pidI = i; m_pidD = d; m_pidTarget = target;
    pushLog(QStringLiteral("PID 参数已应用"));
}

void MockSystem::applyConfig(double roughPa, double highPa,
                             double coolRateKPerMin, double warmRateKPerMin)
{
    // 工程师页参数在模拟器中立即生效；高真空门限变化还会通知操作员摘要区。
    m_roughLimit = std::max(0.01, roughPa);
    m_highLimit = std::max(1.0e-8, highPa);
    m_coolingRate = std::clamp(coolRateKPerMin, 0.1, 5.0);
    m_warmingRate = std::max(0.1, warmRateKPerMin);
    emit highLimitChanged(m_highLimit);
    pushLog(QStringLiteral("系统参数已应用"));
}

void MockSystem::resetConfig()
{
    applyConfig(5.0, 1.0e-3, 1.0, 3.0);
}

void MockSystem::setDeviceHealth(int index, bool healthy, const QString &message)
{
    // 供测试或未来故障注入调用；关键设备故障会触发状态机安全停止和报警。
    if (index < 0 || index >= DevCount)
        return;
    m_deviceHealthy[static_cast<size_t>(index)] = healthy;
    m_deviceMessage[static_cast<size_t>(index)] = message;
    publishDevices();
    if (!healthy && m_process.snapshot().commands.canSafetyStop) {
        m_process.requestSafetyStop(message.isEmpty() ? QStringLiteral("设备故障") : message, index);
        m_stateTicks = 0;
        AlarmStatus alarm;
        alarm.active = true;
        alarm.count = 1;
        alarm.text = message.isEmpty() ? QStringLiteral("设备故障") : message;
        alarm.deviceIndex = index;
        alarm.occurredAt = QDateTime::currentDateTime();
        emit alarmChanged(alarm);
        publishSnapshot();
    }
}
