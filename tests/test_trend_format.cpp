#include "TrendChartWidget.h"

#include <QtTest>
#include <QTimeZone>

#include <cmath>

class TrendFormatTest : public QObject
{
    Q_OBJECT

private slots:
    void keXueJiShuFaShiYongShangBiao();
    void ShiJianKeDuShiYongQiangZhongShiJian();
    void DuiShuYaLiYingSheDanDiao();
};

void TrendFormatTest::keXueJiShuFaShiYongShangBiao()
{
    QCOMPARE(TrendChartWidget::formatPressureTick(1.0e-3), QStringLiteral("1×10⁻³"));
    QCOMPARE(TrendChartWidget::formatPressureTick(2.0e5), QStringLiteral("2×10⁵"));
    QCOMPARE(TrendChartWidget::formatPressureTick(5.0), QStringLiteral("5×10⁰"));
    QCOMPARE(TrendChartWidget::formatPressureTick(0.0), QString());
    QCOMPARE(TrendChartWidget::formatPressureTick(-1.0), QString());
}

void TrendFormatTest::ShiJianKeDuShiYongQiangZhongShiJian()
{
    const QDateTime time(QDate(2026, 8, 27), QTime(9, 41, 3), QTimeZone::LocalTime);
    QCOMPARE(TrendChartWidget::formatTimeTick(time.toMSecsSinceEpoch() / 1000.0, 60.0),
             QStringLiteral("09:41:03"));
}

void TrendFormatTest::DuiShuYaLiYingSheDanDiao()
{
    const double low = TrendChartWidget::normalizedLogPressure(1.0e-5, 1.0e-6, 1.0e5);
    const double high = TrendChartWidget::normalizedLogPressure(1.0e3, 1.0e-6, 1.0e5);
    QVERIFY(low >= 0.0);
    QVERIFY(high > low);
    QVERIFY(std::isnan(TrendChartWidget::normalizedLogPressure(0.0, 1.0e-6, 1.0e5)));
}

QTEST_GUILESS_MAIN(TrendFormatTest)

#include "test_trend_format.moc"
