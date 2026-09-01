#include "OperatorComponents.h"

#include <QtTest>

class ProcessFlowTest : public QObject
{
    Q_OBJECT

private slots:
    void dianJiBuZhouFaChuDuiYingBuZhouXinHao();
};

void ProcessFlowTest::dianJiBuZhouFaChuDuiYingBuZhouXinHao()
{
    ProcessFlowWidget flow;
    flow.resize(360, 720);
    qRegisterMetaType<ProcessStep>();

    QSignalSpy spy(&flow, SIGNAL(stepActivated(ProcessStep)));
    QVERIFY2(spy.isValid(), "流程控件必须提供带步骤编号的点击信号");

    QTest::mouseClick(&flow, Qt::LeftButton, Qt::NoModifier, QPoint(120, 341));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<ProcessStep>(), ProcessStep::HighVacuum);

    QTest::mouseClick(&flow, Qt::LeftButton, Qt::NoModifier, QPoint(120, 60));
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(ProcessFlowTest)

#include "test_process_flow.moc"
