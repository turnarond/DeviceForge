#pragma once

// v2.12 MODBUS 重构 Task 3：左栏·连接清单视图模型（namespace modbus）。
// 行 = 设备×从站（行身份 deviceId#slaveId，纯 UI 概念不入库）；列 = 状态｜名称｜端点｜
// 从站｜段｜周期｜详情。数据源三件套：ModbusTargetStore（目标档案）+ DeviceRegistry
// （名称/端点，缺席回退 device.list + ip:port 显示）+ ModbusDataStore（最近样本）。
// 格式化/过滤/健康判定全为纯函数或模型方法，Widget 只做组装（可测性纪律）。

#include <QAbstractTableModel>
#include <QColor>
#include <QString>
#include <QVector>

#include <functional>

#include "ModbusTargetTypes.h"

class DeviceRegistry;

namespace modbus {

class ModbusTargetStore;
class ModbusDataStore;

// 单行清单数据（refresh/syncSamples 后对视图只读）
struct TargetRow {
    QString targetId;      // makeTargetId(deviceId, slaveId)
    QString deviceId;      // 档案 deviceId；回退模式 = "ip:port"
    int slaveId = 1;
    bool enabled = true;
    PollSpec poll;
    QString name;          // 档案名称优先，回退 ip:port（裁决 3）
    QString endpoint;      // "ip:port"；孤儿记录 = "—"
    QString status;        // "OK" / "异常" / "退避" / "停用" / "—"
    QString detail;        // 耗时或错误摘要（截断）
    QString detailFull;    // 错误全文（tooltip）
    bool hasError = false; // 健康判定唯一权威 = Sample.error 非空（裁决 1）
    bool inBackoff = false; // 异常且调度器退避等待中 → "退避黄"（裁决 5，任务 4 接线）
    bool hasSample = false;
};

class ModbusTargetListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColStatus = 0, ColName, ColEndpoint, ColSlave, ColRange, ColInterval, ColDetail,
        ColumnCount
    };

    ModbusTargetListModel(ModbusTargetStore* store, ModbusDataStore* dataStore,
                          QObject* parent = nullptr);

    // 档案注册表可为空（回退 device.list + ip:port），设置即触发一次 refresh
    void setRegistry(DeviceRegistry* registry);
    DeviceRegistry* registry() const { return m_registry; }

    // 全量重建行（结构变更时调用：启动/添加目标/端口写回/搜索）；返回过滤后行数
    int refresh();
    // 样本到达后的增量同步：只重算状态/详情并逐变更行 dataChanged，不做整表 reset
    void syncSamples();
    void setSearchText(const QString& text);
    QString searchText() const { return m_search; }

    const TargetRow* rowAt(int row) const;
    const TargetRow* rowByTargetId(const QString& targetId) const;
    int rowIndexOfTarget(const QString& targetId) const;
    // 过滤前全集中的行号（裁决 1：后端结果按 targetId 精确归位走全集）；未找到 -1
    int allIndexOfTarget(const QString& targetId) const;

    // 退避态探针（任务 4 裁决 5）：由 Widget 注入 backend.inTargetBackoff；
    // 空指针 = 恒非退避（清单回退"异常红"，任务 3 既有语义不变）
    using BackoffProbe = std::function<bool(const QString& targetId)>;
    void setBackoffProbe(BackoffProbe probe) { m_backoffProbe = std::move(probe); }

    // 过滤前全集访问（行数 ≥ rowCount）：后端结果归位必须走全集——搜索过滤只影响视图，
    // 未命中行的读结果仍须写入 dataStore（绑定约束"不可见行只更新 store"）
    int allRowCount() const { return m_allRows.size(); }
    const TargetRow* allRowAt(int index) const;

    // 端口写回（裁决 4，v2.11.1 saveReplacing 语义）：档案 modbus 端点改端口并全量刷新，
    // 同 deviceId 的其余行由 refresh 呈现新端点。失败（无档案/无端点/写库失败）返回 false。
    bool applyPortEdit(const QString& deviceId, int newPort, QString* errText = nullptr);

    // Qt 模型接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    // ── 纯函数（可脱机单测） ──
    // 名称列解析：档案名优先，空则回退端点 ip:port（裁决 3）
    static QString resolveName(const QString& profileName, const QString& endpoint);
    // 段列："保持 40001..40003"（地址标签复用 addressLabel 的 4xxxx 约定）
    static QString rangeLabel(const PollSpec& spec);
    static QString intervalLabel(int intervalMs);
    // 详情截断（tooltip 存全文）；默认 40 字符
    static QString truncateDetail(const QString& text, int limit = 40);
    // 搜索过滤：名称或端点子串命中（大小写不敏感）；空条件恒真
    static bool filterBySearch(const TargetRow& row, const QString& needle);
    // 寄存器类型中文名（Holding/Input/Coil/Discrete → 保持/输入/线圈/离散）
    static QString registerTypeName(QModbusDataUnit::RegisterType type);
    // 双主题状态色：与 QSS 常量一致（暗 #40C8A0/#E85848，亮 #2FA88A/#D6453A），
    // 沿用 v2.11 ModbusWidget 同款取色，不新增色值（QSS 纪律）
    static QColor statusColor(bool success);

signals:
    // 端点列编辑写回失败（无档案/无 modbus 端点/写库失败），供 Widget 记日志
    void portEditFailed(const QString& message);

private:
    struct SourceDevice {
        QString deviceId;
        QString name;      // 档案名（可空 → 回退）
        QString endpoint;  // "ip:port"；无档案端点信息 = "—"
    };
    QVector<SourceDevice> collectDevices() const;
    void applyFilter();
    // 从 dataStore 拉样本回填行的状态/详情字段（refresh 与 syncSamples 共用）
    void fillSampleFields(TargetRow& row) const;
    // 裁决 4：主题状态色在 refresh 时缓存一次（data() 每格查库属禁改缺陷）
    void cacheThemeColors();

    ModbusTargetStore* m_store = nullptr;
    ModbusDataStore* m_data = nullptr;
    DeviceRegistry* m_registry = nullptr;
    QString m_search;
    QVector<TargetRow> m_allRows; // 过滤前全集
    QVector<TargetRow> m_rows;    // 视图当前行集
    BackoffProbe m_backoffProbe;  // 裁决 5：退避态判定注入点
    QColor m_errorColor;          // 裁决 4 缓存：异常红
    QColor m_backoffColor;        // 裁决 4/5 缓存：退避黄（既有琴色 token，双主题同值）
};

} // namespace modbus
