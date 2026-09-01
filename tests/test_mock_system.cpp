#include "MockSystem.h"

#include <QtTest>

class MockSystemTest : public QObject
{
    Q_OBJECT

private slots:
    void ziDongLiuChengJiLuSheBeiQiTing();
};

void MockSystemTest::ziDongLiuChengJiLuSheBeiQiTing()
{
    MockSystem system;
    QSignalSpy logs(&system, &SystemController::logMessage);

    system.requestStart(1.0);
    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        for (const QList<QVariant> &entry : logs) {
            if (entry.constFirst().toString().contains(QStringLiteral("干泵启动")))
                return true;
        }
        return false;
    }(), 2500);

    system.requestStop();
    QTRY_VERIFY_WITH_TIMEOUT([&logs] {
        for (const QList<QVariant> &entry : logs) {
            if (entry.constFirst().toString().contains(QStringLiteral("干泵停止")))
                return true;
        }
        return false;
    }(), 1500);
}

QTEST_GUILESS_MAIN(MockSystemTest)

#include "test_mock_system.moc"
