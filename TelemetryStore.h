#ifndef TELEMETRYSTORE_H
#define TELEMETRYSTORE_H

#include <QVector>
#include <QtGlobal>

#include "SystemTypes.h"

// ============================================================================
// TelemetryStore：界面侧温压历史缓存
//   只保存最近一段时间的数据，用于趋势图绘制。
//   时间使用 epoch 秒（double），便于趋势图显示真实墙钟时间。
// ============================================================================
class TelemetryStore
{
public:
    TelemetryStore() = default;

    void appendPoint(const TelemetryPoint &point)
    {
        appendPoint(point.epochSec, point.vacuumPa, point.temperatureK, point.phase);
        if (!m_points.isEmpty() && m_points.constLast().epochSec == point.epochSec)
            m_points.last() = point;
    }

    void appendPoint(double tSec, double vacuumPa, double tempK, const QString &phase)
    {
        if (tSec <= 0.0)
            return;

        const TelemetryPoint point { tSec, vacuumPa, tempK, phase };

        if (!m_time.isEmpty() && qAbs(tSec - m_time.constLast()) < 0.05) {
            m_vacuum.last() = vacuumPa;
            m_temp.last() = tempK;
            m_points.last() = point;
            return;
        }

        if (!m_time.isEmpty() && tSec <= m_time.constLast())
            return;

        m_time.append(tSec);
        m_vacuum.append(vacuumPa);
        m_temp.append(tempK);
        m_points.append(point);

        // 约保留 6 小时（4Hz）；超过时丢弃最早数据。
        constexpr int kMaxPoints = 4 * 3600 * 6;
        while (m_time.size() > kMaxPoints) {
            m_time.removeFirst();
            m_vacuum.removeFirst();
            m_temp.removeFirst();
            m_points.removeFirst();
        }
    }

    void appendSample(double tSec, double vacuumPa, double tempK)
    {
        appendPoint(tSec, vacuumPa, tempK, QString());
    }

    void clear()
    {
        m_time.clear();
        m_vacuum.clear();
        m_temp.clear();
        m_points.clear();
    }

    int size() const { return m_time.size(); }
    double lastTime() const { return m_time.isEmpty() ? 0.0 : m_time.constLast(); }

    const QVector<double> &time() const { return m_time; }
    const QVector<double> &vacuum() const { return m_vacuum; }
    const QVector<double> &temp() const { return m_temp; }
    const TelemetryPoint &at(int index) const { return m_points.at(index); }

private:
    QVector<double> m_time;
    QVector<double> m_vacuum;
    QVector<double> m_temp;
    QVector<TelemetryPoint> m_points;
};

#endif // TELEMETRYSTORE_H
