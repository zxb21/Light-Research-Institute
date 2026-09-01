#include "ProcessStateMachine.h"

#include <QtTest>

class ProcessStateTest : public QObject
{
    Q_OBJECT

private slots:
    void qiDongLiuChengBaoLiuYiWanChengBuZhou();
    void PuTongTingZhiBaoLiuZhongZhiWeiZhi();
    void GeJieDuanPuTongTingZhiBaoLiuWeiZhi_data();
    void GeJieDuanPuTongTingZhiBaoLiuWeiZhi();
    void JiangWenJieDuanTingZhiJinRuHuiWen();
    void FangKongHeFuWeiShouDaoAnQuanTiaoJianYueShu();
    void GuZhangBaoLiuFaShengBuZhou();
    void TongXinZhongDuanZiDongChuFaAnQuanTingZhi();
    void ChongFuMingLingBuChanShengFeiFaTiaoZhuan();
};

void ProcessStateTest::qiDongLiuChengBaoLiuYiWanChengBuZhou()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QCOMPARE(state.snapshot().state, SystemState::Preparing);

    QVERIFY(state.advanceTo(SystemState::Roughing));
    QVERIFY(state.advanceTo(SystemState::HighVacuum));
    QVERIFY(state.advanceTo(SystemState::PreCooling));
    QVERIFY(state.advanceTo(SystemState::DeepCooling));
    QVERIFY(state.advanceTo(SystemState::Stabilizing));
    QVERIFY(state.advanceTo(SystemState::Running));

    const ProcessSnapshot snapshot = state.snapshot();
    QCOMPARE(snapshot.currentStep, ProcessStep::StabilizeAndRun);
    QCOMPARE(snapshot.steps[0], StepState::Completed);
    QCOMPARE(snapshot.steps[4], StepState::Completed);
    QCOMPARE(snapshot.steps[5], StepState::Active);
    QCOMPARE(snapshot.progressPercent, 86);
}

void ProcessStateTest::GeJieDuanPuTongTingZhiBaoLiuWeiZhi_data()
{
    QTest::addColumn<SystemState>("targetState");
    QTest::addColumn<ProcessStep>("interruptedStep");
    QTest::newRow("粗抽") << SystemState::Roughing << ProcessStep::RoughVacuum;
    QTest::newRow("高真空") << SystemState::HighVacuum << ProcessStep::HighVacuum;
    QTest::newRow("深冷") << SystemState::DeepCooling << ProcessStep::DeepCooling;
    QTest::newRow("运行") << SystemState::Running << ProcessStep::StabilizeAndRun;
}

void ProcessStateTest::GeJieDuanPuTongTingZhiBaoLiuWeiZhi()
{
    QFETCH(SystemState, targetState);
    QFETCH(ProcessStep, interruptedStep);
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    const SystemState sequence[] = {
        SystemState::Roughing, SystemState::HighVacuum, SystemState::PreCooling,
        SystemState::DeepCooling, SystemState::Stabilizing, SystemState::Running
    };
    for (SystemState next : sequence) {
        if (state.snapshot().state == targetState)
            break;
        QVERIFY(state.advanceTo(next));
    }
    QCOMPARE(state.snapshot().state, targetState);
    QVERIFY(state.requestStop(targetState >= SystemState::DeepCooling ? 100.0 : 295.0,
                              1.0e-3));
    QCOMPARE(state.snapshot().steps[static_cast<int>(interruptedStep)],
             StepState::Interrupted);
    QCOMPARE(state.snapshot().steps[6], StepState::Recovering);
}

void ProcessStateTest::JiangWenJieDuanTingZhiJinRuHuiWen()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QVERIFY(state.advanceTo(SystemState::Roughing));
    QVERIFY(state.advanceTo(SystemState::HighVacuum));
    QVERIFY(state.advanceTo(SystemState::PreCooling));
    QVERIFY(state.requestStop(295.0, 1.0e-3));
    QVERIFY(state.advanceTo(SystemState::Warming));
    QCOMPARE(state.snapshot().state, SystemState::Warming);
    QCOMPARE(state.snapshot().steps[3], StepState::Interrupted);
    QCOMPARE(state.snapshot().steps[6], StepState::Recovering);
}

void ProcessStateTest::PuTongTingZhiBaoLiuZhongZhiWeiZhi()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QVERIFY(state.advanceTo(SystemState::Roughing));
    QVERIFY(state.advanceTo(SystemState::HighVacuum));

    QVERIFY(state.requestStop(295.0, 1.0e-3));
    const ProcessSnapshot snapshot = state.snapshot();
    QCOMPARE(snapshot.state, SystemState::Stopping);
    QCOMPARE(snapshot.steps[0], StepState::Completed);
    QCOMPARE(snapshot.steps[1], StepState::Completed);
    QCOMPARE(snapshot.steps[2], StepState::Interrupted);
    QCOMPARE(snapshot.steps[6], StepState::Recovering);
}

void ProcessStateTest::FangKongHeFuWeiShouDaoAnQuanTiaoJianYueShu()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QVERIFY(state.advanceTo(SystemState::Roughing));
    QVERIFY(state.requestStop(295.0, 10.0));
    QVERIFY(state.advanceTo(SystemState::ReadyToVent));

    state.updateSafetyConditions(true, true, 280.0, 10.0, true, true, true);
    QVERIFY(!state.requestVent());
    QVERIFY(!state.snapshot().commands.canVent);

    state.updateSafetyConditions(true, true, 295.0, 10.0, true, false, true);
    QVERIFY(!state.requestVent());
    QVERIFY(state.snapshot().commands.ventReason.contains(QStringLiteral("泵组")));

    state.updateSafetyConditions(true, true, 295.0, 10.0, true, true, true);
    QVERIFY(state.requestVent());
    QCOMPARE(state.snapshot().state, SystemState::Venting);
    QVERIFY(!state.requestVent());

    state.enterRecovering();
    state.updateSafetyConditions(true, true, 295.0, 101325.0, true, true, true);
    state.completeRecovery();
    QCOMPARE(state.snapshot().state, SystemState::Idle);
    QCOMPARE(state.snapshot().steps[0], StepState::Pending);
}

void ProcessStateTest::TongXinZhongDuanZiDongChuFaAnQuanTingZhi()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QVERIFY(state.advanceTo(SystemState::Roughing));
    state.updateSafetyConditions(false, true, 295.0, 100.0, false, false, true);
    QCOMPARE(state.snapshot().state, SystemState::SafetyStopping);
    QCOMPARE(state.snapshot().steps[1], StepState::Faulted);
    QVERIFY(state.fault().text.contains(QStringLiteral("通信")));
}

void ProcessStateTest::GuZhangBaoLiuFaShengBuZhou()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QVERIFY(state.advanceTo(SystemState::Roughing));
    QVERIFY(state.advanceTo(SystemState::HighVacuum));

    QVERIFY(state.requestSafetyStop(QStringLiteral("分子泵通信中断"), 1));
    QCOMPARE(state.snapshot().state, SystemState::SafetyStopping);
    QCOMPARE(state.snapshot().currentStep, ProcessStep::HighVacuum);
    QCOMPARE(state.snapshot().steps[2], StepState::Faulted);
    QCOMPARE(state.fault().deviceIndex, 1);

    state.enterResetRequired();
    state.updateSafetyConditions(false, true, 295.0, 101325.0, true, true, true);
    QVERIFY(!state.requestReset());
    state.updateSafetyConditions(true, true, 295.0, 101325.0, true, true, true);
    QVERIFY(state.requestReset());
    QCOMPARE(state.snapshot().state, SystemState::Idle);
}

void ProcessStateTest::ChongFuMingLingBuChanShengFeiFaTiaoZhuan()
{
    ProcessStateMachine state;
    QVERIFY(state.requestStart());
    QVERIFY(!state.requestStart());
    QVERIFY(state.requestStop(295.0, 101325.0));
    QVERIFY(!state.requestStop(295.0, 101325.0));
    QVERIFY(!state.requestReset());
}

QTEST_GUILESS_MAIN(ProcessStateTest)

#include "test_process_state.moc"
