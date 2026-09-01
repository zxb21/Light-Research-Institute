#ifndef DATALOGGER_H
#define DATALOGGER_H

#include <QObject>
#include <QFile>
#include <QString>

class QTimer;

// ============================================================================
// DataLogger：数据与日志存储接口
//   - 默认自动按时间命名，保存到程序目录下的 data_logs/
//   - 可调用 exportInteractive() 让用户选择导出位置
//   - 真实硬件接入后，只需继续调用 onSampleReady / appendLog 即可
// ============================================================================
class DataLogger : public QObject
{
    Q_OBJECT

public:
    explicit DataLogger(QObject *parent = nullptr);

    void setAutoSaveEnabled(bool enabled);
    void setAutoSaveDirectory(const QString &directory);
    QString autoSaveDirectory() const;
    QString currentRunDirectory() const;

public slots:
    void onSampleReady(double epochSec, double vacuumPa, double tempK,
                       const QString &phase);
    void appendLog(const QString &message);
    void exportInteractive();
    void flush();

private:
    bool ensureAutoSession();
    void appendEvent(const QString &severity, const QString &message);
    static bool copyFile(const QString &sourcePath, const QString &targetPath);
    static QString makeTimestamp();

    QTimer *m_timer = nullptr;
    QString m_autoDirectory;
    QString m_runDirectory;
    QFile m_telemetryFile;
    QFile m_eventsFile;
    QFile m_diagnosticsFile;
    bool m_autoSaveEnabled = true;
};

#endif // DATALOGGER_H
