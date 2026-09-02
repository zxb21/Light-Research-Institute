#ifndef MOCKSYSTEM_H
#define MOCKSYSTEM_H

#include "ProcessStateMachine.h"
#include "SystemController.h"

#include <QDateTime>

#include <array>

class QTimer;

// 当前演示版控制器：以定时器生成模拟温度、压强和设备状态。
// 它不是实际硬件策略；接入硬件时应以新的 SystemController 实现替换它。
class MockSystem : public SystemController
{
    Q_OBJECT

public:
    // 工程师页使用的数组下标，顺序必须与对应标签数组保持一致。
    enum DiIndex {
        DiWaterFlow, DiEStop, DiWatchdog, DiDoorInterlock,
        DiHeaterOvertemp, DiCompressorHp, DiCount
    };
    enum SensorIndex {
        SnsVgCold, SnsVgPirani, SnsTempA, SnsTempB,
        SnsHeaterCurrent, SnsCompressorPower, SnsCount
    };
    enum DeviceIndex {
        DevDryPump, DevMolecularPump, DevChiller, DevVacuumGauge,
        DevTemperatureController, DevCompressor, DevSolenoidValve, DevCount
    };

    explicit MockSystem(QObject *parent = nullptr);

public slots:
    // SystemController 的具体实现：处理界面请求并发布模拟结果。
    void requestStart(double coolingRateKPerMin) override;
    void requestStop() override;
    void requestSafetyStop() override;
    void requestVent() override;
    void requestReset() override;
    void setGateValve(bool open) override;
    void setForeValve(bool open) override;
    void setVentValve(bool open) override;
    void setDryPump(bool on) override;
    void setTmp(bool on) override;
    void setTmpSpeed(int rpm) override;
    void setCryocooler(bool on) override;
    void setHeater(bool on) override;
    void setPidParams(double p, double i, double d, double target) override;
    void applyConfig(double roughPa, double highPa,
                     double coolRateKPerMin, double warmRateKPerMin) override;
    void resetConfig() override;
    void setDeviceHealth(int index, bool healthy, const QString &message);

private:
    // 一个定时周期内的模拟推进、数据发布和安全条件同步。
    void tick();
    void enterState(SystemState state);
    void publishSnapshot();
    void publishDevices();
    void publishTelemetry();
    void updateSensors();
    void updateSafetyConditions();
    void setOutputsSafe();
    void setDeviceRunning(DeviceIndex index, bool running, const QString &action);
    void pushLog(const QString &message);
    double sensorVoltage(double pa) const;
    QString phaseText() const;

    // 模拟运行时状态；物理单位仍保留 K、Pa、K/min，时间被特意压缩用于演示。
    QTimer *m_timer = nullptr;
    ProcessStateMachine m_process;
    int m_ticks = 0;
    int m_stateTicks = 0;
    double m_vacuum = 101325.0;
    double m_temperature = 295.0;
    double m_roughLimit = 5.0;
    double m_highLimit = 1.0e-3;
    double m_coolingRate = 1.0;
    double m_warmingRate = 3.0;
    double m_pidP = 10.0;
    double m_pidI = 0.5;
    double m_pidD = 0.0;
    double m_pidTarget = 295.0;
    bool m_gate = false;
    bool m_fore = false;
    bool m_vent = false;
    bool m_dryPump = false;
    bool m_tmp = false;
    bool m_cryocooler = false;
    bool m_heater = false;
    bool m_stopRequiresWarming = false;
    int m_tmpSpeed = 45000;
    std::array<bool, DiCount> m_di = {};
    std::array<bool, DevCount> m_deviceHealthy = {};
    std::array<QString, DevCount> m_deviceMessage;
    std::array<bool, DevCount> m_deviceRunning = {};
    std::array<QDateTime, DevCount> m_deviceStartedAt;
    std::array<QDateTime, DevCount> m_deviceStoppedAt;
};

#endif // MOCKSYSTEM_H
