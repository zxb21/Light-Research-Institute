#include "MainWindow.h"
#include "SystemController.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void queRenBaoJingBuFaChuFuWeiHuoQiDongTingZhiQingQiu();
};

void MainWindowTest::queRenBaoJingBuFaChuFuWeiHuoQiDongTingZhiQingQiu()
{
    MainWindow window;
    QTest::qWait(20);
    auto *system = window.findChild<SystemController *>();
    QVERIFY(system);
    QSignalSpy snapshots(system, &SystemController::processSnapshotChanged);

    AlarmStatus alarm;
    alarm.active = true;
    alarm.count = 1;
    alarm.text = QStringLiteral("真空保护报警");
    alarm.occurredAt = QDateTime::currentDateTime();
    QVERIFY(QMetaObject::invokeMethod(&window, "onAlarmChanged", Qt::DirectConnection,
                                      Q_ARG(AlarmStatus, alarm)));
    QVERIFY(QMetaObject::invokeMethod(&window, "acknowledgeCurrentAlarm", Qt::DirectConnection));

    QCOMPARE(snapshots.count(), 0);
    auto *headerAlarm = window.findChild<QPushButton *>(QStringLiteral("HeaderAlarm"));
    QVERIFY(headerAlarm);
    QVERIFY(headerAlarm->text().contains(QStringLiteral("已确认")));
}

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
