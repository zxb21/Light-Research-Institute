#include "OperatorComponents.h"
#include "OperatorWidget.h"
#include "TrendChartWidget.h"

#include <QLabel>
#include <QtTest>

class OperatorDetailTest : public QObject
{
    Q_OBJECT

private slots:
    void dianJiMeiGeBuZhouXianShiDuiYingXiangQing();
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

QTEST_MAIN(OperatorDetailTest)

#include "test_operator_detail.moc"
