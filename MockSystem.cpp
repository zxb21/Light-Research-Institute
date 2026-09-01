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
    return rateKPerMin * kTickSeconds / 60.0 * kSimulationTimeScale;
}

} // namespace

MockSystem::MockSystem(QObject *parent)
    : SystemController(parent)
    , m_timer(new QTimer(this))
{
    m_di = { true, true, true, true, true, true };
    m_deviceHealthy.fill(true);
    for (QString &message : m_deviceMessage)
        message = QStringLiteral("通信正常");

    connect(m_timer, &QTimer::timeout, this, &MockSystem::tick);
    m_timer->start(250);
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
    updateSafetyConditions();
    if (!m_process.requestStart()) {
        pushLog(QStringLiteral("启动请求被拒绝：%1")
                    .arg(m_process.snapshot().commands.startReason));
        return;
    }
    m_coolingRate = std::clamp(coolingRateKPerMin, 0.1, 5.0);
    m_stateTicks = 0;
    pushLog(QStringLiteral("一键启动，降温速率 %1 K/min").arg(m_coolingRate, 0, 'f', 1));
    publishSnapshot();
}

void MockSystem::requestStop()
{
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
    ++m_ticks;
    ++m_stateTicks;
    const SystemState state = m_process.snapshot().state;

    switch (state) {
    case SystemState::Preparing:
        if (m_stateTicks >= 5) {
            setDryPump(true);
            setForeValve(true);
            enterState(SystemState::Roughing);
        }
        break;
    case SystemState::Roughing:
        m_vacuum = std::max(m_roughLimit, m_vacuum * 0.85);
        if (m_vacuum <= m_roughLimit + 1.0e-9) {
            setTmp(true);
            enterState(SystemState::HighVacuum);
        }
        break;
    case SystemState::HighVacuum:
        m_vacuum = std::max(m_highLimit, m_vacuum * 0.75);
        if (m_vacuum <= m_highLimit + 1.0e-12) {
            setForeValve(false);
            setGateValve(true);
            enterState(SystemState::PreCooling);
        }
        break;
    case SystemState::PreCooling:
        if (!m_cryocooler)
            setCryocooler(true);
        if (m_stateTicks >= 6)
            enterState(SystemState::DeepCooling);
        break;
    case SystemState::DeepCooling:
        m_temperature = std::max(2.8, m_temperature - temperatureDeltaForTick(m_coolingRate));
        m_vacuum = std::max(1.0e-5, m_vacuum * 0.98);
        if (m_temperature <= 2.8)
            enterState(SystemState::Stabilizing);
        break;
    case SystemState::Stabilizing:
        m_temperature = 2.8 + std::sin(m_ticks * 0.35) * 0.04;
        if (m_stateTicks >= 12)
            enterState(SystemState::Running);
        break;
    case SystemState::Running:
        m_temperature = 2.8 + std::sin(m_ticks * 0.25) * 0.03;
        m_vacuum = std::max(1.0e-5, m_vacuum * (1.0 + std::sin(m_ticks * 0.2) * 0.01));
        break;
    case SystemState::Stopping:
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
    if (!m_process.advanceTo(state))
        return;
    m_stateTicks = 0;
    pushLog(m_process.snapshot().stateText);
    publishSnapshot();
}

void MockSystem::publishSnapshot()
{
    const ProcessSnapshot snapshot = m_process.snapshot();
    emit processSnapshotChanged(snapshot);
    emit phaseChanged(snapshot.stateText);
}

void MockSystem::publishDevices()
{
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
        switch (i) {
        case DevDryPump: device.keyValue = m_dryPump ? QStringLiteral("已启动") : QStringLiteral("已停止"); break;
        case DevMolecularPump: device.keyValue = m_tmp ? QStringLiteral("%1 rpm").arg(m_tmpSpeed) : QStringLiteral("已停止"); break;
        case DevChiller: device.keyValue = m_cryocooler ? QStringLiteral("18.0 ℃") : QStringLiteral("待机"); break;
        case DevVacuumGauge: device.keyValue = QStringLiteral("%1 Pa").arg(m_vacuum, 0, 'e', 1); break;
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
    emit logMessage(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + message);
}

double MockSystem::sensorVoltage(double pa) const
{
    return std::clamp((std::log10(std::max(pa, 1.0e-6)) + 6.0) / 11.0 * 10.0, 0.0, 10.0);
}

QString MockSystem::phaseText() const
{
    return m_process.snapshot().stateText;
}

void MockSystem::setGateValve(bool open)
{
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
    m_pidP = p; m_pidI = i; m_pidD = d; m_pidTarget = target;
    pushLog(QStringLiteral("PID 参数已应用"));
}

void MockSystem::applyConfig(double roughPa, double highPa,
                             double coolRateKPerMin, double warmRateKPerMin)
{
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
