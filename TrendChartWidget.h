#ifndef TRENDCHARTWIDGET_H
#define TRENDCHARTWIDGET_H

#include <QWidget>

class TelemetryStore;

// ============================================================================
// TrendChartWidget：自绘实时趋势图
//   Temperature  : 单轴显示样品温度
//   Pressure     : 对数轴显示腔体真空度
//   Combined     : 左轴温度 + 右轴对数压力
//   时间轴采用“自适应窗宽 + 到达上限后滚动截取”，避免早期大量空白，
//   也避免长时间后曲线被压缩到看不见。
// ============================================================================
class TrendChartWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        Temperature,
        Pressure,
        Combined
    };

    explicit TrendChartWidget(Mode mode = Mode::Combined, QWidget *parent = nullptr);

    void setStore(const TelemetryStore *store);
    void setMode(Mode mode);
    static QString formatPressureTick(double pa);
    static QString formatTimeTick(double epochSeconds, double visibleSpanSeconds);
    static double normalizedLogPressure(double pa, double minPa, double maxPa);

public:
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double adaptiveSpan(double totalSeconds) const;
    static double niceTimeStep(double spanSeconds);
    static double niceTempStep(double range);

    Mode m_mode = Mode::Combined;
    const TelemetryStore *m_store = nullptr;
};

#endif // TRENDCHARTWIDGET_H
