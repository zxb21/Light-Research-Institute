#ifndef PROCESSSTATEMACHINE_H
#define PROCESSSTATEMACHINE_H

#include "SystemTypes.h"

// 自动流程状态机：决定状态能否转换、按钮何时可用，以及七步流程图如何着色。
// 它只处理流程规则，不操作计时器、设备或界面。
class ProcessStateMachine
{
public:
    ProcessStateMachine();

    const ProcessSnapshot &snapshot() const { return m_snapshot; }
    const FaultRecord &fault() const { return m_fault; }

    // 操作请求；返回 false 表示当前快照的权限不允许该操作。
    bool requestStart();
    bool requestStop(double temperatureK, double pressurePa);
    bool requestSafetyStop(const QString &reason, int deviceIndex = -1);
    bool requestVent();
    bool requestReset();

    // 自动推进、故障恢复和安全条件同步入口。
    bool advanceTo(SystemState state);
    void enterResetRequired();
    void enterRecovering();
    void completeRecovery();
    void updateSafetyConditions(bool communicationConnected,
                                bool devicesHealthy,
                                double temperatureK,
                                double pressurePa,
                                bool outputsSafe,
                                bool pumpsStopped,
                                bool mainValveClosed);

private:
    // 状态与界面步骤的映射、显示文字和内部安全判断。
    static ProcessStep stepForState(SystemState state);
    static QString stateText(SystemState state);
    static QString detailText(SystemState state);
    static int stepIndex(ProcessStep step);
    static bool isNormalActive(SystemState state);
    static bool isCoolingState(SystemState state);
    bool isSafeForReset() const;
    void setState(SystemState state);
    void resetLedger();
    void refreshSnapshot();

    // 对外发布的单一流程真相来源，以及故障时冻结的现场。
    ProcessSnapshot m_snapshot;
    FaultRecord m_fault;
    ProcessStep m_interruptedStep = ProcessStep::Preparation;
    // 最近一次由控制器更新的安全条件；refreshSnapshot() 据此计算按钮权限。
    bool m_communicationConnected = true;
    bool m_devicesHealthy = true;
    bool m_outputsSafe = true;
    bool m_pumpsStopped = true;
    bool m_mainValveClosed = true;
    double m_temperatureK = 295.0;
    double m_pressurePa = 101325.0;
};

#endif // PROCESSSTATEMACHINE_H
