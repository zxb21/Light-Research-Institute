#include "OperatorComponents.h"
#include "OperatorWidget.h"
#include "TrendChartWidget.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QtTest>

class OperatorDetailTest : public QObject
{
    Q_OBJECT

private slots:
    void dianJiMeiGeBuZhouXianShiDuiYingXiangQing();
    void jiangWenSuLvDanCiDiaoJieWeiLingDianWu();
    void zongLanYunXingJiLuBaoLiuLiShiXiaoXi();
};

void OperatorDetailTest::dianJiMeiGeBuZhouXianShiDuiYingXiangQing()
{
    OperatorWidget widget;
    widget.resize(1280, 800);

    auto *flow = widget.findChild<ProcessFlowWidget *>();
    auto *chart = widget.findChild<TrendChartWidget *>();
    QVERIFY(flow);
    QVERIFY(chart);

    qRegisterMetaType<ProcessStep>();
    QLabel *title = nullptr;
    for (QLabel *label : widget.findChildren<QLabel *>()) {
        if (label->text().startsWith(QStringLiteral("流程详情："))) {
            title = label;
            break;
        }
    }
    QVERIFY(title);

    struct DetailCase {
        ProcessStep step;
        QString name;
        QString trendName;
    };
    const DetailCase cases[] = {
        { ProcessStep::Preparation, QStringLiteral("系统准备"), QStringLiteral("步骤趋势：样品温度与腔体真空度") },
        { ProcessStep::RoughVacuum, QStringLiteral("粗抽真空（干泵）"), QStringLiteral("步骤趋势：腔体真空度") },
        { ProcessStep::HighVacuum, QStringLiteral("高真空建立（分子泵）"), QStringLiteral("步骤趋势：腔体真空度") },
        { ProcessStep::PreCooling, QStringLiteral("预冷"), QStringLiteral("步骤趋势：样品温度") },
        { ProcessStep::DeepCooling, QStringLiteral("深冷至目标温度"), QStringLiteral("步骤趋势：样品温度") },
        { ProcessStep::StabilizeAndRun, QStringLiteral("恒温与实验运行"), QStringLiteral("步骤趋势：样品温度与腔体真空度") },
        { ProcessStep::VentAndRecover, QStringLiteral("放空与恢复"), QStringLiteral("步骤趋势：样品温度与腔体真空度") }
    };
    for (const DetailCase &detail : cases) {
        QVERIFY(QMetaObject::invokeMethod(flow, "stepActivated", Qt::DirectConnection,
                                          Q_ARG(ProcessStep, detail.step)));
        QCOMPARE(title->text(), QStringLiteral("流程详情：%1").arg(detail.name));
        QCOMPARE(chart->accessibleName(), detail.trendName);
    }
}

void OperatorDetailTest::jiangWenSuLvDanCiDiaoJieWeiLingDianWu()
{
    OperatorWidget widget;
    auto *coolingRate = widget.findChild<QDoubleSpinBox *>();
    QVERIFY(coolingRate);

    coolingRate->setValue(1.0);
    QTest::keyClick(coolingRate, Qt::Key_Up);
    QCOMPARE(coolingRate->value(), 1.5);
}

void OperatorDetailTest::zongLanYunXingJiLuBaoLiuLiShiXiaoXi()
{
    OperatorWidget widget;
    const QList<QListWidget *> eventLists = widget.findChildren<QListWidget *>();
    QVERIFY(eventLists.size() >= 2);
    QListWidget *overviewEvents = nullptr;
    for (QListWidget *eventList : eventLists) {
        if (eventList->count() == 1
            && eventList->item(0)->text() == QStringLiteral("等待自动流程启动")) {
            overviewEvents = eventList;
            break;
        }
    }
    QVERIFY(overviewEvents);

    widget.onLogMessage(QStringLiteral("记录 1"));
    widget.onLogMessage(QStringLiteral("记录 2"));
    widget.onLogMessage(QStringLiteral("记录 3"));
    widget.onLogMessage(QStringLiteral("记录 4"));
    widget.onLogMessage(QStringLiteral("记录 5"));
    widget.onLogMessage(QStringLiteral("记录 6"));

    QCOMPARE(overviewEvents->count(), 6);
    QCOMPARE(overviewEvents->item(0)->text(), QStringLiteral("记录 1"));
    QCOMPARE(overviewEvents->item(5)->text(), QStringLiteral("记录 6"));
}

QTEST_MAIN(OperatorDetailTest)

#include "test_operator_detail.moc"
