#include "EngineerWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// 状态指示灯：亮 = 绿色（或指定色），灭 = 浅灰
void setLedState(QLabel *led, bool on, const QString &colorOn = QStringLiteral("#22c55e"))
{
    const int r = led->width() / 2;
    led->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #3a3a5e;"
                                      " border-radius:%2px;")
                           .arg(on ? colorOn : QStringLiteral("#3a3a5e"))
                           .arg(r));
}

QLabel *makeLed(QWidget *parent, int size = 14)
{
    auto *led = new QLabel(parent);
    led->setFixedSize(size, size);
    setLedState(led, false);
    return led;
}

// 一行控件：LED + 两个按钮
QWidget *makeRow(QWidget *parent, QLabel *led, QPushButton *b1, QPushButton *b2)
{
    auto *row = new QWidget(parent);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    h->addWidget(led);
    h->addWidget(b1);
    h->addWidget(b2);
    h->addStretch(1);
    return row;
}

QDoubleSpinBox *makeDoubleSpin(QWidget *parent, double lo, double hi, int dec, double val)
{
    // 工程师参数输入框的公共初始化，避免每个参数重复设置范围和小数位。
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setValue(val);
    return s;
}

} // namespace

EngineerWidget::EngineerWidget(QWidget *parent)
    : QWidget(parent)
{
    // 三个页签一次性创建，切换页签时只改变可见内容，不丢失当前输入值。
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildManualTab(), QStringLiteral("硬件单动"));
    m_tabs->addTab(buildDiagnosticsTab(), QStringLiteral("IO 与诊断"));
    m_tabs->addTab(buildConfigTab(), QStringLiteral("系统参数"));
    v->addWidget(m_tabs);
}

EngineerWidget::ValveRow EngineerWidget::makeValveRow(const QString &name, QFormLayout *form)
{
    // 每个阀门统一为“状态灯 + 开启 + 关闭”，返回指针以便槽函数回显。
    ValveRow r;
    QWidget *parent = form->parentWidget();
    auto *nameLbl = new QLabel(name, parent);
    r.led = makeLed(parent);
    r.onBtn = new QPushButton(QStringLiteral("开启"), parent);
    r.offBtn = new QPushButton(QStringLiteral("关闭"), parent);
    r.onBtn->setCursor(Qt::PointingHandCursor);
    r.offBtn->setCursor(Qt::PointingHandCursor);
    form->addRow(nameLbl, makeRow(parent, r.led, r.onBtn, r.offBtn));
    return r;
}

// ============================================================================
// Tab 1：硬件单动
// ============================================================================
QWidget *EngineerWidget::buildManualTab()
{
    auto *w = new QWidget(this);
    auto *grid = new QGridLayout(w);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setSpacing(12);

    // ---- 阀门控制区 ----
    auto *valveBox = new QGroupBox(QStringLiteral("阀门控制（硬件单动）"), w);
    auto *vf = new QFormLayout(valveBox);
    vf->setLabelAlignment(Qt::AlignLeft);
    vf->setHorizontalSpacing(16);
    vf->setVerticalSpacing(10);
    m_gateRow = makeValveRow(QStringLiteral("高真空气动主阀"), vf);
    m_foreRow = makeValveRow(QStringLiteral("前级电磁阀"), vf);
    m_ventRow = makeValveRow(QStringLiteral("放空阀"), vf);
    // 工程师手动点击仍只发请求，实际阀门状态以后由控制器回传。
    connect(m_gateRow.onBtn, &QPushButton::clicked, this, [this] { emit gateValveSet(true); });
    connect(m_gateRow.offBtn, &QPushButton::clicked, this, [this] { emit gateValveSet(false); });
    connect(m_foreRow.onBtn, &QPushButton::clicked, this, [this] { emit foreValveSet(true); });
    connect(m_foreRow.offBtn, &QPushButton::clicked, this, [this] { emit foreValveSet(false); });
    connect(m_ventRow.onBtn, &QPushButton::clicked, this, [this] { emit ventValveSet(true); });
    connect(m_ventRow.offBtn, &QPushButton::clicked, this, [this] { emit ventValveSet(false); });

    // ---- 泵类与制冷控制区 ----
    auto *pumpBox = new QGroupBox(QStringLiteral("泵类与制冷控制"), w);
    auto *pf = new QFormLayout(pumpBox);
    pf->setLabelAlignment(Qt::AlignLeft);
    pf->setHorizontalSpacing(16);
    pf->setVerticalSpacing(10);

    m_dryLed = makeLed(pumpBox);
    m_dryOn = new QPushButton(QStringLiteral("启动"), pumpBox);
    m_dryOff = new QPushButton(QStringLiteral("停止"), pumpBox);
    pf->addRow(QStringLiteral("干泵"), makeRow(pumpBox, m_dryLed, m_dryOn, m_dryOff));

    m_tmpLed = makeLed(pumpBox);
    m_tmpOn = new QPushButton(QStringLiteral("启动"), pumpBox);
    m_tmpOff = new QPushButton(QStringLiteral("停止"), pumpBox);
    pf->addRow(QStringLiteral("分子泵 TMP"), makeRow(pumpBox, m_tmpLed, m_tmpOn, m_tmpOff));

    m_tmpSpeed = new QSpinBox(pumpBox);
    m_tmpSpeed->setRange(0, 90000);
    m_tmpSpeed->setSingleStep(1000);
    m_tmpSpeed->setValue(45000);
    m_tmpSpeed->setSuffix(QStringLiteral(" rpm"));
    m_tmpSpeedBtn = new QPushButton(QStringLiteral("设置转速"), pumpBox);
    auto *speedRow = new QWidget(pumpBox);
    auto *sh = new QHBoxLayout(speedRow);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(8);
    sh->addWidget(m_tmpSpeed, 1);
    sh->addWidget(m_tmpSpeedBtn);
    pf->addRow(QStringLiteral("TMP 目标转速"), speedRow);

    m_cryoLed = makeLed(pumpBox);
    m_cryoOn = new QPushButton(QStringLiteral("使能"), pumpBox);
    m_cryoOff = new QPushButton(QStringLiteral("禁用"), pumpBox);
    pf->addRow(QStringLiteral("制冷机"), makeRow(pumpBox, m_cryoLed, m_cryoOn, m_cryoOff));

    connect(m_dryOn, &QPushButton::clicked, this, [this] { emit dryPumpSet(true); });
    connect(m_dryOff, &QPushButton::clicked, this, [this] { emit dryPumpSet(false); });
    connect(m_tmpOn, &QPushButton::clicked, this, [this] { emit tmpSet(true); });
    connect(m_tmpOff, &QPushButton::clicked, this, [this] { emit tmpSet(false); });
    connect(m_tmpSpeedBtn, &QPushButton::clicked, this,
            [this] { emit tmpSpeedSet(m_tmpSpeed->value()); });
    connect(m_cryoOn, &QPushButton::clicked, this, [this] { emit cryocoolerSet(true); });
    connect(m_cryoOff, &QPushButton::clicked, this, [this] { emit cryocoolerSet(false); });

    // ---- 加热控制区 ----
    auto *heatBox = new QGroupBox(QStringLiteral("加热控制"), w);
    auto *hf = new QFormLayout(heatBox);
    hf->setLabelAlignment(Qt::AlignLeft);
    hf->setHorizontalSpacing(16);
    hf->setVerticalSpacing(10);

    m_heaterChk = new QCheckBox(QStringLiteral("手动使能加热"), heatBox);
    m_pidP = makeDoubleSpin(heatBox, 0.0, 999.0, 2, 10.0);
    m_pidI = makeDoubleSpin(heatBox, 0.0, 999.0, 2, 0.5);
    m_pidD = makeDoubleSpin(heatBox, 0.0, 999.0, 2, 0.0);
    m_pidTarget = makeDoubleSpin(heatBox, 0.0, 400.0, 1, 295.0);
    m_pidTarget->setSuffix(QStringLiteral(" K"));
    m_pidApply = new QPushButton(QStringLiteral("应用 PID 参数"), heatBox);

    hf->addRow(m_heaterChk);
    hf->addRow(QStringLiteral("比例 P"), m_pidP);
    hf->addRow(QStringLiteral("积分 I"), m_pidI);
    hf->addRow(QStringLiteral("微分 D"), m_pidD);
    hf->addRow(QStringLiteral("目标温度"), m_pidTarget);
    hf->addRow(m_pidApply);

    // 勾选框由控制器状态回显时也可能改变，因此控制器实现需避免无效重复切换。
    connect(m_heaterChk, &QCheckBox::toggled, this, &EngineerWidget::heaterSet);
    connect(m_pidApply, &QPushButton::clicked, this, [this] {
        emit pidParamsSet(m_pidP->value(), m_pidI->value(), m_pidD->value(), m_pidTarget->value());
    });

    // ---- 系统状态（实时） ----
    auto *stBox = new QGroupBox(QStringLiteral("系统状态（实时）"), w);
    auto *sf = new QFormLayout(stBox);
    sf->setLabelAlignment(Qt::AlignLeft);
    sf->setHorizontalSpacing(16);
    sf->setVerticalSpacing(10);
    m_stPhase = new QLabel(QStringLiteral("—"), stBox);
    m_stVacuum = new QLabel(QStringLiteral("—"), stBox);
    m_stTemp = new QLabel(QStringLiteral("—"), stBox);
    sf->addRow(QStringLiteral("当前相态"), m_stPhase);
    sf->addRow(QStringLiteral("腔体真空度"), m_stVacuum);
    sf->addRow(QStringLiteral("样品温度"), m_stTemp);

    grid->addWidget(valveBox, 0, 0);
    grid->addWidget(pumpBox, 0, 1);
    grid->addWidget(heatBox, 1, 0);
    grid->addWidget(stBox, 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    return w;
}

// ============================================================================
// Tab 2：IO 与诊断看板
// ============================================================================
QWidget *EngineerWidget::buildDiagnosticsTab()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(12);

    // ---- DI 数字量输入 ----
    auto *diBox = new QGroupBox(QStringLiteral("DI 数字量输入（灯亮 = 正常）"), w);
    auto *diGrid = new QGridLayout(diBox);
    diGrid->setHorizontalSpacing(24);
    diGrid->setVerticalSpacing(8);

    // 名称数组和 MockSystem::DiIndex 的顺序必须完全一致。
    static const char *kDiNames[kDiCount] = {
        "水冷流量开关", "急停按钮", "看门狗心跳",
        "前级门联锁", "加热超温保护", "制冷机高压保护"
    };
    for (int i = 0; i < kDiCount; ++i) {
        auto *name = new QLabel(QString::fromUtf8(kDiNames[i]), diBox);
        m_diLed[i] = makeLed(diBox, 12);
        m_diText[i] = new QLabel(QStringLiteral("—"), diBox);
        const int col = (i % 3) * 3;
        const int row = i / 3;
        diGrid->addWidget(name, row, col);
        diGrid->addWidget(m_diLed[i], row, col + 1);
        diGrid->addWidget(m_diText[i], row, col + 2);
    }
    diGrid->setColumnStretch(2, 1);
    diGrid->setColumnStretch(5, 1);
    v->addWidget(diBox);

    // ---- 传感器模拟量反馈 ----
    auto *snsBox = new QGroupBox(QStringLiteral("传感器模拟量反馈（原始报文占位）"), w);
    auto *snsGrid = new QGridLayout(snsBox);
    snsGrid->setHorizontalSpacing(24);
    snsGrid->setVerticalSpacing(8);

    // 原始读数名称和 MockSystem::SensorIndex 的顺序必须完全一致。
    static const char *kSnsNames[kSnsCount] = {
        "真空规 1（冷阴极）", "真空规 2（皮拉尼）",
        "温度传感器 A（DT-670）", "温度传感器 B（Cernox）",
        "加热器电流", "制冷机功耗"
    };
    for (int i = 0; i < kSnsCount; ++i) {
        auto *name = new QLabel(QString::fromUtf8(kSnsNames[i]), snsBox);
        m_snsValue[i] = new QLabel(QStringLiteral("—"), snsBox);
        m_snsValue[i]->setStyleSheet(
            QStringLiteral("color:#7dd3fc; font-family:monospace; font-size:12px;"));
        m_snsValue[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const int col = (i % 2) * 2;
        const int row = i / 2;
        snsGrid->addWidget(name, row, col);
        snsGrid->addWidget(m_snsValue[i], row, col + 1);
    }
    snsGrid->setColumnStretch(1, 1);
    snsGrid->setColumnStretch(3, 1);
    v->addWidget(snsBox);

    // ---- 原始报文 / 事件日志 ----
    auto *logBox = new QGroupBox(QStringLiteral("原始报文 / 事件日志"), w);
    auto *lv = new QVBoxLayout(logBox);
    m_log = new QPlainTextEdit(logBox);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    lv->addWidget(m_log);
    v->addWidget(logBox, 1);
    return w;
}

// ============================================================================
// Tab 3：系统参数设置
// ============================================================================
QWidget *EngineerWidget::buildConfigTab()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(12);

    auto *box = new QGroupBox(QStringLiteral("系统参数设置"), w);
    auto *f = new QFormLayout(box);
    f->setLabelAlignment(Qt::AlignLeft);
    f->setHorizontalSpacing(16);
    f->setVerticalSpacing(12);

    m_cfgRough = makeDoubleSpin(box, 0.1, 1000.0, 3, 5.0);
    m_cfgRough->setSuffix(QStringLiteral(" Pa"));
    m_cfgHigh = makeDoubleSpin(box, 0.000001, 1.0, 6, 0.001);
    m_cfgHigh->setSuffix(QStringLiteral(" Pa"));
    m_cfgCoolRate = makeDoubleSpin(box, 0.1, 50.0, 1, 5.0);
    m_cfgCoolRate->setSuffix(QStringLiteral(" K/min"));
    m_cfgWarmRate = makeDoubleSpin(box, 0.1, 50.0, 1, 3.0);
    m_cfgWarmRate->setSuffix(QStringLiteral(" K/min"));

    f->addRow(QStringLiteral("粗抽真空门限"), m_cfgRough);
    f->addRow(QStringLiteral("高真空门限"), m_cfgHigh);
    f->addRow(QStringLiteral("降温最大速率"), m_cfgCoolRate);
    f->addRow(QStringLiteral("回温最大速率"), m_cfgWarmRate);

    auto *btnRow = new QHBoxLayout;
    auto *resetBtn = new QPushButton(QStringLiteral("恢复默认"), box);
    auto *saveBtn = new QPushButton(QStringLiteral("保存参数"), box);
    saveBtn->setObjectName(QStringLiteral("PrimaryButton"));
    saveBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(resetBtn);
    btnRow->addWidget(saveBtn);
    btnRow->addStretch(1);
    f->addRow(btnRow);

    // 明确告知此页参数只作用于 Mock 控制器，不能视为真实硬件标定结果。
    auto *note = new QLabel(
        QStringLiteral("说明：门限与速率参数由工程师模式修改并立即生效（Mock 控制器）。\n"
                       "升降温度速率在演示中按 K/tick 加速执行，真实 FSM 接入后按 K/min 生效。"),
        w);
    note->setObjectName(QStringLiteral("HintLabel"));
    note->setWordWrap(true);

    v->addWidget(box);
    v->addWidget(note);
    v->addStretch(1);

    connect(saveBtn, &QPushButton::clicked, this, [this] {
        emit configApplied(m_cfgRough->value(), m_cfgHigh->value(),
                           m_cfgCoolRate->value(), m_cfgWarmRate->value());
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        m_cfgRough->setValue(5.0);
        m_cfgHigh->setValue(0.001);
        m_cfgCoolRate->setValue(5.0);
        m_cfgWarmRate->setValue(3.0);
        emit configResetRequested();
    });
    return w;
}

// ============================================================================
// 状态回显槽
// ============================================================================
void EngineerWidget::onGateValveChanged(bool open) { setLedState(m_gateRow.led, open); }
void EngineerWidget::onForeValveChanged(bool open) { setLedState(m_foreRow.led, open); }
void EngineerWidget::onVentValveChanged(bool open) { setLedState(m_ventRow.led, open); }
void EngineerWidget::onDryPumpChanged(bool on) { setLedState(m_dryLed, on); }
void EngineerWidget::onTmpChanged(bool on) { setLedState(m_tmpLed, on); }
void EngineerWidget::onTmpSpeedChanged(int rpm) { m_tmpSpeed->setValue(rpm); }
void EngineerWidget::onCryocoolerChanged(bool on) { setLedState(m_cryoLed, on); }
void EngineerWidget::onHeaterChanged(bool on) { m_heaterChk->setChecked(on); }

void EngineerWidget::onDiChanged(int index, bool on)
{
    // 索引来自控制器；越界时忽略，避免数组访问越界。
    if (index < 0 || index >= kDiCount)
        return;
    setLedState(m_diLed[index], on);
    m_diText[index]->setText(on ? QStringLiteral("正常") : QStringLiteral("触发/异常"));
    m_diText[index]->setStyleSheet(on ? QStringLiteral("color:#22c55e;")
                                      : QStringLiteral("color:#ef4444;"));
}

void EngineerWidget::onSensorRawChanged(int index, const QString &text)
{
    // 这里只显示控制器给出的原始字符串，不再解释或转换其物理含义。
    if (index < 0 || index >= kSnsCount)
        return;
    m_snsValue[index]->setText(text);
}

void EngineerWidget::onLogMessage(const QString &msg)
{
    // QPlainTextEdit 已设定最大行数，长期运行不会无限占用内存。
    if (m_log)
        m_log->appendPlainText(msg);
}

void EngineerWidget::onVacuumChanged(double pa)
{
    m_stVacuum->setText(QStringLiteral("%1 Pa").arg(pa, 0, 'e', 2));
}

void EngineerWidget::onTemperatureChanged(double k)
{
    m_stTemp->setText(QStringLiteral("%1 K").arg(k, 0, 'f', 1));
}

void EngineerWidget::onPhaseChanged(const QString &phase)
{
    m_stPhase->setText(phase);
}
