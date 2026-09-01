#include "OperatorComponents.h"

#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {

const QStringList kStepNames = {
    QStringLiteral("系统准备"),
    QStringLiteral("粗抽真空（干泵）"),
    QStringLiteral("高真空建立（分子泵）"),
    QStringLiteral("预冷"),
    QStringLiteral("深冷至目标温度"),
    QStringLiteral("恒温与实验运行"),
    QStringLiteral("放空与恢复")
};

const QStringList kDeviceNames = {
    QStringLiteral("干泵"), QStringLiteral("分子泵"), QStringLiteral("冷水机"),
    QStringLiteral("真空计"), QStringLiteral("温控仪"), QStringLiteral("压缩机"),
    QStringLiteral("真空电磁阀")
};

const QStringList kDeviceIcons = {
    QStringLiteral(":/cryo/resources/icons/dry-pump.svg"),
    QStringLiteral(":/cryo/resources/icons/molecular-pump.svg"),
    QStringLiteral(":/cryo/resources/icons/chiller.svg"),
    QStringLiteral(":/cryo/resources/icons/vacuum-gauge.svg"),
    QStringLiteral(":/cryo/resources/icons/temperature-controller.svg"),
    QStringLiteral(":/cryo/resources/icons/compressor.svg"),
    QStringLiteral(":/cryo/resources/icons/valve.svg")
};

QColor stepColor(StepState state)
{
    switch (state) {
    case StepState::Completed: return QColor(QStringLiteral("#16A36A"));
    case StepState::Active: return QColor(QStringLiteral("#1F6FD1"));
    case StepState::Interrupted: return QColor(QStringLiteral("#D88710"));
    case StepState::Faulted: return QColor(QStringLiteral("#D83A3A"));
    case StepState::Recovering: return QColor(QStringLiteral("#0F8F8A"));
    case StepState::Pending: return QColor(QStringLiteral("#98A5B7"));
    }
    return QColor(QStringLiteral("#98A5B7"));
}

QString stepStateText(StepState state)
{
    switch (state) {
    case StepState::Completed: return QStringLiteral("已完成");
    case StepState::Active: return QStringLiteral("进行中");
    case StepState::Interrupted: return QStringLiteral("已中止");
    case StepState::Faulted: return QStringLiteral("故障");
    case StepState::Recovering: return QStringLiteral("恢复中");
    case StepState::Pending: return QStringLiteral("等待");
    }
    return QString();
}

QColor healthColor(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Normal:
    case DeviceHealth::Running: return QColor(QStringLiteral("#16A36A"));
    case DeviceHealth::Standby: return QColor(QStringLiteral("#7B8A9E"));
    case DeviceHealth::Warning: return QColor(QStringLiteral("#D88710"));
    case DeviceHealth::Fault: return QColor(QStringLiteral("#D83A3A"));
    case DeviceHealth::Offline: return QColor(QStringLiteral("#7B8797"));
    }
    return QColor(QStringLiteral("#7B8797"));
}

QString healthText(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Normal: return QStringLiteral("正常");
    case DeviceHealth::Running: return QStringLiteral("运行中");
    case DeviceHealth::Standby: return QStringLiteral("待机");
    case DeviceHealth::Warning: return QStringLiteral("警告");
    case DeviceHealth::Fault: return QStringLiteral("故障");
    case DeviceHealth::Offline: return QStringLiteral("离线");
    }
    return QString();
}

} // namespace

ProcessFlowWidget::ProcessFlowWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(360);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("七步自动运行流程"));
    setToolTip(QStringLiteral("单击任一步骤查看该步骤的工作详情、趋势和设备事件记录"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void ProcessFlowWidget::setSnapshot(const ProcessSnapshot &snapshot)
{
    m_snapshot = snapshot;
    update();
}

void ProcessFlowWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRectF panel = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor(QStringLiteral("#D9E1EA"))));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(panel, 8.0, 8.0);

    painter.setPen(QColor(QStringLiteral("#66758A")));
    QFont small = font();
    small.setPixelSize(13);
    painter.setFont(small);
    painter.drawText(QRectF(18, 17, width() - 36, 20), QStringLiteral("当前运行"));

    QFont title = font();
    title.setPixelSize(20);
    title.setWeight(QFont::DemiBold);
    painter.setFont(title);
    painter.setPen(QColor(QStringLiteral("#142033")));
    painter.drawText(QRectF(18, 40, width() - 36, 28),
                     Qt::AlignLeft | Qt::AlignVCenter, m_snapshot.stateText);

    painter.setFont(small);
    painter.setPen(QColor(QStringLiteral("#66758A")));
    painter.drawText(QRectF(18, 72, width() - 36, 38),
                     Qt::TextWordWrap, m_snapshot.detailText);

    painter.drawText(QRectF(18, 116, 80, 20), QStringLiteral("总进度"));
    painter.setPen(QColor(QStringLiteral("#1F6FD1")));
    QFont progressFont = small;
    progressFont.setWeight(QFont::DemiBold);
    painter.setFont(progressFont);
    painter.drawText(QRectF(width() - 78, 116, 60, 20),
                     Qt::AlignRight, QString::number(m_snapshot.progressPercent) + QLatin1Char('%'));
    const QRectF progressTrack(18, 140, width() - 36, 7);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#E7ECF2")));
    painter.drawRoundedRect(progressTrack, 3.5, 3.5);
    painter.setBrush(QColor(QStringLiteral("#1F6FD1")));
    painter.drawRoundedRect(QRectF(progressTrack.left(), progressTrack.top(),
                                  progressTrack.width() * m_snapshot.progressPercent / 100.0,
                                  progressTrack.height()), 3.5, 3.5);

    painter.setFont(small);
    painter.setPen(QColor(QStringLiteral("#66758A")));
    painter.drawText(QRectF(18, 164, width() - 36, 20), QStringLiteral("流程步骤（共 7 步）"));

    const double firstY = 205.0;
    const double rowHeight = std::min(68.0, (height() - firstY - 22.0) / 7.0);
    const double nodeX = 35.0;
    for (int i = 0; i < 7; ++i) {
        const double centerY = firstY + i * rowHeight;
        const StepState state = m_snapshot.steps[static_cast<size_t>(i)];
        const QColor color = stepColor(state);

        if (i < 6) {
            const StepState next = m_snapshot.steps[static_cast<size_t>(i + 1)];
            const bool solid = state == StepState::Completed
                && next != StepState::Pending;
            painter.setPen(QPen(solid ? QColor(QStringLiteral("#16A36A"))
                                     : QColor(QStringLiteral("#D9E1EA")),
                                2.0, solid ? Qt::SolidLine : Qt::DashLine));
            painter.drawLine(QPointF(nodeX, centerY + 11),
                             QPointF(nodeX, centerY + rowHeight - 11));
        }

        if (state == StepState::Active || i == m_selectedStep) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(state == StepState::Active
                                 ? QColor(QStringLiteral("#EAF3FE"))
                                 : QColor(QStringLiteral("#F3F7FC")));
            painter.drawRoundedRect(QRectF(12, centerY - 20, width() - 24, 42), 6, 6);
        }

        if (hasFocus() && i == m_selectedStep) {
            painter.setPen(QPen(QColor(QStringLiteral("#1F6FD1")), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(12, centerY - 20, width() - 24, 42), 6, 6);
        }

        painter.setPen(QPen(color, 2.0));
        painter.setBrush(state == StepState::Pending ? Qt::white : color);
        if (state == StepState::Interrupted)
            painter.drawRect(QRectF(nodeX - 9, centerY - 9, 18, 18));
        else if (state == StepState::Faulted) {
            QPainterPath diamond;
            diamond.moveTo(nodeX, centerY - 10);
            diamond.lineTo(nodeX + 10, centerY);
            diamond.lineTo(nodeX, centerY + 10);
            diamond.lineTo(nodeX - 10, centerY);
            diamond.closeSubpath();
            painter.drawPath(diamond);
        } else {
            painter.drawEllipse(QPointF(nodeX, centerY), 10, 10);
        }

        QFont nodeFont = small;
        nodeFont.setPixelSize(11);
        nodeFont.setWeight(QFont::DemiBold);
        painter.setFont(nodeFont);
        painter.setPen(state == StepState::Pending ? color : Qt::white);
        painter.drawText(QRectF(nodeX - 10, centerY - 10, 20, 20),
                         Qt::AlignCenter,
                         state == StepState::Completed ? QStringLiteral("✓")
                         : state == StepState::Faulted ? QStringLiteral("!")
                         : QString::number(i + 1));

        QFont stepFont = font();
        stepFont.setPixelSize(14);
        stepFont.setWeight(state == StepState::Active ? QFont::DemiBold : QFont::Normal);
        painter.setFont(stepFont);
        painter.setPen(state == StepState::Pending
                           ? QColor(QStringLiteral("#66758A"))
                           : QColor(QStringLiteral("#142033")));
        painter.drawText(QRectF(57, centerY - 17, width() - 132, 34),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         kStepNames.at(i));

        QFont stateFont = small;
        stateFont.setPixelSize(12);
        painter.setFont(stateFont);
        painter.setPen(color);
        painter.drawText(QRectF(width() - 79, centerY - 12, 62, 24),
                         Qt::AlignRight | Qt::AlignVCenter, stepStateText(state));
    }

    const QRectF detailHint(18, height() - 40, width() - 36, 26);
    painter.setPen(QColor(QStringLiteral("#1F6FD1")));
    painter.drawRoundedRect(detailHint, 5, 5);
    painter.drawText(detailHint, Qt::AlignCenter, QStringLiteral("点击任一步骤查看该步骤详情"));
}

void ProcessFlowWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = stepIndexAt(event->position().y());
        if (index >= 0) {
            m_selectedStep = index;
            setFocus(Qt::MouseFocusReason);
            update();
            emit stepActivated(static_cast<ProcessStep>(index));
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void ProcessFlowWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Up) {
        m_selectedStep = std::max(0, m_selectedStep - 1);
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        m_selectedStep = std::min(6, m_selectedStep + 1);
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Space) {
        emit stepActivated(static_cast<ProcessStep>(m_selectedStep));
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

int ProcessFlowWidget::stepIndexAt(int y) const
{
    const double firstY = 205.0;
    const double rowHeight = std::min(68.0, (height() - firstY - 22.0) / 7.0);
    const double top = firstY - rowHeight / 2.0;
    if (y < top || y >= top + rowHeight * 7.0)
        return -1;
    return static_cast<int>((y - top) / rowHeight);
}

DeviceHealthWidget::DeviceHealthWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(88);
    setMaximumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("七设备健康状态"));
    for (int i = 0; i < 7; ++i) {
        m_devices[static_cast<size_t>(i)].index = i;
        m_devices[static_cast<size_t>(i)].name = kDeviceNames.at(i);
        m_devices[static_cast<size_t>(i)].health = DeviceHealth::Offline;
    }
}

void DeviceHealthWidget::setDeviceStatus(const DeviceStatus &status)
{
    if (status.index < 0 || status.index >= 7)
        return;
    m_devices[static_cast<size_t>(status.index)] = status;
    update();
}

void DeviceHealthWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(QPen(QColor(QStringLiteral("#D9E1EA"))));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    const double itemWidth = width() / 7.0;
    for (int i = 0; i < 7; ++i) {
        const DeviceStatus &device = m_devices[static_cast<size_t>(i)];
        const QRectF item(i * itemWidth, 0, itemWidth, height());
        if (i > 0) {
            painter.setPen(QPen(QColor(QStringLiteral("#E5EAF0"))));
            painter.drawLine(QPointF(item.left(), 13), QPointF(item.left(), height() - 13));
        }

        const QColor color = healthColor(device.health);
        QIcon(kDeviceIcons.at(i)).paint(&painter,
                                        QRect(static_cast<int>(item.left() + 12), 30, 26, 26));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(item.left() + 18, 16), 4, 4);

        QFont nameFont = font();
        nameFont.setPixelSize(13);
        nameFont.setWeight(QFont::DemiBold);
        painter.setFont(nameFont);
        painter.setPen(QColor(QStringLiteral("#142033")));
        painter.drawText(QRectF(item.left() + 28, 5, item.width() - 35, 22),
                         Qt::AlignLeft | Qt::AlignVCenter, device.name);

        QFont statusFont = font();
        statusFont.setPixelSize(12);
        painter.setFont(statusFont);
        painter.setPen(color);
        painter.drawText(QRectF(item.left() + 46, 29, item.width() - 52, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, healthText(device.health));
        painter.setPen(QColor(QStringLiteral("#66758A")));
        const QString auxiliary = device.keyValue.isEmpty()
            ? (device.updatedAt.isValid()
                   ? device.updatedAt.toString(QStringLiteral("HH:mm"))
                   : QStringLiteral("--:--"))
            : device.keyValue;
        painter.drawText(QRectF(item.left() + 46, 48, item.width() - 52, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, auxiliary);
    }
}
