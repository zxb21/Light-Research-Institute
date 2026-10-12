#include "OperatorComponents.h"

#include <QIcon>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>

#include <algorithm>

namespace {

const QStringList kStepNames = {
    // 左侧流程图的七步名称；顺序必须与 ProcessStep 枚举值一致。
    QStringLiteral("系统准备"),
    QStringLiteral("粗抽真空（干泵）"),
    QStringLiteral("高真空建立（分子泵）"),
    QStringLiteral("预冷"),
    QStringLiteral("深冷至目标温度"),
    QStringLiteral("恒温与实验运行"),
    QStringLiteral("放空与恢复")
};

const QStringList kDeviceNames = {
    // 七设备状态带的固定顺序；必须与 MockSystem::DeviceIndex 一致。
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
    // 流程节点颜色只反映步骤状态，不承担任何安全判断。
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
    // 设备状态灯与文字共用此颜色，修改健康状态配色可从这里开始。
    switch (health) {
    case DeviceHealth::Normal: return QColor(QStringLiteral("#16805C"));
    case DeviceHealth::Running: return QColor(QStringLiteral("#1F6FD1"));
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

FlowCommandButton::FlowCommandButton(const QString &text, int step, Position position,
                                     Symbol symbol, QWidget *parent)
    : QPushButton(text, parent)
{
    // 属性只保存绘制所需的三个稳定值，避免再维护一套与按钮状态重复的数据。
    setProperty("flowStep", step);
    setProperty("flowPosition", static_cast<int>(position));
    setProperty("flowSymbol", static_cast<int>(symbol));
    setCursor(Qt::PointingHandCursor);
}

void FlowCommandButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const Position position = static_cast<Position>(property("flowPosition").toInt());
    const Symbol symbol = static_cast<Symbol>(property("flowSymbol").toInt());
    const bool active = isEnabled();
    const bool pressed = isDown();
    const bool hovered = underMouse();
    QColor fill(QStringLiteral("#EEF2F6"));
    QColor foreground(QStringLiteral("#75849A"));
    if (active) {
        if (position == Position::First)
            fill = QColor(pressed ? QStringLiteral("#134D95")
                                  : hovered ? QStringLiteral("#195FB5")
                                            : QStringLiteral("#1F6FD1"));
        else if (symbol == Symbol::Stop)
            fill = QColor(pressed ? QStringLiteral("#693700")
                                  : hovered ? QStringLiteral("#814300")
                                            : QStringLiteral("#9A5100"));
        else
            fill = QColor(pressed ? QStringLiteral("#084E49")
                                  : hovered ? QStringLiteral("#0B625C")
                                            : QStringLiteral("#0F766E"));
        foreground = Qt::white;
    }

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const QPainterPath shape = shapePath();
    painter.setPen(QPen(QColor(QStringLiteral("#C9D4E1")), 1.0));
    if (active && position == Position::First && !pressed && !hovered) {
        QLinearGradient startFill(r.topLeft(), r.topRight());
        startFill.setColorAt(0.0, QColor(QStringLiteral("#2879D8")));
        startFill.setColorAt(1.0, QColor(QStringLiteral("#1763C5")));
        painter.setBrush(startFill);
    } else {
        painter.setBrush(fill);
    }
    painter.drawPath(shape);

    constexpr qreal cut = 14.0;
    const qreal leftInset = position == Position::First ? 0.0 : 10.0;
    const QPointF stepCenter(leftInset + 21.0, r.center().y());
    const QPointF symbolCenter(leftInset + 57.0, r.center().y());
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(foreground, 1.5));
    painter.drawEllipse(stepCenter, 8.0, 8.0);
    painter.drawEllipse(symbolCenter, 19.0, 19.0);

    QFont stepFont = font();
    stepFont.setPixelSize(11);
    stepFont.setWeight(QFont::DemiBold);
    painter.setFont(stepFont);
    painter.drawText(QRectF(stepCenter.x() - 8, stepCenter.y() - 8, 16, 16),
                     Qt::AlignCenter, QString::number(property("flowStep").toInt()));

    // Segoe MDL2 Assets 是当前 Windows 自带的图标库，避免用文本符号冒充操作图标。
    QFont iconFont(QStringLiteral("Segoe MDL2 Assets"));
    iconFont.setPixelSize(symbol == Symbol::Stop ? 16 : 19);
    painter.setFont(iconFont);
    if (symbol == Symbol::Vent) {
        // 官方图标字体允许图标叠加；云形与向上箭头叠放后对应目标图的“放空”图标。
        painter.drawText(QRectF(symbolCenter.x() - 19, symbolCenter.y() - 22, 38, 34),
                         Qt::AlignCenter, QString(QChar(0xE753)));
        iconFont.setPixelSize(11);
        painter.setFont(iconFont);
        painter.drawText(QRectF(symbolCenter.x() - 19, symbolCenter.y() - 4, 38, 25),
                         Qt::AlignCenter, QString(QChar(0xE74A)));
    } else {
        const QString glyph = symbol == Symbol::Play ? QString(QChar(0xE768))
                                                      : QString(QChar(0xE71A));
        painter.drawText(QRectF(symbolCenter.x() - 19, symbolCenter.y() - 19, 38, 38),
                         Qt::AlignCenter, glyph);
    }

    QFont textFont = font();
    textFont.setPixelSize(14);
    textFont.setWeight(QFont::DemiBold);
    painter.setFont(textFont);
    const qreal textLeft = leftInset + 84.0;
    painter.drawText(QRectF(textLeft, 0, width() - textLeft - cut, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text());

    if (hasFocus()) {
        painter.setPen(QPen(QColor(QStringLiteral("#0B3B70")), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
    }
}

QPainterPath FlowCommandButton::shapePath() const
{
    // 中、末段左边使用与前一段箭头完全相同的折线；配合轨道中的 14 像素重叠，
    // 两个色块会像拼图一样共用边界，不再出现直边或空白三角。
    constexpr qreal cut = 14.0;
    constexpr qreal radius = 7.0;
    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const Position position = static_cast<Position>(property("flowPosition").toInt());
    QPainterPath shape;
    if (position == Position::First) {
        shape.moveTo(r.left() + radius, r.top());
        shape.lineTo(r.right() - cut, r.top());
        shape.lineTo(r.right(), r.center().y());
        shape.lineTo(r.right() - cut, r.bottom());
        shape.lineTo(r.left() + radius, r.bottom());
        shape.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - radius);
        shape.lineTo(r.left(), r.top() + radius);
        shape.quadTo(r.left(), r.top(), r.left() + radius, r.top());
    } else {
        shape.moveTo(r.left(), r.top());
        if (position == Position::Middle) {
            shape.lineTo(r.right() - cut, r.top());
            shape.lineTo(r.right(), r.center().y());
            shape.lineTo(r.right() - cut, r.bottom());
        } else {
            shape.lineTo(r.right() - radius, r.top());
            shape.quadTo(r.right(), r.top(), r.right(), r.top() + radius);
            shape.lineTo(r.right(), r.bottom() - radius);
            shape.quadTo(r.right(), r.bottom(), r.right() - radius, r.bottom());
        }
        shape.lineTo(r.left(), r.bottom());
        shape.lineTo(r.left() + cut, r.center().y());
    }
    shape.closeSubpath();
    return shape;
}

void FlowCommandButton::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    // 控件矩形会互相重叠；掩模让箭头区域仍由前一段接收点击，避免误触后一段。
    setMask(QRegion(shapePath().toFillPolygon().toPolygon()));
}

FlowCommandRail::FlowCommandRail(QWidget *parent)
    : QFrame(parent)
{
}

void FlowCommandRail::setButtons(FlowCommandButton *start, FlowCommandButton *stop,
                                 FlowCommandButton *vent)
{
    m_buttons = {start, stop, vent};
    positionButtons();
}

void FlowCommandRail::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    positionButtons();
}

void FlowCommandRail::positionButtons()
{
    if (!m_buttons[0])
        return;

    constexpr int overlap = 14;
    const int innerWidth = qMax(0, width() - 2);
    const int totalButtonWidth = innerWidth + overlap * 2;
    const int firstWidth = totalButtonWidth / 3;
    const int secondWidth = totalButtonWidth / 3;
    const int thirdWidth = totalButtonWidth - firstWidth - secondWidth;
    m_buttons[0]->setGeometry(1, 1, firstWidth, height() - 2);
    m_buttons[1]->setGeometry(1 + firstWidth - overlap, 1, secondWidth, height() - 2);
    m_buttons[2]->setGeometry(1 + firstWidth + secondWidth - overlap * 2, 1,
                              thirdWidth, height() - 2);
}

ProcessFlowWidget::ProcessFlowWidget(QWidget *parent)
    : QWidget(parent)
{
    // 固定宽度、鼠标手型和强焦点共同保证流程图可读且可用键盘操作。
    setMinimumWidth(360);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("七步自动运行流程"));
    setToolTip(QStringLiteral("单击任一步骤查看该步骤的工作详情、趋势和设备事件记录"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void ProcessFlowWidget::setSnapshot(const ProcessSnapshot &snapshot)
{
    // 仅缓存快照并请求重绘；实际绘制发生在下一次 paintEvent()。
    m_snapshot = snapshot;
    update();
}

void ProcessFlowWidget::paintEvent(QPaintEvent *)
{
    // 自绘顺序：外框 → 当前运行摘要 → 进度条 → 七个步骤节点。
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
    // 窗口变矮时同步压缩七行，点击命中计算也必须复用同一公式。
    const double rowHeight = std::min(68.0, (height() - firstY - 22.0) / 7.0);
    const double nodeX = 35.0;
    for (int i = 0; i < 7; ++i) {
        const double centerY = firstY + i * rowHeight;
        const StepState state = m_snapshot.steps[static_cast<size_t>(i)];
        const QColor color = stepColor(state);

        // 已完成且下一步不再等待时连接线实线绿色，否则使用虚线。
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

        // 只给真正执行中的步骤蓝色背景，避免“选中查看”和“正在执行”看起来相同。
        if (state == StepState::Active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#EAF3FE")));
            painter.drawRoundedRect(QRectF(12, centerY - 20, width() - 24, 42), 6, 6);
        }

        painter.setPen(QPen(color, 2.0));
        painter.setBrush(state == StepState::Pending ? Qt::white : color);
        // 中止画方形、故障画菱形，其余状态画圆形，便于不只依赖颜色区分。
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
    // 前面绘制流程节点时会根据最后一步状态切换画刷。这里必须明确关闭填充，
    // 否则停止、回温或故障后，提示框会错误继承第七步的橙色、青色或红色背景。
    // 提示框只表达“这里可以点击”，因此始终保持白底（面板底色）和蓝色边框。
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor(QStringLiteral("#1F6FD1")));
    painter.drawRoundedRect(detailHint, 5, 5);
    painter.drawText(detailHint, Qt::AlignCenter, QStringLiteral("点击任一步骤查看该步骤详情"));
}

void ProcessFlowWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // 松开左键才激活，避免鼠标按下并拖出控件时误切换详情页。
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
    // 上下键只移动选择，不改变流程；回车、空格才发出查看详情请求。
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
    // 该命中区域必须与 paintEvent() 的节点行位置保持一致。
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
    // 初始值全部设为离线，直到控制器逐个发布七设备状态。
    // 摘要区释放出的空间用于把设备栏从 88 像素增至 104 像素，避免图标和文字拥挤。
    setFixedHeight(104);
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
    // index 是设备的稳定位置；越界数据直接忽略，防止破坏固定七格布局。
    if (status.index < 0 || status.index >= 7)
        return;
    m_devices[static_cast<size_t>(status.index)] = status;
    update();
}

void DeviceHealthWidget::paintEvent(QPaintEvent *)
{
    // 每格依次画图标、状态点、设备名称、健康文字和 keyValue。
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
        QFont nameFont = font();
        nameFont.setPixelSize(13);
        nameFont.setWeight(QFont::DemiBold);
        const QFontMetricsF nameMetrics(nameFont);
        const qreal nameWidth = nameMetrics.horizontalAdvance(device.name);

        QFont statusFont = font();
        statusFont.setPixelSize(12);
        const QString status = healthText(device.health);
        const QString auxiliary = device.keyValue.isEmpty()
            ? (device.updatedAt.isValid()
                   ? device.updatedAt.toString(QStringLiteral("HH:mm"))
                   : QStringLiteral("--:--"))
            : device.keyValue;
        const QFontMetricsF statusMetrics(statusFont);

        // 每格内容由“名称行 + 图标及两行读数”组成，先计算整体尺寸，再放到格子中心。
        constexpr qreal contentHeight = 68.0;
        const qreal contentTop = (height() - contentHeight) / 2.0;
        const qreal nameGroupWidth = 8.0 + 6.0 + nameWidth;
        const qreal nameLeft = item.left() + (item.width() - nameGroupWidth) / 2.0;
        const qreal textWidth = std::max(statusMetrics.horizontalAdvance(status),
                                         statusMetrics.horizontalAdvance(auxiliary));
        const qreal lowerGroupWidth = 26.0 + 8.0 + textWidth;
        const qreal lowerLeft = item.left() + (item.width() - lowerGroupWidth) / 2.0;
        const qreal lowerTop = contentTop + 27.0;

        QIcon(kDeviceIcons.at(i)).paint(&painter,
                                        QRect(static_cast<int>(lowerLeft),
                                              static_cast<int>(lowerTop + 6), 26, 26));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(nameLeft + 4, contentTop + 9), 4, 4);

        painter.setFont(nameFont);
        painter.setPen(QColor(QStringLiteral("#142033")));
        painter.drawText(QRectF(nameLeft + 14, contentTop, nameWidth + 1, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, device.name);

        painter.setFont(statusFont);
        painter.setPen(color);
        painter.drawText(QRectF(lowerLeft + 34, lowerTop, textWidth + 1, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, status);
        painter.setPen(QColor(QStringLiteral("#66758A")));
        // 有关键读数时优先显示 keyValue；没有时退回显示最后更新时间。
        painter.drawText(QRectF(lowerLeft + 34, lowerTop + 20, textWidth + 1, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, auxiliary);
    }
}
