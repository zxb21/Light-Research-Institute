#ifndef OPERATORCOMPONENTS_H
#define OPERATORCOMPONENTS_H

#include "SystemTypes.h"

#include <QWidget>

#include <array>

class QKeyEvent;
class QMouseEvent;

// 左侧七步流程的自绘控件：负责画图和把鼠标/键盘操作转换为步骤信号。
// 它不切换页面；OperatorWidget 收到 stepActivated 后负责显示详情。
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

// 七设备状态带的自绘控件：显示控制器传入的名称、健康状态和关键读数。
// 真空计的压强文字已经在 MockSystem::publishDevices() 中格式化后传入。
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
