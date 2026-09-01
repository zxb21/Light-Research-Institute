#include "DataLogger.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DataLoggerTest : public QObject
{
    Q_OBJECT

private slots:
    void lianXuYaoCeZhiJieZhuiJiaDaoYunXingMuLu();
};

void DataLoggerTest::lianXuYaoCeZhiJieZhuiJiaDaoYunXingMuLu()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    DataLogger logger;
    logger.setAutoSaveDirectory(temporaryDirectory.path());
    logger.onSampleReady(1700000000.0, 100.0, 290.0, QStringLiteral("粗抽"));
    logger.onSampleReady(1700000000.25, 10.0, 280.0, QStringLiteral("高真空"));
    logger.flush();

    const QString runDirectory = logger.currentRunDirectory();
    QVERIFY(!runDirectory.isEmpty());
    QVERIFY(QFile::exists(runDirectory + QStringLiteral("/telemetry.csv")));
    QVERIFY(QFile::exists(runDirectory + QStringLiteral("/events.csv")));
    QVERIFY(QFile::exists(runDirectory + QStringLiteral("/metadata.json")));

    QFile telemetry(runDirectory + QStringLiteral("/telemetry.csv"));
    QVERIFY(telemetry.open(QIODevice::ReadOnly));
    const QByteArray contents = telemetry.readAll();
    QCOMPARE(contents.count('\n'), 3);
    QVERIFY(contents.contains("100.000000"));
    QVERIFY(contents.contains("10.000000"));
}

QTEST_GUILESS_MAIN(DataLoggerTest)

#include "test_data_logger.moc"
