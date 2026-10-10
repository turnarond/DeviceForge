#pragma once

// v2.12 MODBUS 重构 Task 3：右栏·逐寄存器视图模型（namespace modbus）。
// 一寄存器一行；列 = 地址｜十进制｜十六进制｜变化｜更新时刻。
// setTarget 做整表 reset、applyUpdate 增量刷新：仅值变化行发 dataChanged，
// 且不可见行只更新内部数据不发信号（模型内跟踪可视区，虚拟化纪律）。
// 变化标记 Changed 由调用方传入的 prev/cur 快照比对（store 上次快照）。
// 裁决 1：Sample.error 非空 → 值列照常显示旧 values 但前景置灰。

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

#include "ModbusTargetTypes.h"

namespace modbus {

class ModbusPointModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColAddress = 0, ColDecimal, ColHex, ColChanged, ColUpdatedAt,
        ColumnCount
    };

    explicit ModbusPointModel(QObject* parent = nullptr);

    // 整表重置：spec 决定行数与地址标签；prev/cur 比对生成变化标记
    void setTarget(const QString& targetId, const PollSpec& spec,
                   const QVector<quint16>& prev, const QVector<quint16>& cur);
    // 增量刷新：行数与 spec 一致时仅对变化行发 dataChanged（大小不符自动 reset）
    void applyUpdate(const QVector<quint16>& prev, const QVector<quint16>& cur);
    // 样本元信息（裁决 3 收敛）：wallMs=真实时钟毫秒（0 → "—"）；error 非空 → 值列置灰。
    // 可视区纪律同 applyUpdate：只对可视行发 dataChanged，越界行置 metaDirty 滚入时补发
    void setSampleMeta(qint64 wallMs, const QString& error);

    void setHexMode(bool on);
    bool hexMode() const { return m_hex; }

    // 可视行区（Widget 由视图滚动条驱动；默认 -1 = 全表视为可见）
    void setVisibleRange(int first, int last);

    QString targetId() const { return m_targetId; }
    PollSpec spec() const { return m_spec; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // ── 纯函数 ──
    static QString decimalText(quint16 value);      // "1234"
    static QString hexText(quint16 value);           // "0x04D2"
    static QString changedMark(bool increased);      // 增 "↑" / 减 "↓"

private:
    struct Point {
        quint16 value = 0;
        quint8 changed = 0;   // 0 无标记 / 1 ↑ / 2 ↓
        bool dirty = false;   // 值/标记已更新但（因不可见）未发 dataChanged
        bool metaDirty = false; // 元信息（时刻/置灰）已更新但未发 dataChanged（裁决 3）
    };
    void rebuild(const QVector<quint16>& prev, const QVector<quint16>& cur);
    void flushDirtyInRange();
    // 裁决 3：可视区内脏 meta 行一次列级区间 dataChanged；越界行保留 metaDirty 待滚入补发
    void flushMetaInRange();

    QString m_targetId;
    PollSpec m_spec;
    QVector<Point> m_points;
    QString m_updatedText = QStringLiteral("—");
    QString m_error;               // 非空 = 异常（值列置灰）
    bool m_hex = false;
    int m_visFirst = 0;
    int m_visLast = -1;            // -1 = 全表可见（未接滚动条时的安全默认）
};

} // namespace modbus
