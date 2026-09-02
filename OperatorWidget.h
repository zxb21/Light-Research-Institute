#ifndef OPERATORWIDGET_H
#define OPERATORWIDGET_H

#include "SystemTypes.h"

#include <QWidget>

class DeviceHealthWidget;
class QLabel;
class QListWidget;
class QPushButton;
class QDoubleSpinBox;
class ProcessFlowWidget;
class QStackedWidget;
class TelemetryStore;
class TrendChartWidget;

// 操作员界面：展示自动流程、设备状态和核心操作。
// 若要改文字、布局或按钮，优先查看本类；若要改七步流程绘制，查看 ProcessFlowWidget。
class OperatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OperatorWidget(QWidget *parent = nullptr);

signals:
    // 仅向控制器提出操作请求；界面不直接读写硬件或改变流程状态。
    void startRequested(double coolingRateKPerMin);
    void stopRequested();
    void ventRequested();
    void dataExportRequested();

public slots:
    // MainWindow 将控制器发布的快照、遥测、设备状态和日志接到这些刷新入口。
    void onProcessSnapshot(const ProcessSnapshot &snapshot);
    void onTelemetryReady(const TelemetryPoint &point);
    void onDeviceStatus(const DeviceStatus &device);
    void onHighLimitChanged(double pa);
    void onLogMessage(const QString &message);

private:
    // 构建两个内部页面及主界面的三个独立面板。
    QWidget *buildOverviewPage();
    QWidget *buildDetailPage();
    QWidget *buildSystemStatusPanel();
    QWidget *buildControlPanel();
    // 将缓存的最新数据写入已创建的标签和按钮，不重新创建控件。
    void refreshSummary();
    void refreshCommands();
    void refreshDetail();
    void showProcessDetail(ProcessStep step);
    void showOverview();
    void showExportDialog();
    void exportTrendImage();
    void confirmVent();

    // 第二层页面切换器：索引 0 为总览，索引 1 为单步骤详情。
    QStackedWidget *m_pages = nullptr;
    ProcessFlowWidget *m_processFlow = nullptr;
    DeviceHealthWidget *m_deviceHealth = nullptr;
    TrendChartWidget *m_detailChart = nullptr;
    // 仅供详情趋势图读取的界面侧历史缓存。
    TelemetryStore *m_store = nullptr;
    // 主界面摘要区：系统状态、温度、压强及操作提示。
    QLabel *m_stateValue = nullptr;
    QLabel *m_stateDetail = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_temperatureTarget = nullptr;
    QLabel *m_temperatureRate = nullptr;
    QLabel *m_pressureValue = nullptr;
    QLabel *m_pressureLimit = nullptr;
    QLabel *m_commandReason = nullptr;
    // 详情页：当前步骤的标题、说明、条件和趋势占位文字。
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailState = nullptr;
    QLabel *m_detailDescription = nullptr;
    QLabel *m_detailCondition = nullptr;
    QLabel *m_detailTrendTitle = nullptr;
    QLabel *m_detailTrendPlaceholder = nullptr;
    // 总览保留本次界面生命周期内的全部记录；详情页保留最近 200 条事件。
    QListWidget *m_overviewEvents = nullptr;
    QListWidget *m_eventList = nullptr;
    // 启动前可编辑的降温速率；步长在 buildControlPanel() 中设置。
    QDoubleSpinBox *m_coolingRate = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_ventButton = nullptr;
    // 从控制器收到的最新快照及最新遥测值；所有刷新函数以这些缓存为准。
    ProcessSnapshot m_snapshot;
    double m_temperatureK = 295.0;
    double m_pressurePa = 101325.0;
    double m_highLimitPa = 1.0e-3;
    // 用户当前在详情页选中的步骤，不一定等于控制器正在执行的步骤。
    ProcessStep m_detailStep = ProcessStep::Preparation;
};

#endif // OPERATORWIDGET_H
