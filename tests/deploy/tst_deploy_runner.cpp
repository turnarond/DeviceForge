/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: tst_deploy_runner.cpp
 *
 * Date: 2026-08-22
 *
 * Author: turnarond
 *
 * Description: DeploymentRunner 并发调度单元测试（v2.8 并行批量部署 Task 3）。
 *              Mock IDeployable 注入 ProtocolRegistry（"mockrun" 键），零网络：
 *              并发峰值用原子计数器实测、取消经 globalCancel/requestCancel 双路注入。
 */

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "adapter/IProtocolAdapter.h"
#include "adapter/IDeployable.h"
#include "adapter/ProtocolRegistry.h"
#include "framework/DeviceInfo.h"
#include "tools/FtpDeployTool/DeployJob.h"
#include "tools/FtpDeployTool/DeploymentRunner.h"

// ---------------------------------------------------------------------------
// Mock 部署通道：connect/upload 全部本地空转，仅统计调用与并发峰值。
// 计数器为类级 static——工厂每次 create 新实例，但统计在进程内聚合。
// 运行期只读配置（s_uploadMs/s_failFiles）由测试用例在 run() 前写定。
// ---------------------------------------------------------------------------
class MockDeployable : public IProtocolAdapter, public IDeployable {
public:
    // --- 进程级共享统计 ---
    static std::atomic<int> s_active;        // 正在上传的文件数
    static std::atomic<int> s_peak;          // 并发峰值（CAS 实测）
    static std::atomic<int> s_uploadCalls;   // uploadFile 调用总数
    static std::atomic<int> s_connectCalls;  // connect 调用总数

    // --- 运行期只读配置（init() 重置，run() 前写定） ---
    static int s_uploadMs;
    static std::set<std::string> s_failFiles;  // 命中文件名的 uploadFile 返回 false
    static std::atomic<bool> s_gate;           // false = uploadFile 挂起等待放闸
    static int s_progressBurst;                // 成功路径连发的进度回调数（≥1）

    std::string protocolId() const override { return "mockrun"; }
    bool connect(const DeviceInfo&, const AuthInfo&) override {
        ++s_connectCalls;
        return true;
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    std::string lastError() const override { return m_error; }
    std::future<Response> request(const Request&) override {
        return std::async(std::launch::deferred, [] { return Response{}; });
    }
    void subscribe(const Request&, StreamCallback) override {}
    void unsubscribe() override {}
    ProtocolCapability capability() const override { return {}; }

    bool uploadFile(const std::string& localPath, const std::string&) override {
        // 并发峰值实测：先递增，再 CAS 抬高 peak，离开时递减
        const int now = ++s_active;
        for (int p = s_peak.load(); now > p && !s_peak.compare_exchange_weak(p, now);) {
        }
        while (!s_gate.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // 模拟协议栈取消检查点（curl 进度回调返回非零 / SFTP 块间检查同理）：
            // 取消置位即中止本次传输并以失败返回
            if (m_flag && m_flag->load()) {
                --s_active;
                ++s_uploadCalls;
                m_error = "good.txt: 模拟取消中止";
                return false;
            }
        }
        QTest::qSleep(s_uploadMs);
        --s_active;
        ++s_uploadCalls;

        std::string name = localPath;
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (s_failFiles.count(name)) {
            // 真实适配器在传输中回调进度（curl xferinfo 同理），此处对齐契约
            if (m_progressCb) m_progressCb(50);
            m_error = name + ": 模拟传输失败";
            return false;
        }
        if (m_progressCb) {
            // 连发模拟高频进度注入（curl 分块上报同理），用于总进度节流验证
            const int burst = std::max(1, s_progressBurst);
            for (int b = 1; b <= burst; ++b) {
                m_progressCb(b * 100 / burst);
            }
        }
        return true;
    }
    bool uploadFolder(const std::string&, const std::string&) override {
        ++s_uploadCalls;
        return true;
    }
    bool clearRemoteDirectory(const std::string&) override { return true; }
    void setProgressCallback(std::function<void(int)> cb) override { m_progressCb = std::move(cb); }
    void setCancelFlag(std::atomic<bool>* flag) override { m_flag = flag; }

    std::string m_error;
    std::atomic<bool>* m_flag = nullptr;
    std::function<void(int)> m_progressCb;
};

std::atomic<int> MockDeployable::s_active{0};
std::atomic<int> MockDeployable::s_peak{0};
std::atomic<int> MockDeployable::s_uploadCalls{0};
std::atomic<int> MockDeployable::s_connectCalls{0};
int MockDeployable::s_uploadMs = 0;
std::set<std::string> MockDeployable::s_failFiles;
std::atomic<bool> MockDeployable::s_gate{true};
int MockDeployable::s_progressBurst = 1;

// ---------------------------------------------------------------------------
class TstDeployRunner : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmpDir;
    std::string m_goodFile;  // 真实存在的源文件（DeployJob 会做 is_directory 探测）
    std::string m_badFile;   // 内容同 good，仅文件名命中失败集

    // 构造 n 台设备的 Params：ip=192.168.1.N, port=21，files 各带一份指定列表
    std::vector<DeployJob::Params> makeParams(int n, const std::vector<std::string>& files) const {
        std::vector<DeployJob::Params> params;
        params.reserve(n);
        for (int i = 0; i < n; ++i) {
            DeployJob::Params p;
            p.device.ip = "192.168.1." + std::to_string(i + 1);
            p.device.port = 21;
            p.auth = {"user", "pass"};
            p.files = files;
            p.remotePath = "/apps";
            p.protocol = "mockrun";
            params.push_back(std::move(p));
        }
        return params;
    }

    static std::vector<std::string> expectedKeys(int n) {
        std::vector<std::string> keys;
        for (int i = 0; i < n; ++i)
            keys.push_back("192.168.1." + std::to_string(i + 1) + ":21");
        return keys;
    }

private slots:
    // 每个用例前重置 mock 状态并注册 mock 工厂（QtTest 经元对象调用，必须在 slots 段）
    void init() {
        MockDeployable::s_active = 0;
        MockDeployable::s_peak = 0;
        MockDeployable::s_uploadCalls = 0;
        MockDeployable::s_connectCalls = 0;
        MockDeployable::s_uploadMs = 0;
        MockDeployable::s_failFiles.clear();
        MockDeployable::s_gate = true;
        MockDeployable::s_progressBurst = 1;
        ProtocolRegistry::instance()->registerFactory("mockrun", [] {
            return std::shared_ptr<IProtocolAdapter>(new MockDeployable);
        });
    }

    void initTestCase() {
        QVERIFY(m_tmpDir.isValid());
        m_goodFile = m_tmpDir.filePath("good.txt").toStdString();
        m_badFile = m_tmpDir.filePath("bad.txt").toStdString();
        for (const auto* f : {m_goodFile.c_str(), m_badFile.c_str()}) {
            QFile file(QString::fromLocal8Bit(f));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("runner-test-fixture");
        }
    }

    // 断言 1：10 台 × concurrency=3 → 并发峰值不超 3 且全部 Ok
    void concurrencyCap_allOk() {
        MockDeployable::s_uploadMs = 20;  // 拉开窗口让三线程重叠可测
        DeploymentRunner runner;
        std::atomic<bool> cancel{false};
        const auto progress = [](const std::string&, int) {};

        DeployReport report = runner.run(makeParams(10, {m_goodFile}), 3, cancel, progress);

        QCOMPARE(report.concurrency, 3);
        QCOMPARE(QString::fromStdString(report.protocol), QStringLiteral("mockrun"));
        QCOMPARE((int)report.results.size(), 10);

        // 峰值下界≥1（确实跑过）且上界≤3（不越并发上限）
        QVERIFY2(MockDeployable::s_peak.load() >= 1, "无任何并发被观测到");
        QVERIFY2(MockDeployable::s_peak.load() <= 3,
                 qPrintable(QString("并发峰值 %1 > 3").arg(MockDeployable::s_peak.load())));

        // 全部成功且逐台字段完备（carry-forward b：state 必须是 Job 显式赋值）
        const auto keys = expectedKeys(10);
        std::set<std::string> gotKeys;
        for (size_t i = 0; i < report.results.size(); ++i) {
            const DeviceResult& r = report.results[i];
            QCOMPARE(r.state, DeviceResult::Ok);
            QVERIFY(!r.deviceKey.empty());
            gotKeys.insert(r.deviceKey);
            QVERIFY2(r.durationMs >= 0, "durationMs 未赋值（默认负值不可能，此处防呆）");
            QVERIFY(r.startedAt != 0);
            QVERIFY(r.failedFiles.empty());
            QVERIFY(r.lastError.empty());
        }
        QCOMPARE((int)gotKeys.size(), 10);
        for (const auto& k : keys) {
            QVERIFY2(gotKeys.count(k) == 1,
                     qPrintable(QString("缺少/重复设备 key: %1").arg(QString::fromStdString(k))));
        }
        QCOMPARE(MockDeployable::s_uploadCalls.load(), 10);
    }

    // 断言 2：globalCancel 预置 true → 不调度直接全 Cancelled，零适配器活动
    void globalCancelPreflight_allCancelled() {
        MockDeployable::s_uploadMs = 50;
        DeploymentRunner runner;
        std::atomic<bool> cancel{true};  // 预置取消
        const auto progress = [](const std::string&, int) {};

        QElapsedTimer timer;
        timer.start();
        DeployReport report = runner.run(makeParams(10, {m_goodFile}), 4, cancel, progress);
        const qint64 elapsedMs = timer.elapsed();

        QVERIFY2(elapsedMs < 1000,
                 qPrintable(QString("预检取消耗时 %1ms ≥ 1s，疑似仍调度了任务").arg(elapsedMs)));
        QCOMPARE((int)report.results.size(), 10);
        for (const DeviceResult& r : report.results) {
            QCOMPARE(r.state, DeviceResult::Cancelled);
            QVERIFY(r.failedFiles.empty());
        }
        // 零网络/零连接证据：mock 完全未被触及
        QCOMPARE(MockDeployable::s_connectCalls.load(), 0);
        QCOMPARE(MockDeployable::s_uploadCalls.load(), 0);
    }

    // 断言 3（carry-forward b 回归）：单文件失败 → 该台 Failed 且 failedFiles 记名，其余台 Ok
    void singleFileFailure_failedFilesRecorded() {
        MockDeployable::s_failFiles.insert("bad.txt");
        DeploymentRunner runner;
        std::atomic<bool> cancel{false};
        const auto progress = [](const std::string&, int) {};

        // 设备 1 只传 good；设备 2 传 good+bad；设备 3 只传 bad
        std::vector<DeployJob::Params> params = makeParams(1, {m_goodFile});
        DeployJob::Params mixed = params[0];
        mixed.files = {m_goodFile, m_badFile};
        params.push_back(mixed);
        DeployJob::Params onlyBad = params[0];
        onlyBad.files = {m_badFile};
        params.push_back(onlyBad);

        DeployReport report = runner.run(params, 3, cancel, progress);
        QCOMPARE((int)report.results.size(), 3);

        const DeviceResult& rGoodOnly = report.results[0];
        QCOMPARE(rGoodOnly.state, DeviceResult::Ok);
        QVERIFY(rGoodOnly.failedFiles.empty());

        const DeviceResult& rMixed = report.results[1];
        QCOMPARE(rMixed.state, DeviceResult::Failed);
        QCOMPARE((int)rMixed.failedFiles.size(), 1);
        QCOMPARE(QString::fromStdString(rMixed.failedFiles.front()), QStringLiteral("bad.txt"));
        QVERIFY(!rMixed.lastError.empty());  // 失败摘要来自 adapter->lastError()

        const DeviceResult& rBadOnly = report.results[2];
        QCOMPARE(rBadOnly.state, DeviceResult::Failed);
        QCOMPARE((int)rBadOnly.failedFiles.size(), 1);
        QCOMPARE(QString::fromStdString(rBadOnly.failedFiles.front()), QStringLiteral("bad.txt"));
    }

    // 断言 4：deviceProgress 收到的 key 与设备集合一致且次数 > 0
    void deviceProgress_keysCorrect() {
        MockDeployable::s_uploadMs = 5;
        DeploymentRunner runner;
        std::atomic<bool> cancel{false};

        QMutex cbMutex;
        std::multiset<std::string> progressKeys;
        int totalCallbacks = 0;
        const auto progress = [&](const std::string& key, int pct) {
            QMutexLocker lock(&cbMutex);
            ++totalCallbacks;
            progressKeys.insert(key);
            QVERIFY2(pct >= 0 && pct <= 100, "进度百分比越界");
        };

        runner.run(makeParams(4, {m_goodFile}), 2, cancel, progress);

        QMutexLocker lock(&cbMutex);
        QVERIFY2(totalCallbacks > 0, "deviceProgress 未收到任何回调");
        // 4 台设备的 key 全部出现过
        for (const auto& k : expectedKeys(4)) {
            QVERIFY2(progressKeys.count(k) > 0,
                     qPrintable(QString("缺少设备进度 key: %1").arg(QString::fromStdString(k))));
        }
    }

    // 断言 5（Brief Step 3）：总进度节流——错开设备文件数使进度更新簇跨越
    // 多个 ≥100ms 节流窗口：中程（强制末次之前）至少发射一次，且转发总数
    // 远小于注入数；全部完成后的强制末次保证 100% 必达。
    // 空转免疫：若门闸失效（如 lastEmit 初值与 now() 相减溢出导致永不发射），
    // 中程计数为 0、仅剩 1 次强制末次——forwards>=2 断言即失败，缺陷无法被掩盖。
    void totalProgress_throttled_finalForced() {
        MockDeployable::s_uploadMs = 150;      // 单文件 150ms，使更新簇错峰跨窗口
        MockDeployable::s_progressBurst = 5;   // 每文件连发 5 次（同簇内仅首个可能过闸）
        DeploymentRunner runner;
        std::atomic<bool> cancel{false};

        QMutex cbMutex;
        int injectedPerDevice = 0;  // per-device 流收到的原始回调数（=注入数）
        // 每次总进度发射的采样时刻：最后一条必为 waitForDone 后的强制末次
        // （此后无线程存活、不可能再有任何发射），其余全部为中程发射
        std::vector<std::chrono::steady_clock::time_point> overallStamps;
        int overallLast = -1;
        const auto progress = [&](const std::string& key, int pct) {
            QMutexLocker lock(&cbMutex);
            if (key == DeploymentRunner::kOverallKey) {
                overallStamps.emplace_back(std::chrono::steady_clock::now());
                overallLast = pct;
            } else {
                ++injectedPerDevice;
            }
            QVERIFY2(pct >= 0 && pct <= 100, "进度百分比越界");
        };

        // 设备 1 传 1 个文件、设备 2 传 3 个文件（并发 2）：更新簇落在
        // ≈150/300/450ms——首簇必过闸（初值已开窗），后续簇以 150ms 间隔
        // 落入新开的节流窗口，保证 ≥1 次中程发射
        std::vector<DeployJob::Params> params = makeParams(1, {m_goodFile});
        DeployJob::Params multi = params[0];
        multi.files = {m_goodFile, m_goodFile, m_goodFile};
        params.push_back(std::move(multi));

        runner.run(params, 2, cancel, progress);

        QMutexLocker lock(&cbMutex);
        const int midFlight = static_cast<int>(overallStamps.size()) - 1;
        qDebug("总进度发射 %lld 次（中程 %d + 强制末次 1），per-device 注入 %d 次",
               static_cast<long long>(overallStamps.size()), midFlight,
               injectedPerDevice);
        QCOMPARE(injectedPerDevice, 20);  // 1+3 个文件 × 连发 5 次（确定性）
        QVERIFY2(midFlight >= 1,
                 "无任何中程总进度发射——节流门闸未开启（疑似初值溢出回归）");
        // 结构上界：每簇至多过闸 1 次 + 强制末次；4 簇 ⇒ 上限 5，留慢机余量取 10
        QVERIFY2(static_cast<int>(overallStamps.size()) <= 10,
                 qPrintable(QString("总进度转发 %1 次超出节流上界 10")
                                .arg(overallStamps.size())));
        QCOMPARE(overallLast, 100);       // 强制末次：最后一次必为 100%（全部 Ok）
    }

    // 取消传播（controller Ruling 1）：requestCancel 经 DeployJob::setCancel 生效——
    // 在途台次完成当前文件后于文件循环头中止并记 Cancelled，未启动台次跳过；
    // carry-forward b 回归：旧实现此处 state 会静默停留在默认 Ok。
    void requestCancel_midFlight_allCancelledNotSilentlyOk() {
        MockDeployable::s_uploadMs = 30;
        MockDeployable::s_gate = false;  // 闸门：首个文件上传挂起，保证确定性时序
        DeploymentRunner runner;
        std::atomic<bool> cancel{false};
        const auto progress = [](const std::string&, int) {};

        DeployReport report;
        std::thread worker([&] {
            report = runner.run(makeParams(9, {m_goodFile}), 3, cancel, progress);
        });

        // 等 3 个在途 job 都进入 uploadFile 挂起点（uploadCalls 尚未自增，
        // 用 active 计数判断），随后请求取消并放闸
        QTRY_VERIFY_WITH_TIMEOUT(MockDeployable::s_active.load() == 3, 5000);
        runner.requestCancel();
        MockDeployable::s_gate = true;
        worker.join();

        QCOMPARE((int)report.results.size(), 9);
        int cancelledCount = 0, okCount = 0, failedCount = 0;
        for (const DeviceResult& r : report.results) {
            if (r.state == DeviceResult::Cancelled) ++cancelledCount;
            else if (r.state == DeviceResult::Ok) ++okCount;
            else ++failedCount;
        }
        // 在途 3 台 + 未启动 6 台全部 Cancelled；不允许任何台静默 Ok
        QVERIFY2(cancelledCount == 9,
                 qPrintable(QString("ok=%1 failed=%2 cancelled=%3")
                                .arg(okCount).arg(failedCount).arg(cancelledCount)));
        // 放闸前已挂起的 3 个文件完成后不再发起新上传（未启动台次零调用）
        QCOMPARE(MockDeployable::s_uploadCalls.load(), 3);
    }

    // carry-forward a 回归（调度路径）：Params.globalCancel 缺省为空指针时，
    // Runner 入池前统一接线引用参数，不得解引用崩溃
    void nullCancelFlags_treatedAsNotCancelled() {
        DeploymentRunner runner;
        std::atomic<bool> batchCancel{false};  // run() 引用参数恒有效；空指针风险在 Params 字段
        const auto progress = [](const std::string&, int) {};

        // Params.globalCancel 保持默认 nullptr（模拟未接线的上游构造）
        std::vector<DeployJob::Params> params = makeParams(2, {m_goodFile});
        for (auto& p : params) p.globalCancel = nullptr;

        DeployReport report = runner.run(params, 2, batchCancel, progress);

        QCOMPARE((int)report.results.size(), 2);
        for (const DeviceResult& r : report.results)
            QCOMPARE(r.state, DeviceResult::Ok);
    }

    // carry-forward a 回归（裸 Job 路径）：不经 Runner 直接 run() 时，
    // globalCancel 与 setCancel 均空——取消判定视为未取消，且适配器拿到的
    // 取消指针必须非空（IDeployable 契约，内部哑标志兜底）
    void bareJob_nullFlags_runsAndGivesAdapterNonNullFlag() {
        std::vector<DeployJob::Params> params = makeParams(1, {m_goodFile});
        params[0].globalCancel = nullptr;  // 双空：无 globalCancel、未 setCancel

        DeployJob job(std::move(params[0]));
        job.run();  // 同步直跑（Task 2 串行路径）

        QCOMPARE(job.result().state, DeviceResult::Ok);
        QVERIFY(MockDeployable::s_uploadCalls.load() == 1);
    }
};

QTEST_MAIN(TstDeployRunner)
#include "tst_deploy_runner.moc"
