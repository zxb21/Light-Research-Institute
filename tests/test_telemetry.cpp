#include "TelemetryStore.h"

#include <QtTest>

class TelemetryStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void tongYiShiKeFuGaiWanZhengYaoCeDian();
};

void TelemetryStoreTest::tongYiShiKeFuGaiWanZhengYaoCeDian()
{
    TelemetryStore store;
    store.appendPoint(1000.0, 100.0, 290.0, QStringLiteral("粗抽"));
    store.appendPoint(1000.0, 10.0, 280.0, QStringLiteral("高真空"));

    QCOMPARE(store.size(), 1);
    QCOMPARE(store.at(0).vacuumPa, 10.0);
    QCOMPARE(store.at(0).temperatureK, 280.0);
    QCOMPARE(store.at(0).phase, QStringLiteral("高真空"));
}

QTEST_MAIN(TelemetryStoreTest)

#include "test_telemetry.moc"
