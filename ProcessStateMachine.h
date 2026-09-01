#ifndef PROCESSSTATEMACHINE_H
#define PROCESSSTATEMACHINE_H

#include "SystemTypes.h"

class ProcessStateMachine
{
public:
    ProcessStateMachine();

    const ProcessSnapshot &snapshot() const { return m_snapshot; }
    const FaultRecord &fault() const { return m_fault; }

    bool requestStart();
    bool requestStop(double temperatureK, double pressurePa);
    bool requestSafetyStop(const QString &reason, int deviceIndex = -1);
    bool requestVent();
    bool requestReset();

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

    ProcessSnapshot m_snapshot;
    FaultRecord m_fault;
    ProcessStep m_interruptedStep = ProcessStep::Preparation;
    bool m_communicationConnected = true;
    bool m_devicesHealthy = true;
    bool m_outputsSafe = true;
    bool m_pumpsStopped = true;
    bool m_mainValveClosed = true;
    double m_temperatureK = 295.0;
    double m_pressurePa = 101325.0;
};

#endif // PROCESSSTATEMACHINE_H
