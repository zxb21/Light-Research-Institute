#ifndef ENGINEERWIDGET_H
#define ENGINEERWIDGET_H

#include <QString>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

// ============================================================================
// EngineerWidget：工程师调试后台（QTabWidget 三个页签）
//   Tab1 硬件单动：阀门 / 泵与制冷 / 加热(PID)
//   Tab2 IO 与诊断：DI 状态 / 传感器模拟量 / 原始报文日志
//   Tab3 系统参数：真空门限与升降温度速率
// ============================================================================
// 工程师调试后台：提供手动控制、诊断回显和模拟参数设置。
// 操作员模式不会展示本控件；切换权限由 MainWindow 管理。
class EngineerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EngineerWidget(QWidget *parent = nullptr);

    static constexpr int kDiCount = 6;
    static constexpr int kSnsCount = 6;

signals:
    // 手动操作和参数修改请求，均由 MainWindow 转交给 SystemController。
    void gateValveSet(bool open);
    void foreValveSet(bool open);
    void ventValveSet(bool open);
    void dryPumpSet(bool on);
    void tmpSet(bool on);
    void tmpSpeedSet(int rpm);
    void cryocoolerSet(bool on);
    void heaterSet(bool on);
    void pidParamsSet(double p, double i, double d, double target);
    void configApplied(double roughPa, double highPa, double coolRateKpm, double warmRateKpm);
    void configResetRequested();

public slots:
    // 控制器状态回显入口；这些槽不应反向决定控制流程。
    void onGateValveChanged(bool open);
    void onForeValveChanged(bool open);
    void onVentValveChanged(bool open);
    void onDryPumpChanged(bool on);
    void onTmpChanged(bool on);
    void onTmpSpeedChanged(int rpm);
    void onCryocoolerChanged(bool on);
    void onHeaterChanged(bool on);
    void onDiChanged(int index, bool on);
    void onSensorRawChanged(int index, const QString &text);
    void onLogMessage(const QString &msg);
    void onVacuumChanged(double pa);
    void onTemperatureChanged(double k);
    void onPhaseChanged(const QString &phase);

private:
    // 一组阀门控件，便于构建和回显时同时访问状态灯与两个按钮。
    struct ValveRow {
        QLabel *led = nullptr;
        QPushButton *onBtn = nullptr;
        QPushButton *offBtn = nullptr;
    };

    ValveRow makeValveRow(const QString &name, QFormLayout *form);
    QWidget *buildManualTab();
    QWidget *buildDiagnosticsTab();
    QWidget *buildConfigTab();

    QTabWidget *m_tabs = nullptr;

    // Tab1 硬件单动
    ValveRow m_gateRow;
    ValveRow m_foreRow;
    ValveRow m_ventRow;
    QLabel *m_dryLed = nullptr;
    QPushButton *m_dryOn = nullptr;
    QPushButton *m_dryOff = nullptr;
    QLabel *m_tmpLed = nullptr;
    QPushButton *m_tmpOn = nullptr;
    QPushButton *m_tmpOff = nullptr;
    QSpinBox *m_tmpSpeed = nullptr;
    QPushButton *m_tmpSpeedBtn = nullptr;
    QLabel *m_cryoLed = nullptr;
    QPushButton *m_cryoOn = nullptr;
    QPushButton *m_cryoOff = nullptr;
    QCheckBox *m_heaterChk = nullptr;
    QDoubleSpinBox *m_pidP = nullptr;
    QDoubleSpinBox *m_pidI = nullptr;
    QDoubleSpinBox *m_pidD = nullptr;
    QDoubleSpinBox *m_pidTarget = nullptr;
    QPushButton *m_pidApply = nullptr;
    QLabel *m_stPhase = nullptr;
    QLabel *m_stVacuum = nullptr;
    QLabel *m_stTemp = nullptr;

    // Tab2 IO 与诊断
    QLabel *m_diLed[kDiCount] = {};
    QLabel *m_diText[kDiCount] = {};
    QLabel *m_snsValue[kSnsCount] = {};
    QPlainTextEdit *m_log = nullptr;

    // Tab3 系统参数
    QDoubleSpinBox *m_cfgRough = nullptr;
    QDoubleSpinBox *m_cfgHigh = nullptr;
    QDoubleSpinBox *m_cfgCoolRate = nullptr;
    QDoubleSpinBox *m_cfgWarmRate = nullptr;
};

#endif // ENGINEERWIDGET_H
