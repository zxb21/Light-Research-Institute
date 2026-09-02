#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "SystemTypes.h"

#include <QMainWindow>

class DataLogger;
class EngineerWidget;
class QLabel;
class OperatorWidget;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class SystemController;

// 应用窗口外壳：只负责页面切换和对象接线，不直接决定流程或硬件动作。
// 操作员发出的请求经由本类转交给 SystemController，控制器发回的数据再分发给界面。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // 顶部栏的时钟、通信状态、报警状态和模式切换入口。
    void updateClock();
    void onHeartbeat(bool connected);
    void onAlarmChanged(const AlarmStatus &alarm);
    void onToggleModeClicked();

private:
    // 构建顶部栏、切换两个大页面、连接界面与控制器的信号。
    void buildHeaderBar(QVBoxLayout *shellLayout);
    void setEngineerMode(bool enabled);
    void wireSystem();

    // 顶部栏控件。
    QLabel *m_modeLabel = nullptr;
    QLabel *m_clockLabel = nullptr;
    QLabel *m_communicationLabel = nullptr;
    QLabel *m_alarmLabel = nullptr;
    QPushButton *m_modeButton = nullptr;
    // 第一层页面切换器：索引 0 为操作员，索引 1 为工程师。
    QStackedWidget *m_stack = nullptr;
    OperatorWidget *m_operator = nullptr;
    EngineerWidget *m_engineer = nullptr;
    QTimer *m_clockTimer = nullptr;
    // 控制器抽象；当前实例为 MockSystem，真实硬件接入时替换实现而非改界面。
    SystemController *m_system = nullptr;
    DataLogger *m_logger = nullptr;
    bool m_engineerMode = false;
};

#endif // MAINWINDOW_H
