#ifndef SYSTEMTYPES_H
#define SYSTEMTYPES_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <array>

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

enum class ProcessStep {
    Preparation,
    RoughVacuum,
    HighVacuum,
    PreCooling,
    DeepCooling,
    StabilizeAndRun,
    VentAndRecover
};

enum class StepState {
    Pending,
    Active,
    Completed,
    Interrupted,
    Faulted,
    Recovering
};

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
    bool active = false;
    int count = 0;
    QString text;
    QDateTime occurredAt;
    int deviceIndex = -1;
};

struct FaultRecord
{
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
Q_DECLARE_METATYPE(TelemetryPoint)

#endif // SYSTEMTYPES_H
