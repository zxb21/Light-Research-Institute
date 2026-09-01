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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateClock();
    void onHeartbeat(bool connected);
    void onAlarmChanged(const AlarmStatus &alarm);
    void onToggleModeClicked();

private:
    void buildHeaderBar(QVBoxLayout *shellLayout);
    void setEngineerMode(bool enabled);
    void wireSystem();

    QLabel *m_modeLabel = nullptr;
    QLabel *m_clockLabel = nullptr;
    QLabel *m_communicationLabel = nullptr;
    QLabel *m_alarmLabel = nullptr;
    QPushButton *m_modeButton = nullptr;
    QStackedWidget *m_stack = nullptr;
    OperatorWidget *m_operator = nullptr;
    EngineerWidget *m_engineer = nullptr;
    QTimer *m_clockTimer = nullptr;
    SystemController *m_system = nullptr;
    DataLogger *m_logger = nullptr;
    bool m_engineerMode = false;
};

#endif // MAINWINDOW_H
