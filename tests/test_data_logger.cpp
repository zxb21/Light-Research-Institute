#include "DataLogger.h"

#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

class DataLoggerTest : public QObject
{
    Q_OBJECT

private slots:
    void lianXuYaoCeZhiJieZhuiJiaDaoYunXingMuLu();
    void ziDongYunXingJieShuZhiShengChengYiTiaoJieGuo();
    void guZhangHeWanChengXieRuBuTongJieGuo();
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

void DataLoggerTest::ziDongYunXingJieShuZhiShengChengYiTiaoJieGuo()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    DataLogger logger;
    logger.setAutoSaveDirectory(temporaryDirectory.path());
    QVERIFY(!logger.hasExportableRun());

    ProcessSnapshot preparing;
    preparing.state = SystemState::Preparing;
    logger.onProcessSnapshot(preparing);
    QVERIFY(!logger.hasExportableRun());
    logger.onSampleReady(1700000000.0, 10.0, 290.0, QStringLiteral("系统准备中"));
    QVERIFY(logger.hasExportableRun());

    ProcessSnapshot stopping;
    stopping.state = SystemState::Stopping;
    stopping.steps[0] = StepState::Interrupted;
    logger.onProcessSnapshot(stopping);
    ProcessSnapshot idle;
    idle.state = SystemState::Idle;
    idle.stateText = QStringLiteral("待机");
    logger.onProcessSnapshot(idle);
    logger.onProcessSnapshot(idle);

    const QVector<RunSummary> summaries = logger.runSummaries();
    QCOMPARE(summaries.size(), 1);
    QCOMPARE(summaries.constFirst().result, QStringLiteral("正常停止"));
}

void DataLoggerTest::guZhangHeWanChengXieRuBuTongJieGuo()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    DataLogger logger;
    logger.setAutoSaveDirectory(temporaryDirectory.path());
    ProcessSnapshot preparing;
    preparing.state = SystemState::Preparing;
    logger.onProcessSnapshot(preparing);
    logger.onSampleReady(1700000000.0, 10.0, 290.0, QStringLiteral("系统准备中"));

    ProcessSnapshot fault;
    fault.state = SystemState::Fault;
    fault.stateText = QStringLiteral("故障待处理");
    logger.onProcessSnapshot(fault);

    ProcessSnapshot preparingAgain;
    preparingAgain.state = SystemState::Preparing;
    logger.onProcessSnapshot(preparingAgain);
    logger.onSampleReady(1700000010.0, 1.0, 280.0, QStringLiteral("运行完成"));
    ProcessSnapshot idle;
    idle.state = SystemState::Idle;
    idle.stateText = QStringLiteral("待机");
    logger.onProcessSnapshot(idle);

    const QVector<RunSummary> summaries = logger.runSummaries();
    QCOMPARE(summaries.size(), 2);
    QSet<QString> results;
    for (const RunSummary &summary : summaries)
        results.insert(summary.result);
    QCOMPARE(results, QSet<QString>({ QStringLiteral("完成"), QStringLiteral("故障") }));
}

QTEST_GUILESS_MAIN(DataLoggerTest)

#include "test_data_logger.moc"
