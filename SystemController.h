#ifndef SYSTEMCONTROLLER_H
#define SYSTEMCONTROLLER_H

#include "SystemTypes.h"

#include <QObject>

class SystemController : public QObject
{
    Q_OBJECT

public:
    explicit SystemController(QObject *parent = nullptr) : QObject(parent) {}
    ~SystemController() override = default;

signals:
    void processSnapshotChanged(const ProcessSnapshot &snapshot);
    void telemetryReady(const TelemetryPoint &point);
    void deviceStatusChanged(const DeviceStatus &device);
    void communicationChanged(bool connected);
    void alarmChanged(const AlarmStatus &alarm);

    void vacuumChanged(double pa);
    void temperatureChanged(double k);
    void phaseChanged(const QString &text);
    void highLimitChanged(double pa);
    void heartbeat(bool ok);
    void gateValveChanged(bool open);
    void foreValveChanged(bool open);
    void ventValveChanged(bool open);
    void dryPumpChanged(bool on);
    void tmpChanged(bool on);
    void tmpSpeedChanged(int rpm);
    void cryocoolerChanged(bool on);
    void heaterChanged(bool on);
    void diChanged(int index, bool on);
    void sensorRawChanged(int index, const QString &text);
    void sampleReady(double epochSec, double vacuumPa, double tempK,
                     const QString &phase);
    void logMessage(const QString &message);

public slots:
    virtual void requestStart(double coolingRateKPerMin) = 0;
    virtual void requestStop() = 0;
    virtual void requestSafetyStop() = 0;
    virtual void requestVent() = 0;
    virtual void requestReset() = 0;
    virtual void setGateValve(bool open) = 0;
    virtual void setForeValve(bool open) = 0;
    virtual void setVentValve(bool open) = 0;
    virtual void setDryPump(bool on) = 0;
    virtual void setTmp(bool on) = 0;
    virtual void setTmpSpeed(int rpm) = 0;
    virtual void setCryocooler(bool on) = 0;
    virtual void setHeater(bool on) = 0;
    virtual void setPidParams(double p, double i, double d, double target) = 0;
    virtual void applyConfig(double roughPa, double highPa,
                             double coolRateKPerMin, double warmRateKPerMin) = 0;
    virtual void resetConfig() = 0;
};

#endif // SYSTEMCONTROLLER_H
