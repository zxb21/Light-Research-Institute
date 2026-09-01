#include "ProcessStateMachine.h"

#include <algorithm>

namespace {

constexpr double kSafeTemperatureK = 290.0;
constexpr double kAtmosphereSafePa = 90000.0;

}

ProcessStateMachine::ProcessStateMachine()
{
    resetLedger();
    setState(SystemState::Idle);
}

bool ProcessStateMachine::requestStart()
{
    if (!m_snapshot.commands.canStart)
        return false;
    resetLedger();
    setState(SystemState::Preparing);
    return true;
}

bool ProcessStateMachine::requestStop(double temperatureK, double pressurePa)
{
    if (!m_snapshot.commands.canStop)
        return false;
    m_temperatureK = temperatureK;
    m_pressurePa = pressurePa;
    m_interruptedStep = m_snapshot.currentStep;
    m_snapshot.steps[stepIndex(m_interruptedStep)] = StepState::Interrupted;
    m_snapshot.steps[stepIndex(ProcessStep::VentAndRecover)] = StepState::Recovering;
    setState(SystemState::Stopping);
    return true;
}

bool ProcessStateMachine::requestSafetyStop(const QString &reason, int deviceIndex)
{
    if (!m_snapshot.commands.canSafetyStop)
        return false;

    m_fault.valid = true;
    m_fault.state = m_snapshot.state;
    m_fault.step = m_snapshot.currentStep;
    m_fault.steps = m_snapshot.steps;
    m_fault.deviceIndex = deviceIndex;
    m_fault.text = reason;
    m_fault.occurredAt = QDateTime::currentDateTime();
    m_fault.steps[stepIndex(m_fault.step)] = StepState::Faulted;
    m_snapshot.steps[stepIndex(m_fault.step)] = StepState::Faulted;
    setState(SystemState::SafetyStopping);
    m_snapshot.currentStep = m_fault.step;
    m_snapshot.detailText = QStringLiteral("%1 · %2")
                                .arg(reason, m_fault.occurredAt.toString(QStringLiteral("HH:mm:ss")));
    refreshSnapshot();
    return true;
}

bool ProcessStateMachine::requestVent()
{
    if (!m_snapshot.commands.canVent)
        return false;
    setState(SystemState::Venting);
    return true;
}

bool ProcessStateMachine::requestReset()
{
    if (!m_snapshot.commands.canReset)
        return false;
    m_fault = FaultRecord();
    resetLedger();
    setState(SystemState::Idle);
    return true;
}

bool ProcessStateMachine::advanceTo(SystemState next)
{
    const SystemState current = m_snapshot.state;
    bool allowed = false;
    switch (current) {
    case SystemState::Preparing: allowed = next == SystemState::Roughing; break;
    case SystemState::Roughing: allowed = next == SystemState::HighVacuum; break;
    case SystemState::HighVacuum: allowed = next == SystemState::PreCooling; break;
    case SystemState::PreCooling: allowed = next == SystemState::DeepCooling; break;
    case SystemState::DeepCooling: allowed = next == SystemState::Stabilizing; break;
    case SystemState::Stabilizing: allowed = next == SystemState::Running; break;
    case SystemState::Stopping:
        allowed = next == SystemState::Warming || next == SystemState::ReadyToVent;
        break;
    case SystemState::Warming: allowed = next == SystemState::ReadyToVent; break;
    case SystemState::SafetyStopping: allowed = next == SystemState::Fault; break;
    default: break;
    }
    if (!allowed)
        return false;

    if (isNormalActive(current) && isNormalActive(next)) {
        const ProcessStep currentStep = stepForState(current);
        const ProcessStep nextStep = stepForState(next);
        if (currentStep != nextStep)
            m_snapshot.steps[stepIndex(currentStep)] = StepState::Completed;
    }
    setState(next);
    if (next == SystemState::Fault && m_fault.valid) {
        m_snapshot.detailText = QStringLiteral("%1 · %2")
                                    .arg(m_fault.text,
                                         m_fault.occurredAt.toString(QStringLiteral("HH:mm:ss")));
        refreshSnapshot();
    }
    return true;
}

void ProcessStateMachine::enterResetRequired()
{
    if (m_snapshot.state != SystemState::SafetyStopping
        && m_snapshot.state != SystemState::Fault) {
        return;
    }
    setState(SystemState::ResetRequired);
}

void ProcessStateMachine::enterRecovering()
{
    if (m_snapshot.state != SystemState::Venting)
        return;
    setState(SystemState::Recovering);
}

void ProcessStateMachine::completeRecovery()
{
    if (m_snapshot.state != SystemState::Recovering || !isSafeForReset())
        return;
    m_snapshot.steps[stepIndex(ProcessStep::VentAndRecover)] = StepState::Completed;
    resetLedger();
    setState(SystemState::Idle);
}

void ProcessStateMachine::updateSafetyConditions(bool communicationConnected,
                                                 bool devicesHealthy,
                                                 double temperatureK,
                                                 double pressurePa,
                                                 bool outputsSafe,
                                                 bool pumpsStopped,
                                                 bool mainValveClosed)
{
    const bool mustSafetyStop = m_snapshot.commands.canSafetyStop
        && (!communicationConnected || !devicesHealthy);
    m_communicationConnected = communicationConnected;
    m_devicesHealthy = devicesHealthy;
    m_temperatureK = temperatureK;
    m_pressurePa = pressurePa;
    m_outputsSafe = outputsSafe;
    m_pumpsStopped = pumpsStopped;
    m_mainValveClosed = mainValveClosed;
    if (mustSafetyStop) {
        requestSafetyStop(!communicationConnected
                              ? QStringLiteral("控制器通信中断")
                              : QStringLiteral("关键设备故障或离线"));
        return;
    }
    refreshSnapshot();
}

ProcessStep ProcessStateMachine::stepForState(SystemState state)
{
    switch (state) {
    case SystemState::Preparing: return ProcessStep::Preparation;
    case SystemState::Roughing: return ProcessStep::RoughVacuum;
    case SystemState::HighVacuum: return ProcessStep::HighVacuum;
    case SystemState::PreCooling: return ProcessStep::PreCooling;
    case SystemState::DeepCooling: return ProcessStep::DeepCooling;
    case SystemState::Stabilizing:
    case SystemState::Running: return ProcessStep::StabilizeAndRun;
    case SystemState::Stopping:
    case SystemState::Warming:
    case SystemState::ReadyToVent:
    case SystemState::Venting:
    case SystemState::Recovering: return ProcessStep::VentAndRecover;
    default: return ProcessStep::Preparation;
    }
}

QString ProcessStateMachine::stateText(SystemState state)
{
    switch (state) {
    case SystemState::Booting: return QStringLiteral("系统启动中");
    case SystemState::Idle: return QStringLiteral("待机");
    case SystemState::Preparing: return QStringLiteral("系统准备中");
    case SystemState::Roughing: return QStringLiteral("粗抽真空中");
    case SystemState::HighVacuum: return QStringLiteral("高真空建立中");
    case SystemState::PreCooling: return QStringLiteral("预冷中");
    case SystemState::DeepCooling: return QStringLiteral("深冷中");
    case SystemState::Stabilizing: return QStringLiteral("恒温稳定中");
    case SystemState::Running: return QStringLiteral("实验运行中");
    case SystemState::Stopping: return QStringLiteral("正在停止");
    case SystemState::Warming: return QStringLiteral("受控回温中");
    case SystemState::ReadyToVent: return QStringLiteral("等待确认放空");
    case SystemState::Venting: return QStringLiteral("正在放空");
    case SystemState::Recovering: return QStringLiteral("系统恢复中");
    case SystemState::SafetyStopping: return QStringLiteral("安全停止");
    case SystemState::Fault: return QStringLiteral("系统故障");
    case SystemState::ResetRequired: return QStringLiteral("等待复位");
    }
    return QString();
}

QString ProcessStateMachine::detailText(SystemState state)
{
    switch (state) {
    case SystemState::Idle: return QStringLiteral("系统处于大气安全状态，可开始新任务");
    case SystemState::Preparing: return QStringLiteral("检查通信、设备和模拟安全条件");
    case SystemState::Roughing: return QStringLiteral("干泵正在建立前级真空");
    case SystemState::HighVacuum: return QStringLiteral("分子泵正在建立高真空");
    case SystemState::PreCooling: return QStringLiteral("制冷系统预冷准备");
    case SystemState::DeepCooling: return QStringLiteral("按设定速率降至目标温度");
    case SystemState::Stabilizing: return QStringLiteral("等待温度进入稳定区间");
    case SystemState::Running: return QStringLiteral("温度与真空持续监测中");
    case SystemState::Stopping: return QStringLiteral("自动流程已中止，设备正在退出运行");
    case SystemState::Warming: return QStringLiteral("样品温度达到 290 K 后才允许放空");
    case SystemState::ReadyToVent: return QStringLiteral("已满足模拟放空条件，等待操作员确认");
    case SystemState::Venting: return QStringLiteral("腔体压力正在恢复至大气区");
    case SystemState::Recovering: return QStringLiteral("正在确认初始安全条件");
    case SystemState::SafetyStopping: return QStringLiteral("保护动作执行中，故障位置已保留");
    case SystemState::Fault: return QStringLiteral("故障仍未解除");
    case SystemState::ResetRequired: return QStringLiteral("安全条件满足后可复位");
    case SystemState::Booting: return QStringLiteral("模拟控制器正在初始化");
    }
    return QString();
}

int ProcessStateMachine::stepIndex(ProcessStep step)
{
    return static_cast<int>(step);
}

bool ProcessStateMachine::isNormalActive(SystemState state)
{
    return state >= SystemState::Preparing && state <= SystemState::Running;
}

bool ProcessStateMachine::isCoolingState(SystemState state)
{
    return state == SystemState::PreCooling || state == SystemState::DeepCooling
        || state == SystemState::Stabilizing || state == SystemState::Running;
}

bool ProcessStateMachine::isSafeForReset() const
{
    return m_communicationConnected && m_devicesHealthy && m_outputsSafe
        && m_pumpsStopped && m_mainValveClosed
        && m_temperatureK >= kSafeTemperatureK && m_pressurePa >= kAtmosphereSafePa;
}

void ProcessStateMachine::setState(SystemState state)
{
    m_snapshot.state = state;
    if (state != SystemState::SafetyStopping && state != SystemState::Fault
        && state != SystemState::ResetRequired && state != SystemState::Idle) {
        m_snapshot.currentStep = stepForState(state);
    }
    if (isNormalActive(state))
        m_snapshot.steps[stepIndex(m_snapshot.currentStep)] = StepState::Active;
    else if (state == SystemState::Stopping || state == SystemState::Warming
             || state == SystemState::ReadyToVent || state == SystemState::Venting
             || state == SystemState::Recovering) {
        m_snapshot.steps[stepIndex(ProcessStep::VentAndRecover)] = StepState::Recovering;
    }
    m_snapshot.stateText = stateText(state);
    m_snapshot.detailText = detailText(state);
    refreshSnapshot();
}

void ProcessStateMachine::resetLedger()
{
    m_snapshot.steps.fill(StepState::Pending);
    m_snapshot.currentStep = ProcessStep::Preparation;
    m_snapshot.progressPercent = 0;
}

void ProcessStateMachine::refreshSnapshot()
{
    CommandAvailability commands;
    commands.canStart = m_snapshot.state == SystemState::Idle && isSafeForReset();
    commands.canStop = isNormalActive(m_snapshot.state);
    commands.canSafetyStop = isNormalActive(m_snapshot.state)
        || m_snapshot.state == SystemState::Stopping
        || m_snapshot.state == SystemState::Warming
        || m_snapshot.state == SystemState::ReadyToVent
        || m_snapshot.state == SystemState::Venting
        || m_snapshot.state == SystemState::Recovering;
    commands.canVent = m_snapshot.state == SystemState::ReadyToVent
        && m_temperatureK >= kSafeTemperatureK && m_pumpsStopped && m_mainValveClosed;
    commands.canReset = m_snapshot.state == SystemState::ResetRequired && isSafeForReset();

    if (!commands.canStart)
        commands.startReason = m_snapshot.state == SystemState::Idle
            ? QStringLiteral("通信、设备或安全条件尚未满足")
            : QStringLiteral("当前流程未处于待机状态");
    if (!commands.canStop)
        commands.stopReason = QStringLiteral("当前没有可停止的自动流程");
    if (!commands.canVent) {
        if (m_temperatureK < kSafeTemperatureK)
            commands.ventReason = QStringLiteral("样品温度低于 290 K，禁止放空");
        else if (!m_pumpsStopped)
            commands.ventReason = QStringLiteral("泵组尚未停止，禁止放空");
        else if (!m_mainValveClosed)
            commands.ventReason = QStringLiteral("高真空主阀尚未关闭，禁止放空");
        else
            commands.ventReason = QStringLiteral("当前流程状态不允许放空");
    }
    if (!commands.canReset) {
        if (m_snapshot.state != SystemState::ResetRequired)
            commands.resetReason = QStringLiteral("系统尚未进入等待复位状态");
        else if (!m_communicationConnected)
            commands.resetReason = QStringLiteral("通信尚未恢复");
        else if (!m_devicesHealthy)
            commands.resetReason = QStringLiteral("仍有设备故障或离线");
        else if (m_temperatureK < kSafeTemperatureK)
            commands.resetReason = QStringLiteral("样品温度低于 290 K");
        else if (m_pressurePa < kAtmosphereSafePa)
            commands.resetReason = QStringLiteral("腔体压力尚未恢复到大气安全区");
        else if (!m_outputsSafe || !m_pumpsStopped || !m_mainValveClosed)
            commands.resetReason = QStringLiteral("控制输出尚未处于安全状态");
    }
    m_snapshot.commands = commands;

    int progressUnits = 0;
    for (StepState step : m_snapshot.steps) {
        if (step == StepState::Completed || step == StepState::Active
            || step == StepState::Recovering) {
            ++progressUnits;
        }
    }
    m_snapshot.progressPercent = std::min(100, (progressUnits * 100 + 3) / 7);
}
