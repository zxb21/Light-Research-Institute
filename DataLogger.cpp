#include "DataLogger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString csvEscape(const QString &value)
{
    // CSV 中的逗号、双引号和换行必须转义，避免阶段或日志文字破坏列结构。
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n'))) {
        escaped = QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

bool writeHeader(QFile &file, const QByteArray &header)
{
    // 写入 UTF-8 标记，便于 Windows 表格软件直接识别中文列名和内容。
    return file.write("\xEF\xBB\xBF") == 3 && file.write(header) == header.size();
}

} // namespace

DataLogger::DataLogger(QObject *parent)
    : QObject(parent)
{
    // 默认数据目录跟随可执行程序；每 30 秒刷盘一次，退出时还会由 MainWindow 再刷一次。
    m_autoDirectory = QDir(QCoreApplication::applicationDirPath())
                          .filePath(QStringLiteral("data_logs"));

    m_timer = new QTimer(this);
    m_timer->setInterval(30 * 1000);
    connect(m_timer, &QTimer::timeout, this, &DataLogger::flush);
    m_timer->start();
}

void DataLogger::setAutoSaveEnabled(bool enabled)
{
    m_autoSaveEnabled = enabled;
}

void DataLogger::setAutoSaveDirectory(const QString &directory)
{
    // 已创建会话后不允许切换目录，避免一次运行的数据被拆分到两个位置。
    if (!m_runDirectory.isEmpty())
        return;
    m_autoDirectory = directory;
}

QString DataLogger::autoSaveDirectory() const
{
    return m_autoDirectory;
}

QString DataLogger::currentRunDirectory() const
{
    return m_runDirectory;
}

bool DataLogger::directoryHasTelemetry(const QString &directory) const
{
    const QFileInfo telemetry(QDir(directory).filePath(QStringLiteral("telemetry.csv")));
    return telemetry.isFile() && telemetry.size() > 3;
}

bool DataLogger::hasExportableRun() const
{
    const QString directory = m_selectedRunDirectory.isEmpty()
        ? m_runDirectory : m_selectedRunDirectory;
    return directoryHasTelemetry(directory);
}

QVector<RunSummary> DataLogger::runSummaries() const
{
    QVector<RunSummary> summaries;
    const QDir directory(m_autoDirectory);
    const QFileInfoList entries = directory.entryInfoList({ QStringLiteral("run_*") },
                                                          QDir::Dirs | QDir::NoDotAndDotDot,
                                                          QDir::Time);
    for (const QFileInfo &entry : entries)
        summaries.append(readSummary(entry.absoluteFilePath()));
    return summaries;
}

void DataLogger::selectRunDirectory(const QString &directory)
{
    if (directoryHasTelemetry(directory)) {
        m_selectedRunDirectory = directory;
        emit exportAvailabilityChanged(true);
    }
}

bool DataLogger::ensureAutoSession()
{
    // 只有真正出现数据或事件时才建目录，空启动不会产生无意义运行记录。
    if (!m_autoSaveEnabled)
        return false;
    if (!m_runDirectory.isEmpty())
        return true;
    if (!QDir().mkpath(m_autoDirectory))
        return false;

    // 一次运行对应一个时间戳目录，四类文件在同一目录中便于整体导出与追溯。
    m_runDirectory = QDir(m_autoDirectory).filePath(
        QStringLiteral("run_%1").arg(makeTimestamp()));
    if (!QDir().mkpath(m_runDirectory)) {
        m_runDirectory.clear();
        return false;
    }

    m_telemetryFile.setFileName(QDir(m_runDirectory).filePath(QStringLiteral("telemetry.csv")));
    m_eventsFile.setFileName(QDir(m_runDirectory).filePath(QStringLiteral("events.csv")));
    m_diagnosticsFile.setFileName(QDir(m_runDirectory).filePath(QStringLiteral("diagnostics.log")));
    // 三个连续追加文件必须全部打开成功；任一失败就关闭已打开文件并放弃本次会话。
    if (!m_telemetryFile.open(QIODevice::WriteOnly | QIODevice::Append)
        || !m_eventsFile.open(QIODevice::WriteOnly | QIODevice::Append)
        || !m_diagnosticsFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_telemetryFile.close();
        m_eventsFile.close();
        m_diagnosticsFile.close();
        m_runDirectory.clear();
        return false;
    }

    if (!writeHeader(m_telemetryFile, "time,temperature_k,vacuum_pa,phase\n")
        || !writeHeader(m_eventsFile, "time,severity,message\n")) {
        m_telemetryFile.close();
        m_eventsFile.close();
        m_diagnosticsFile.close();
        m_runDirectory.clear();
        return false;
    }

    // 元数据一次性安全提交，避免程序中断时产生半写入的 JSON 文件。
    QSaveFile metadata(QDir(m_runDirectory).filePath(QStringLiteral("metadata.json")));
    if (metadata.open(QIODevice::WriteOnly)) {
        const QJsonObject object {
            { QStringLiteral("system"), QStringLiteral("超低振动深低温恒温器") },
            { QStringLiteral("startedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs) },
            { QStringLiteral("temperatureUnit"), QStringLiteral("K") },
            { QStringLiteral("vacuumUnit"), QStringLiteral("Pa") }
        };
        metadata.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        metadata.commit();
    }
    m_activeSummary.directory = m_runDirectory;
    return true;
}

void DataLogger::startRun()
{
    // 上一轮结束后关闭旧文件并清空目录，下一轮绝不追加到旧记录。
    m_telemetryFile.close();
    m_eventsFile.close();
    m_diagnosticsFile.close();
    m_runDirectory.clear();
    m_runActive = true;
    m_hasSample = false;
    m_selectedRunDirectory.clear();
    m_activeSummary = RunSummary();
    m_activeSummary.startedAt = QDateTime::currentDateTime();
}

void DataLogger::writeMetadata() const
{
    if (m_runDirectory.isEmpty())
        return;
    QSaveFile metadata(QDir(m_runDirectory).filePath(QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::WriteOnly))
        return;
    QJsonObject object {
        { QStringLiteral("system"), QStringLiteral("超低振动深低温恒温器") },
        { QStringLiteral("startedAt"), m_activeSummary.startedAt.toString(Qt::ISODateWithMs) },
        { QStringLiteral("endedAt"), m_activeSummary.endedAt.toString(Qt::ISODateWithMs) },
        { QStringLiteral("result"), m_activeSummary.result },
        { QStringLiteral("lastPhase"), m_activeSummary.lastPhase },
        { QStringLiteral("alarmCount"), m_activeSummary.alarmCount },
        { QStringLiteral("temperatureUnit"), QStringLiteral("K") },
        { QStringLiteral("vacuumUnit"), QStringLiteral("Pa") }
    };
    if (std::isfinite(m_activeSummary.lowestTemperatureK))
        object.insert(QStringLiteral("lowestTemperatureK"), m_activeSummary.lowestTemperatureK);
    if (std::isfinite(m_activeSummary.finalVacuumPa))
        object.insert(QStringLiteral("finalVacuumPa"), m_activeSummary.finalVacuumPa);
    metadata.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    metadata.commit();
}

RunSummary DataLogger::readSummary(const QString &directory) const
{
    RunSummary summary;
    summary.directory = directory;
    QFile metadata(QDir(directory).filePath(QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::ReadOnly))
        return summary;
    const QJsonObject object = QJsonDocument::fromJson(metadata.readAll()).object();
    summary.startedAt = QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
    summary.endedAt = QDateTime::fromString(object.value(QStringLiteral("endedAt")).toString(), Qt::ISODateWithMs);
    summary.result = object.value(QStringLiteral("result")).toString();
    summary.lastPhase = object.value(QStringLiteral("lastPhase")).toString();
    summary.lowestTemperatureK = object.value(QStringLiteral("lowestTemperatureK")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    summary.finalVacuumPa = object.value(QStringLiteral("finalVacuumPa")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    summary.alarmCount = object.value(QStringLiteral("alarmCount")).toInt();
    return summary;
}

void DataLogger::onSampleReady(double epochSec, double vacuumPa, double tempK,
                               const QString &phase)
{
    // 每条遥测写入时间、温度、压强和阶段；这是文本导出的主要数据来源。
    if (m_lifecycleSeen && !m_runActive)
        return;
    if (!ensureAutoSession())
        return;

    m_hasSample = true;
    if (m_runActive) {
        m_activeSummary.directory = m_runDirectory;
        m_activeSummary.lastPhase = phase;
        m_activeSummary.finalVacuumPa = vacuumPa;
        if (!std::isfinite(m_activeSummary.lowestTemperatureK)
            || tempK < m_activeSummary.lowestTemperatureK) {
            m_activeSummary.lowestTemperatureK = tempK;
        }
    }

    const QDateTime time = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(epochSec * 1000.0));
    const QString row = QStringLiteral("%1,%2,%3,%4\n")
                            .arg(time.toString(Qt::ISODateWithMs),
                                 QString::number(tempK, 'f', 3),
                                 QString::number(vacuumPa, 'f', 6),
                                 csvEscape(phase));
    m_telemetryFile.write(row.toUtf8());
    // 立即落盘后再判断可导出状态，避免缓冲区尚未刷新时误判为空记录。
    m_telemetryFile.flush();
    emit exportAvailabilityChanged(hasExportableRun());
}

void DataLogger::appendEvent(const QString &severity, const QString &message)
{
    // 事件与遥测分文件保存，便于既按时间画趋势，也按事件定位操作。
    if (!ensureAutoSession())
        return;
    const QString row = QStringLiteral("%1,%2,%3\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                 csvEscape(severity), csvEscape(message));
    m_eventsFile.write(row.toUtf8());
}

void DataLogger::appendLog(const QString &message)
{
    // 已接入流程状态后，只有实际运行中的消息才允许创建记录目录。
    if (m_lifecycleSeen && !m_runActive)
        return;
    appendEvent(QStringLiteral("信息"), message);
    if (!m_diagnosticsFile.isOpen())
        return;
    const QString line = QStringLiteral("%1 %2\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message);
    m_diagnosticsFile.write(line.toUtf8());
}

void DataLogger::onProcessSnapshot(const ProcessSnapshot &snapshot)
{
    m_lifecycleSeen = true;
    if (snapshot.state == SystemState::Preparing && !m_runActive)
        startRun();
    if (m_runActive && snapshot.state == SystemState::Fault) {
        finishRun(QStringLiteral("故障"), snapshot);
    } else if (m_runActive && snapshot.state == SystemState::Idle
               && m_lastSnapshot.state != SystemState::Idle) {
        const bool interrupted = std::any_of(m_lastSnapshot.steps.cbegin(), m_lastSnapshot.steps.cend(),
                                             [](StepState state) { return state == StepState::Interrupted; });
        finishRun(interrupted ? QStringLiteral("正常停止") : QStringLiteral("完成"), snapshot);
    }
    m_lastSnapshot = snapshot;
}

void DataLogger::onAlarmChanged(const AlarmStatus &alarm)
{
    if (m_runActive && alarm.active)
        m_activeSummary.alarmCount = std::max(m_activeSummary.alarmCount, alarm.count);
}

void DataLogger::finishRun(const QString &result, const ProcessSnapshot &snapshot)
{
    if (!m_runActive)
        return;
    m_runActive = false;
    m_activeSummary.endedAt = QDateTime::currentDateTime();
    m_activeSummary.result = result;
    m_activeSummary.lastPhase = snapshot.stateText;
    if (m_hasSample && !m_runDirectory.isEmpty()) {
        writeMetadata();
        flush();
        emit runFinished(m_activeSummary);
        emit exportAvailabilityChanged(true);
    }
}

bool DataLogger::copyFile(const QString &sourcePath, const QString &targetPath)
{
    // 用 QSaveFile 输出副本，避免导出目标被意外中断后留下损坏文件。
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly))
        return false;
    target.write(source.readAll());
    return target.commit();
}

void DataLogger::exportInteractive()
{
    // 选择遥测文件名后，同时导出同批次的事件、元数据和诊断文件。
    const QString directory = m_selectedRunDirectory.isEmpty()
        ? m_runDirectory : m_selectedRunDirectory;
    if (!directoryHasTelemetry(directory))
        return;
    flush();
    const QString suggested = QDir(m_autoDirectory).filePath(
        QStringLiteral("export_%1.csv").arg(makeTimestamp()));
    const QString telemetryPath = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("导出本次运行数据"), suggested, QStringLiteral("CSV 文件 (*.csv)"));
    if (telemetryPath.isEmpty())
        return;

    copyFile(QDir(directory).filePath(QStringLiteral("telemetry.csv")), telemetryPath);
    const QFileInfo info(telemetryPath);
    const QString base = QDir(info.absolutePath()).filePath(info.completeBaseName());
    copyFile(QDir(directory).filePath(QStringLiteral("events.csv")), base + QStringLiteral("_events.csv"));
    copyFile(QDir(directory).filePath(QStringLiteral("metadata.json")),
             base + QStringLiteral("_metadata.json"));
    copyFile(QDir(directory).filePath(QStringLiteral("diagnostics.log")), base + QStringLiteral("_diagnostics.log"));
}

void DataLogger::flush()
{
    // 只刷新已打开文件，未开始运行时调用也是安全的。
    if (m_telemetryFile.isOpen())
        m_telemetryFile.flush();
    if (m_eventsFile.isOpen())
        m_eventsFile.flush();
    if (m_diagnosticsFile.isOpen())
        m_diagnosticsFile.flush();
}

QString DataLogger::makeTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
}
