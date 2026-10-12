#ifndef OPERATORCOMPONENTS_H
#define OPERATORCOMPONENTS_H

#include "SystemTypes.h"

#include <QFrame>
#include <QPushButton>
#include <QWidget>

#include <array>

class QKeyEvent;
class QMouseEvent;
class QPainterPath;
class QResizeEvent;

// “方案一甲”的三段式流程按钮。按钮自己绘制斜切边缘、步骤编号和圆形动作图标，
// 因为普通 QSS 只能画矩形边框，无法准确得到效果图中的箭头形流程轨。
class FlowCommandButton : public QPushButton
{
    Q_OBJECT

public:
    enum class Position { First, Middle, Last };
    enum class Symbol { Play, Stop, Vent };

    FlowCommandButton(const QString &text, int step, Position position, Symbol symbol,
                      QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPainterPath shapePath() const;
};

// 三段按钮在轨道内重叠斜切宽度，后一段的左缺口与前一段箭头共用同一条边。
class FlowCommandRail : public QFrame
{
public:
    explicit FlowCommandRail(QWidget *parent = nullptr);
    void setButtons(FlowCommandButton *start, FlowCommandButton *stop,
                    FlowCommandButton *vent);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void positionButtons();
    std::array<FlowCommandButton *, 3> m_buttons{};
};

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
