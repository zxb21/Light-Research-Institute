#ifndef SYSTEMTYPES_H
#define SYSTEMTYPES_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <array>
#include <cmath>
#include <limits>

// 压强的唯一显示格式入口：真空计、主界面摘要和趋势图都应调用这里。
// 修改系数精度、上标样式或单位前缀时，只改此函数即可保持三处一致。
inline QString formatPressure(double pa)
{
    if (pa <= 0.0 || !std::isfinite(pa))
        return QString();

    const int exponent = static_cast<int>(std::floor(std::log10(pa)));
    const double coefficient = pa / std::pow(10.0, exponent);
    const double rounded = std::round(coefficient);
    const QString coefficientText = std::abs(coefficient - rounded) < 0.05
        ? QString::number(static_cast<int>(rounded))
        : QString::number(coefficient, 'f', 1);

    static const QChar superscripts[] = {
        QChar(0x2070), QChar(0x00B9), QChar(0x00B2), QChar(0x00B3), QChar(0x2074),
        QChar(0x2075), QChar(0x2076), QChar(0x2077), QChar(0x2078), QChar(0x2079)
    };
    QString exponentText;
    if (exponent < 0)
        exponentText.append(QChar(0x207B));
    for (QChar digit : QString::number(std::abs(exponent)))
        exponentText.append(superscripts[digit.digitValue()]);
    return coefficientText + QStringLiteral("×10") + exponentText;
}

// 自动流程的完整系统状态；用于控制器判断，不依赖中文显示文字。
enum class SystemState {
    Booting,
    Idle,
    Preparing,
    Roughing,
    HighVacuum,
    PreCooling,
    DeepCooling,
    Stabilizing,
    Running,
    Stopping,
    Warming,
    ReadyToVent,
    Venting,
    Recovering,
    SafetyStopping,
    Fault,
    ResetRequired
};

// 操作员看见的七个步骤，与 SystemState 一一映射或按阶段合并映射。
enum class ProcessStep {
    Preparation,
    RoughVacuum,
    HighVacuum,
    PreCooling,
    DeepCooling,
    StabilizeAndRun,
    VentAndRecover
};

// 某个步骤在流程图上的显示状态。
enum class StepState {
    Pending,
    Active,
    Completed,
    Interrupted,
    Faulted,
    Recovering
};

// 七设备状态带使用的健康等级。
enum class DeviceHealth {
    Normal,
    Running,
    Standby,
    Warning,
    Fault,
    Offline
};

struct CommandAvailability
{
    // 控制器计算出的按钮权限及不可操作原因；界面只负责据此显示。
    bool canStart = false;
    bool canStop = false;
    bool canSafetyStop = false;
    bool canVent = false;
    bool canReset = false;
    QString startReason;
    QString stopReason;
    QString ventReason;
    QString resetReason;
};

struct ProcessSnapshot
{
    // 控制器一次性发布给操作员界面的流程快照。
    SystemState state = SystemState::Booting;
    ProcessStep currentStep = ProcessStep::Preparation;
    std::array<StepState, 7> steps = {
        StepState::Pending, StepState::Pending, StepState::Pending,
        StepState::Pending, StepState::Pending, StepState::Pending,
        StepState::Pending
    };
    CommandAvailability commands;
    QString stateText;
    QString detailText;
    int progressPercent = 0;
};

struct DeviceStatus
{
    // 单个设备状态；keyValue 是设备卡片第二行显示的文字。
    // 真空计的 keyValue 由 MockSystem 调用 formatPressure() 生成。
    int index = -1;
    QString name;
    DeviceHealth health = DeviceHealth::Offline;
    QString message;
    QString keyValue;
    QDateTime updatedAt;
    QDateTime startedAt;
    QDateTime stoppedAt;
    qint64 runningSeconds = 0;
};

struct AlarmStatus
{
    // 顶部栏和工程师界面使用的当前报警摘要。
    bool active = false;
    int count = 0;
    QString text;
    QDateTime occurredAt;
    int deviceIndex = -1;
};

// 单次自动运行写入 metadata.json 的最小摘要；旧目录缺字段时界面显示“暂无数据”。
struct RunSummary
{
    QDateTime startedAt;
    QDateTime endedAt;
    QString result;
    QString lastPhase;
    double lowestTemperatureK = std::numeric_limits<double>::quiet_NaN();
    double finalVacuumPa = std::numeric_limits<double>::quiet_NaN();
    int alarmCount = 0;
    QString directory;
};

struct FaultRecord
{
    // 安全停止时保留下来的故障现场，供复位前诊断和界面提示使用。
    bool valid = false;
    SystemState state = SystemState::Idle;
    ProcessStep step = ProcessStep::Preparation;
    std::array<StepState, 7> steps = {
        StepState::Pending, StepState::Pending, StepState::Pending,
        StepState::Pending, StepState::Pending, StepState::Pending,
        StepState::Pending
    };
    int deviceIndex = -1;
    QString text;
    QDateTime occurredAt;
};

struct TelemetryPoint
{
    // 一次采样的温度、压强和流程归属；趋势图和 DataLogger 都消费此结构。
    double epochSec = 0.0;
    double vacuumPa = 0.0;
    double temperatureK = 0.0;
    QString phase;
    SystemState state = SystemState::Idle;
    ProcessStep step = ProcessStep::Preparation;
};

Q_DECLARE_METATYPE(SystemState)
Q_DECLARE_METATYPE(ProcessStep)
Q_DECLARE_METATYPE(StepState)
Q_DECLARE_METATYPE(DeviceHealth)
Q_DECLARE_METATYPE(CommandAvailability)
Q_DECLARE_METATYPE(ProcessSnapshot)
Q_DECLARE_METATYPE(DeviceStatus)
Q_DECLARE_METATYPE(AlarmStatus)
Q_DECLARE_METATYPE(RunSummary)
Q_DECLARE_METATYPE(TelemetryPoint)

#endif // SYSTEMTYPES_H
