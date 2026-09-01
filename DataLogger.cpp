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

namespace {

QString csvEscape(const QString &value)
{
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
    return file.write("\xEF\xBB\xBF") == 3 && file.write(header) == header.size();
}

} // namespace

DataLogger::DataLogger(QObject *parent)
    : QObject(parent)
{
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

bool DataLogger::ensureAutoSession()
{
    if (!m_autoSaveEnabled)
        return false;
    if (!m_runDirectory.isEmpty())
        return true;
    if (!QDir().mkpath(m_autoDirectory))
        return false;

    m_runDirectory = QDir(m_autoDirectory).filePath(
        QStringLiteral("run_%1").arg(makeTimestamp()));
    if (!QDir().mkpath(m_runDirectory)) {
        m_runDirectory.clear();
        return false;
    }

    m_telemetryFile.setFileName(QDir(m_runDirectory).filePath(QStringLiteral("telemetry.csv")));
    m_eventsFile.setFileName(QDir(m_runDirectory).filePath(QStringLiteral("events.csv")));
    m_diagnosticsFile.setFileName(QDir(m_runDirectory).filePath(QStringLiteral("diagnostics.log")));
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
    return true;
}

void DataLogger::onSampleReady(double epochSec, double vacuumPa, double tempK,
                               const QString &phase)
{
    if (!ensureAutoSession())
        return;

    const QDateTime time = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(epochSec * 1000.0));
    const QString row = QStringLiteral("%1,%2,%3,%4\n")
                            .arg(time.toString(Qt::ISODateWithMs),
                                 QString::number(tempK, 'f', 3),
                                 QString::number(vacuumPa, 'f', 6),
                                 csvEscape(phase));
    m_telemetryFile.write(row.toUtf8());
}

void DataLogger::appendEvent(const QString &severity, const QString &message)
{
    if (!ensureAutoSession())
        return;
    const QString row = QStringLiteral("%1,%2,%3\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                 csvEscape(severity), csvEscape(message));
    m_eventsFile.write(row.toUtf8());
}

void DataLogger::appendLog(const QString &message)
{
    appendEvent(QStringLiteral("信息"), message);
    if (!m_diagnosticsFile.isOpen())
        return;
    const QString line = QStringLiteral("%1 %2\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message);
    m_diagnosticsFile.write(line.toUtf8());
}

bool DataLogger::copyFile(const QString &sourcePath, const QString &targetPath)
{
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
    if (m_runDirectory.isEmpty())
        return;
    flush();
    const QString suggested = QDir(m_autoDirectory).filePath(
        QStringLiteral("export_%1.csv").arg(makeTimestamp()));
    const QString telemetryPath = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("导出本次运行数据"), suggested, QStringLiteral("CSV 文件 (*.csv)"));
    if (telemetryPath.isEmpty())
        return;

    copyFile(m_telemetryFile.fileName(), telemetryPath);
    const QFileInfo info(telemetryPath);
    const QString base = QDir(info.absolutePath()).filePath(info.completeBaseName());
    copyFile(m_eventsFile.fileName(), base + QStringLiteral("_events.csv"));
    copyFile(QDir(m_runDirectory).filePath(QStringLiteral("metadata.json")),
             base + QStringLiteral("_metadata.json"));
    copyFile(m_diagnosticsFile.fileName(), base + QStringLiteral("_diagnostics.log"));
}

void DataLogger::flush()
{
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
