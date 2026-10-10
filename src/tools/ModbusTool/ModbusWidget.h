#pragma once

// v2.12 MODBUS 重构 Task 3：MODBUS 测试工具前端（双栏组装层）。
// 左栏·连接清单（mbTargetTable）+ 右栏·逐寄存器视图（mbPointTable），
// 格式化/过滤/健康判定全部下沉 ModbusTargetListModel / ModbusPointModel 与纯函数；
// Widget 只做组装、输入与展示（src/tools/AGENTS.md 反模式纪律）。
// 轮询驱动（调度器 ↔ 后端接线）在任务 4 落地；本层先交付视图与写路径。

#include "framework/ToolWidget.h"

#include <QHash>
#include <QString>
#include <QVector>

#include "tools/ModbusTool/ModbusDataStore.h"
#include "tools/ModbusTool/ModbusTargetStore.h"

namespace modbus {
class ModbusPointModel;
class ModbusTargetListModel;
}

class ModbusBackend;
class DeviceRegistry;
class QAbstractItemView;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTableView;

class ModbusWidget : public ToolWidget {
    Q_OBJECT
public:
    explicit ModbusWidget(QWidget* parent = nullptr);
    ~ModbusWidget() override = default;

    QString toolId() const override { return QStringLiteral("com.deviceforge.modbus.test"); }
    QString toolName() const override { return QStringLiteral("Modbus 测试"); }
    void onToolStart() override;
    void onToolStop() override;

    void setBackend(ModbusBackend* backend);
    // 新增：档案注册表注入；空则清单回退 device.list + ip:port 显示（裁决 3）
    void setRegistry(DeviceRegistry* registry);

    // 观察点与任务 4 接线面
    modbus::ModbusTargetListModel* targetModel() const { return m_targetModel; }
    modbus::ModbusPointModel* pointModel() const { return m_pointModel; }
    modbus::ModbusDataStore* dataStore() { return &m_dataStore; }
    modbus::ModbusTargetStore* targetStore() { return &m_targetStore; }
    bool pollingPaused() const;

    // 写确认（保守方向，同任务中心风险确认精神）：恒需确认的纯逻辑 + 模态框
    static bool shouldConfirmWrite(const QString& targetId, int addr, quint16 value);
    // 弹出模态确认（objectName=mbWriteConfirm，默认按钮=取消）；确认返回 true
    bool confirmWrite(const QString& targetId, int addr, quint16 value);

private slots:
    void onAddTargetClicked();
    void onPauseToggled(bool checked);
    void onReadOnceClicked();
    void onTargetSelectionChanged();
    void onPointDoubleClicked(const QModelIndex& index);
    void onPortEditFailed(const QString& message);

private:
    void setupUi();
    void appendLog(const QString& msg);
    void refreshTargets();
    void showTargetDetail(const QString& targetId);
    void syncDevicesToBackend();
    void updateVisibleRangeTracking(QTableView* view, modbus::ModbusPointModel* model);
    // 添加目标：为设备落默认从站 1 目标记录；createEndpoint=true 先建默认 502 modbus 端点写回档案
    void ensureDefaultTarget(const QString& deviceId, bool createEndpoint);
    // 后端结果回调（旧广播 API 的临时归位：按端点 ip 匹配行；任务 4 换 targetId 精确交付）
    void handleBackendResult(const QString& deviceIp, const QVector<quint16>& values,
                             qint64 elapsedMs, const QString& error);

    ModbusBackend* m_backend = nullptr;
    DeviceRegistry* m_registry = nullptr;

    modbus::ModbusTargetStore m_targetStore;
    modbus::ModbusDataStore m_dataStore;
    QHash<QString, qint64> m_wallMsByTarget;  // UpdatedAt 列展示用真实时钟（样本时间为单调源）

    modbus::ModbusTargetListModel* m_targetModel = nullptr;
    modbus::ModbusPointModel* m_pointModel = nullptr;

    QSplitter* m_splitter = nullptr;
    QTableView* m_targetView = nullptr;
    QTableView* m_pointView = nullptr;
    QPushButton* m_addTargetBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_readOnceBtn = nullptr;
    QPushButton* m_hexToggleBtn = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_pointTitle = nullptr;
};
