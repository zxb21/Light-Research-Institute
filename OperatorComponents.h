#ifndef OPERATORCOMPONENTS_H
#define OPERATORCOMPONENTS_H

#include "SystemTypes.h"

#include <QWidget>

#include <array>

class QKeyEvent;
class QMouseEvent;

class ProcessFlowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessFlowWidget(QWidget *parent = nullptr);
    void setSnapshot(const ProcessSnapshot &snapshot);

signals:
    void stepActivated(ProcessStep step);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    int stepIndexAt(int y) const;

    ProcessSnapshot m_snapshot;
    int m_selectedStep = 0;
};

class DeviceHealthWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceHealthWidget(QWidget *parent = nullptr);
    void setDeviceStatus(const DeviceStatus &status);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::array<DeviceStatus, 7> m_devices;
};

#endif // OPERATORCOMPONENTS_H
