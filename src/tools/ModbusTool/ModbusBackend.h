#pragma once
#include "framework/ToolBackend.h"
#include "ModbusScheduler.h"
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

// 界面下拉索引 → QModbusDataUnit 寄存器类型映射
// （与 ModbusWidget 下拉顺序一致：Holding/Input/Coils/Discrete Inputs）
inline QModbusDataUnit::RegisterType registerTypeFromComboIndex(int idx)
{
    switch (idx) {
    case 0:  return QModbusDataUnit::HoldingRegisters;
    case 1:  return QModbusDataUnit::InputRegisters;
    case 2:  return QModbusDataUnit::Coils;
    default: return QModbusDataUnit::DiscreteInputs;
    }
}

// 单次请求最大数量（Modbus 协议上限）：
// Holding/Input 寄存器 125 点；Coils/Discrete Inputs 允许 2000 点
inline int maxReadCountForType(QModbusDataUnit::RegisterType type)
{
    switch (type) {
    case QModbusDataUnit::Coils:
    case QModbusDataUnit::DiscreteInputs:
        return 2000;
    default:
        return 125;
    }
}

// v2.12 MODBUS 重构 Task 4：调度器驱动的传输执行器（IO 留 GUI 线程，规格 §1）。
// 旧"全局广播读 + 每客户端 1 挂起槽"已废除：目标集/队列/退避全在纯逻辑
// ModbusScheduler（入队即推进 nextDueMs，裁决 2），本类只做
// 250ms 驱动 tick → 派发 QModbusTcpClient 读(0x03/0x02/0x01/0x02)/写(0x06/0x05)
// → 响应归位 scheduler.onResult + 结果回调。
// 轮2 审查 I-1：本类不写 ModbusDataStore——store 单写者 = Widget 侧
// handleBackendResult（先读 prev 再落库，↑/↓ 变化高亮依赖旧值）；终审 I-5 后
// 后端连 dataStore 注入句柄都不再持有（死参与透传一并删除）。轮2 审查 I-2：轮询态写路径经
// scheduler.enqueueWrite 记账，readOnce 手工派发经 scheduler.onDispatched 记账，
// 与 onResult 三路（reply/设备错误/failDispatch）对称；终审 I-4：写队列满时
// enqueueWrite 拒绝入队，本类转一次错误回调+日志（"操作过多，请稍后"）。
// 结果交付携带 targetId（deviceId#slaveId）精确归位（裁决 1）。
class ModbusBackend : public ToolBackend {
public:
    ModbusBackend();
    ~ModbusBackend() override;

    int svc() override;

    std::string toolId() const override { return "com.deviceforge.modbus.test"; }
    std::string toolName() const override { return "Modbus 测试"; }
    std::string toolVersion() const override { return "2.2.0"; }
    std::string toolCategory() const override { return "test"; }
    std::string toolIcon() const override { return "modbus_test"; }

    // 设备绑定/凭据/配置：接口兼容保留（数据源 = startPolling 注入的目标集，规格 §2.2）
    void bindDevices(const std::vector<DeviceInfo>& devices) override;
    void bindCredentials(const AuthInfo& auth) override;
    void applyConfig(const lwserverbase::config::ConfigValue& config) override;

    // 回调（errorMsg 非空 = 读/写/连接失败，含 Modbus 异常码文本；
    // targetId 精确交付，写成功回执 values 为空 + errorMsg 为空）
    using LogCallback = std::function<void(const std::string&)>;
    using ResultCallback = std::function<void(const QString& targetId,
                                              const QVector<quint16>& values,
                                              qint64 elapsedMs, const QString& errorMsg)>;
    void setLogCallback(LogCallback cb) { m_logCb = std::move(cb); }
    void setResultCallback(ResultCallback cb) { m_resultCb = std::move(cb); }

    // ── 简报任务 4 接口块（供 Widget 驱动；终审 I-5 死参清理）──
    // 全量重建轮询目标集并启动驱动 timer（重复调用 = 先停后起，调度器 reset 清零在途/队列）。
    // 终审 I-5：旧签名的 store/data 两参从未参与调度与落库（store 单写者=Widget，裁决 1 后
    // 结果按 targetId 精确交付），已删除——dataStore 归 Widget 所有，后端不再持有句柄。
    void startPolling(const QVector<modbus::PollTarget>&);
    // 停 tick → 清队列/在途/手动读挂起 → 逐 client 先断信号后关（死锁规避口径）
    void stopPolling();
    // 立即读取一次选中目标：轮询中挂入待办（≤250ms 补发）；已停轮询则一次性直发
    void requestReadOnce(const QString& targetId);
    // 用户写：转 scheduler.enqueueWrite（插队首，队列满则拒绝并经错误通道提示一次）
    void enqueueWriteTarget(const QString& targetId, int addr, quint16 value);
    // 某目标的轮询溢出丢弃计数透传（终审 I-2：Widget 单写者落库时填入 Sample.skippedCount）
    int skippedFor(const QString& targetId) const { return m_scheduler.skippedCount(targetId); }
    // 目标所属连接是否处于退避等待（裁决 5"退避黄"判定；未轮询恒 false）
    bool inTargetBackoff(const QString& targetId) const;
    bool isPolling() const { return m_polling; }

private:
    // 内部接线：驱动 timer → onTick（派发→send 与 reply→onResult+结果回调 的挂接点；
    // store 落库归 Widget 单写者，见类注释 I-1 口径；
    // client 侧信号在 getOrCreateClient 创建处以 client 为 context 挂一次）
    void setSchedulerCallbacks();

    void onTick();
    // 单连接一在途派发执行：未连接 → 暂存待发并发起连接；已连接 → 直发
    void dispatchScheduled(const modbus::Dispatch& d);
    void sendDispatch(const modbus::Dispatch& d);
    void handleReply(const QString& connKey, QModbusReply* reply);
    void handleDeviceError(const QString& connKey, QModbusDevice::Error error);
    // 无响应可期的失败归位（发送失败/连接失败）：store+回调+调度器退避一并处理
    void failDispatch(const modbus::Dispatch& d, const QString& errText);
    static modbus::Dispatch makePollDispatch(const modbus::PollTarget& t);

    QModbusTcpClient* getOrCreateClient(const QString& connKey);

    // 在途请求档案：响应归位凭 reply 指针比对（代际守卫，防 stop/错误双路重复归位 UAF）
    struct ActiveRequest {
        QString targetId;
        modbus::Kind kind = modbus::Kind::Poll;
        QModbusReply* reply = nullptr;
        qint64 startedMs = 0;
    };

    modbus::ModbusScheduler m_scheduler;
    QTimer* m_driver = nullptr;                    // 固定 250ms 驱动节拍（见 startPolling 注释）
    QElapsedTimer m_clock;                         // 与调度器/退避门限同源的单调时钟
    QHash<QString, modbus::PollTarget> m_pollById; // targetId → 目标（readOnce/退避查询）
    QHash<QString, QModbusTcpClient*> m_clients;   // connKey(ip:port) → 连接池
    QHash<QString, ActiveRequest> m_active;        // connKey → 在途（单连接一在途）
    QHash<QString, modbus::Dispatch> m_pendingSend;// connKey → 等连接就绪的待发派发
    QSet<QString> m_readOnce;                      // 待补发的立即读取 targetId
    bool m_polling = false;
    LogCallback m_logCb;
    ResultCallback m_resultCb;
};
