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

class OperatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OperatorWidget(QWidget *parent = nullptr);

signals:
    void startRequested(double coolingRateKPerMin);
    void stopRequested();
    void ventRequested();
    void dataExportRequested();

public slots:
    void onProcessSnapshot(const ProcessSnapshot &snapshot);
    void onTelemetryReady(const TelemetryPoint &point);
    void onDeviceStatus(const DeviceStatus &device);
    void onHighLimitChanged(double pa);
    void onLogMessage(const QString &message);

private:
    QWidget *buildOverviewPage();
    QWidget *buildDetailPage();
    QWidget *buildSystemStatusPanel();
    QWidget *buildControlPanel();
    void refreshSummary();
    void refreshCommands();
    void refreshDetail();
    void showProcessDetail(ProcessStep step);
    void showOverview();
    void showExportDialog();
    void exportTrendImage();
    void confirmVent();

    QStackedWidget *m_pages = nullptr;
    ProcessFlowWidget *m_processFlow = nullptr;
    DeviceHealthWidget *m_deviceHealth = nullptr;
    TrendChartWidget *m_detailChart = nullptr;
    TelemetryStore *m_store = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_stateDetail = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_temperatureTarget = nullptr;
    QLabel *m_temperatureRate = nullptr;
    QLabel *m_pressureValue = nullptr;
    QLabel *m_pressureLimit = nullptr;
    QLabel *m_commandReason = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailState = nullptr;
    QLabel *m_detailDescription = nullptr;
    QLabel *m_detailCondition = nullptr;
    QLabel *m_detailTrendTitle = nullptr;
    QLabel *m_detailTrendPlaceholder = nullptr;
    QListWidget *m_overviewEvents = nullptr;
    QListWidget *m_eventList = nullptr;
    QDoubleSpinBox *m_coolingRate = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_ventButton = nullptr;
    ProcessSnapshot m_snapshot;
    double m_temperatureK = 295.0;
    double m_pressurePa = 101325.0;
    double m_highLimitPa = 1.0e-3;
    ProcessStep m_detailStep = ProcessStep::Preparation;
};

#endif // OPERATORWIDGET_H
