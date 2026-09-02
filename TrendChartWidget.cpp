#include "TrendChartWidget.h"

#include "TelemetryStore.h"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kLeft = 76.0;
constexpr double kCombinedRight = 92.0;
constexpr double kSingleRight = 18.0;
constexpr double kTop = 16.0;
constexpr double kBottom = 48.0;

const QColor kBackground(0xff, 0xff, 0xff);
const QColor kGrid(0xe2, 0xe8, 0xf0);
const QColor kAxisText(0x64, 0x74, 0x8b);
const QColor kTempColor(0x16, 0x9b, 0xd5);
const QColor kPressureColor(0xd9, 0x77, 0x06);

} // namespace

TrendChartWidget::TrendChartWidget(Mode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
{
    // 图表不保存样本，只保存对 TelemetryStore 的只读引用。
    setMinimumSize(240, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void TrendChartWidget::setStore(const TelemetryStore *store)
{
    // 数据进入 TelemetryStore 后由 OperatorWidget 调用这里，随后重绘。
    m_store = store;
    update();
}

void TrendChartWidget::setMode(Mode mode)
{
    // 同一控件复用为温度、压强或双曲线，切换步骤时不需要重建控件。
    if (m_mode == mode)
        return;
    m_mode = mode;
    update();
}

QSize TrendChartWidget::minimumSizeHint() const
{
    return QSize(240, 150);
}

QSize TrendChartWidget::sizeHint() const
{
    return QSize(520, 260);
}

double TrendChartWidget::adaptiveSpan(double totalSeconds) const
{
    // 前期窗宽短，便于观察秒级变化；随着总时长增长逐步放大；
    // 达到 10 分钟后固定为滚动窗口，不再继续拉长。
    if (totalSeconds <= 30.0)
        return 30.0;
    if (totalSeconds <= 60.0)
        return 60.0;
    if (totalSeconds <= 180.0)
        return 120.0;
    if (totalSeconds <= 600.0)
        return 240.0;
    if (totalSeconds <= 1800.0)
        return 600.0;
    return 600.0;
}

double TrendChartWidget::niceTimeStep(double spanSeconds)
{
    // 从固定的易读刻度中选取最多约六条网格线的间隔。
    static const double kSteps[] = {
        1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
        60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0
    };
    for (double s : kSteps) {
        if (spanSeconds / s <= 6.0)
            return s;
    }
    return 3600.0;
}

double TrendChartWidget::niceTempStep(double range)
{
    // 温度轴也选择易读整数刻度，避免根据数据范围出现难读的小数间隔。
    static const double kSteps[] = { 5.0, 10.0, 20.0, 25.0, 50.0, 100.0 };
    for (double s : kSteps) {
        if (range / s <= 6.0)
            return s;
    }
    return 100.0;
}

QString TrendChartWidget::formatPressureTick(double pa)
{
    // 为兼容已有趋势图调用保留；实际格式规则唯一来源是 formatPressure()。
    return formatPressure(pa);
}

QString TrendChartWidget::formatTimeTick(double epochSeconds, double visibleSpanSeconds)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(epochSeconds * 1000.0));
    return dt.toString(visibleSpanSeconds > 3600.0
                           ? QStringLiteral("HH:mm")
                           : QStringLiteral("HH:mm:ss"));
}

double TrendChartWidget::normalizedLogPressure(double pa, double minPa, double maxPa)
{
    // 对数坐标将跨数量级的压强压缩到 0–1 区间；非法范围返回无效数而非画错位置。
    if (pa <= 0.0 || minPa <= 0.0 || maxPa <= minPa)
        return std::numeric_limits<double>::quiet_NaN();
    return (std::log10(pa) - std::log10(minPa))
        / (std::log10(maxPa) - std::log10(minPa));
}

void TrendChartWidget::paintEvent(QPaintEvent *)
{
    // 自绘完整流程：确定可见窗口 → 计算坐标范围 → 画网格/标注 → 画步骤边界和曲线。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF full = rect();
    p.fillRect(full, kBackground);

    // 双曲线需要右侧压强轴，单曲线只保留紧凑右边距。
    const double rightMargin = m_mode == Mode::Combined ? kCombinedRight : kSingleRight;
    const QRectF plot = full.adjusted(kLeft, kTop, -rightMargin, -kBottom);
    if (plot.width() < 20.0 || plot.height() < 20.0)
        return;

    // ---------- 可见时间范围 ----------
    double total = 0.0;
    double first = 0.0;
    if (m_store && m_store->size() > 0) {
        first = m_store->time().constFirst();
        total = m_store->lastTime();
    } else {
        total = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        first = total - 60.0;
    }

    // 用“首个样本到当前”的时长决定窗宽，才能在启动初期显示秒级变化；
    // X 轴仍使用真实墙钟时间。
    const double elapsed = std::max(0.0, total - first);
    const double span = adaptiveSpan(elapsed);
    const double timeRightMargin = std::max(5.0, span * 0.08);
    const double end = total + timeRightMargin;
    const double start = std::max(first, end - span);

    // ---------- 找可见数据区间 ----------
    int firstVisible = -1;
    int lastVisible = -1;
    if (m_store && m_store->size() > 0) {
        const QVector<double> &times = m_store->time();
        const auto firstIt = std::lower_bound(times.cbegin(), times.cend(), start);
        const auto lastIt = std::upper_bound(times.cbegin(), times.cend(), end);
        firstVisible = static_cast<int>(std::distance(times.cbegin(), firstIt));
        lastVisible = static_cast<int>(std::distance(times.cbegin(), lastIt)) - 1;
    }

    if (firstVisible < 0 || lastVisible < 0 || lastVisible < firstVisible) {
        firstVisible = 0;
        lastVisible = -1;
    }

    // 绘图时往前多取一个点，保证折线从图框左侧连续进入。
    int drawFrom = std::max(0, firstVisible - 1);
    int drawTo = lastVisible;

    // ---------- 温度 Y 范围 ----------
    double tMin = 0.0;
    double tMax = 320.0;
    if (m_store && drawTo >= drawFrom && (m_mode == Mode::Temperature || m_mode == Mode::Combined)) {
        for (int i = drawFrom; i <= drawTo; ++i) {
            const double v = m_store->temp().at(i);
            if (i == drawFrom || v < tMin)
                tMin = v;
            if (i == drawFrom || v > tMax)
                tMax = v;
        }
        const double pad = std::max(5.0, (tMax - tMin) * 0.12);
        tMin = std::max(0.0, tMin - pad);
        tMax = std::min(320.0, tMax + pad);
        if (tMax <= tMin)
            tMax = tMin + 1.0;
    }

    // ---------- 压力 Y 范围（对数） ----------
    double pLogMin = std::log10(1.0e-4);
    double pLogMax = std::log10(1.0e5);
    bool hasPressureData = false;
    if (m_store && drawTo >= drawFrom && (m_mode == Mode::Pressure || m_mode == Mode::Combined)) {
        for (int i = drawFrom; i <= drawTo; ++i) {
            const double v = m_store->vacuum().at(i);
            if (v <= 0.0)
                continue;
            const double l = std::log10(v);
            if (!hasPressureData) {
                pLogMin = l;
                pLogMax = l;
                hasPressureData = true;
            } else {
                pLogMin = std::min(pLogMin, l);
                pLogMax = std::max(pLogMax, l);
            }
        }
        if (hasPressureData) {
            const double pad = 0.22;
            pLogMin = std::max(std::log10(1.0e-6), pLogMin - pad);
            pLogMax = std::min(std::log10(1.0e5), pLogMax + pad);
            if (pLogMax <= pLogMin)
                pLogMax = pLogMin + 0.5;
        }
    }

    // 以下三个局部函数把物理值映射为图框像素坐标。
    auto mapX = [&](double sec) -> double {
        return plot.left() + (sec - start) / (end - start) * plot.width();
    };
    auto mapTempY = [&](double k) -> double {
        return plot.bottom() - (k - tMin) / (tMax - tMin) * plot.height();
    };
    auto mapPressureY = [&](double pa) -> double {
        const double l = std::log10(pa);
        return plot.bottom() - (l - pLogMin) / (pLogMax - pLogMin) * plot.height();
    };

    // ---------- 网格 ----------
    QFont axisFont = font();
    axisFont.setPointSizeF(8.0);
    p.setFont(axisFont);

    // 横向网格：温度（左轴）
    const bool showTempAxis = (m_mode == Mode::Temperature || m_mode == Mode::Combined);
    const bool showPressureAxis = (m_mode == Mode::Pressure || m_mode == Mode::Combined);
    const bool pressureOnLeft = m_mode == Mode::Pressure;

    if (showTempAxis) {
        const double step = niceTempStep(tMax - tMin);
        const double firstTick = std::ceil(tMin / step) * step;
        for (double y = firstTick; y <= tMax + 1e-9; y += step) {
            const double yy = mapTempY(y);
            p.setPen(QPen(kGrid, 1.0));
            p.drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
            p.setPen(kAxisText);
            p.drawText(QRectF(0.0, yy - 9.0, kLeft - 7.0, 18.0),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(y, 'f', 0));
        }
    }

    if (showPressureAxis) {
        int loDecade = static_cast<int>(std::floor(pLogMin));
        int hiDecade = static_cast<int>(std::ceil(pLogMax));
        const int stepDecade = (hiDecade - loDecade) > 7 ? 2 : 1;
        for (int d = loDecade; d <= hiDecade; d += stepDecade) {
            const double pa = std::pow(10.0, d);
            const double yy = mapPressureY(pa);
            p.setPen(QPen(kGrid, 1.0, Qt::DashLine));
            p.drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
            p.setPen(kAxisText);
            const QRectF pressureTickRect = pressureOnLeft
                ? QRectF(0.0, yy - 9.0, kLeft - 7.0, 18.0)
                : QRectF(plot.right() + 7.0, yy - 9.0, kCombinedRight - 10.0, 18.0);
            p.drawText(pressureTickRect,
                       pressureOnLeft ? Qt::AlignRight | Qt::AlignVCenter
                                      : Qt::AlignLeft | Qt::AlignVCenter,
                       formatPressureTick(pa));

            const double decadeHeight = plot.height()
                / std::max(1.0, pLogMax - pLogMin);
            if (stepDecade == 1 && decadeHeight >= 52.0) {
                for (double coefficient : { 2.0, 5.0 }) {
                    const double minorPa = coefficient * pa;
                    const double minorLog = std::log10(minorPa);
                    if (minorLog > pLogMax || minorLog < pLogMin)
                        continue;
                    const double minorY = mapPressureY(minorPa);
                    p.setPen(QPen(QColor(0xec, 0xf0, 0xf4), 1.0, Qt::DotLine));
                    p.drawLine(QPointF(plot.left(), minorY), QPointF(plot.right(), minorY));
                    p.setPen(QColor(0x8a, 0x98, 0xaa));
                    const QRectF minorTickRect = pressureOnLeft
                        ? QRectF(0.0, minorY - 8.0, kLeft - 7.0, 16.0)
                        : QRectF(plot.right() + 7.0, minorY - 8.0,
                                 kCombinedRight - 10.0, 16.0);
                    p.drawText(minorTickRect,
                               pressureOnLeft ? Qt::AlignRight | Qt::AlignVCenter
                                              : Qt::AlignLeft | Qt::AlignVCenter,
                               formatPressureTick(minorPa));
                }
            }
        }
    }

    // 纵向网格：时间轴
    const double timeStep = niceTimeStep(end - start);
    const double firstTime = std::ceil(start / timeStep) * timeStep;
    for (double xv = firstTime; xv <= end + 1e-9; xv += timeStep) {
        const double xx = mapX(xv);
        p.setPen(QPen(kGrid, 1.0));
        p.drawLine(QPointF(xx, plot.top()), QPointF(xx, plot.bottom()));
        p.setPen(kAxisText);
        p.drawText(QRectF(xx - 44.0, plot.bottom() + 7.0, 88.0, 16.0),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   formatTimeTick(xv, end - start));
    }

    // 图框
    p.setPen(QPen(QColor(0xcb, 0xd5, 0xe1), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // 坐标轴名称和单位固定占位，避免缩放后被刻度文字挤出。
    QFont titleFont = font();
    titleFont.setPixelSize(12);
    titleFont.setWeight(QFont::DemiBold);
    p.setFont(titleFont);
    p.setPen(QColor(0x4f, 0x60, 0x76));
    if (showTempAxis) {
        p.save();
        p.translate(15.0, plot.center().y());
        p.rotate(-90.0);
        p.drawText(QRectF(-70.0, -10.0, 140.0, 20.0),
                   Qt::AlignCenter, QStringLiteral("温度 / K"));
        p.restore();
    }
    if (showPressureAxis && !pressureOnLeft) {
        p.save();
        p.translate(width() - 14.0, plot.center().y());
        p.rotate(90.0);
        p.drawText(QRectF(-70.0, -10.0, 140.0, 20.0),
                   Qt::AlignCenter, QStringLiteral("压力 / Pa"));
        p.restore();
    }
    if (pressureOnLeft) {
        p.save();
        p.translate(15.0, plot.center().y());
        p.rotate(-90.0);
        p.drawText(QRectF(-70.0, -10.0, 140.0, 20.0),
                   Qt::AlignCenter, QStringLiteral("压力 / Pa"));
        p.restore();
    }
    p.drawText(QRectF(plot.left(), plot.bottom() + 28.0, plot.width(), 18.0),
               Qt::AlignCenter, QStringLiteral("时间 / HH:mm:ss"));

    // 步骤切换边界来自强类型遥测，不解析中文阶段文字。
    if (m_store && drawTo > drawFrom) {
        ProcessStep previous = m_store->at(drawFrom).step;
        for (int i = drawFrom + 1; i <= drawTo; ++i) {
            const ProcessStep step = m_store->at(i).step;
            if (step == previous)
                continue;
            const double x = mapX(m_store->time().at(i));
            p.setPen(QPen(QColor(0x7f, 0xaa, 0xdc, 155), 1.0, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(QPen(QColor(0x7f, 0xaa, 0xdc), 1.0));
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x, plot.top() + 10.0), 9.0, 9.0);
            p.setPen(QColor(0x1f, 0x6f, 0xd1));
            p.drawText(QRectF(x - 9.0, plot.top() + 1.0, 18.0, 18.0),
                       Qt::AlignCenter, QString::number(static_cast<int>(step) + 1));
            previous = step;
        }
    }

    // ---------- 曲线 ----------
    // 通用画线过程：必要时对数据抽样，填充半透明面积，并标记最后一个读数。
    auto drawCurve = [&](const QVector<double> &values, bool logarithmic,
                         const QColor &color) {
        if (drawTo < drawFrom || !m_store)
            return;

        QPainterPath line;
        bool started = false;
        QPointF lastPoint;
        // 数据点远多于像素时抽样，减少无意义的重复绘制而保持趋势形状。
        const int pointStep = std::max(1, (drawTo - drawFrom + 1)
                                           / std::max(1, static_cast<int>(plot.width() * 2.0)));
        for (int i = drawFrom; i <= drawTo; i += pointStep) {
            const double v = values.at(i);
            if (logarithmic && v <= 0.0)
                continue;
            const double x = mapX(m_store->time().at(i));
            const double y = logarithmic ? mapPressureY(v) : mapTempY(v);
            lastPoint = QPointF(x, y);
            if (!started) {
                line.moveTo(lastPoint);
                started = true;
            } else {
                line.lineTo(lastPoint);
            }
        }

        if (!started)
            return;

        QPainterPath area = line;
        area.lineTo(lastPoint.x(), plot.bottom());
        area.lineTo(line.elementAt(0).x, plot.bottom());
        area.closeSubpath();
        p.fillPath(area, QColor(color.red(), color.green(), color.blue(), 24));

        p.setPen(QPen(color, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);

        p.setPen(QPen(kBackground, 1.2));
        p.setBrush(color);
        p.drawEllipse(lastPoint, 3.2, 3.2);

        const double lastValue = values.at(drawTo);
        const QString currentText = logarithmic
            ? formatPressureTick(lastValue) + QStringLiteral(" Pa")
            : QStringLiteral("%1 K").arg(lastValue, 0, 'f', 1);
        const QRectF valueRect(lastPoint.x() + 7.0,
                               std::clamp(lastPoint.y() - 11.0,
                                          plot.top() + 2.0, plot.bottom() - 23.0),
                               logarithmic ? 82.0 : 54.0, 22.0);
        p.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 90), 1.0));
        p.setBrush(QColor(0xff, 0xff, 0xff, 235));
        p.drawRoundedRect(valueRect, 4.0, 4.0);
        p.setPen(color);
        p.drawText(valueRect.adjusted(4, 0, -4, 0),
                   Qt::AlignLeft | Qt::AlignVCenter, currentText);
    };

    if (showTempAxis && m_store)
        drawCurve(m_store->temp(), false, kTempColor);
    if (showPressureAxis && m_store)
        drawCurve(m_store->vacuum(), true, kPressureColor);

    if (!m_store || m_store->size() == 0) {
        p.setPen(kAxisText);
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("等待数据…"));
    }
}
