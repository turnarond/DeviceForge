#include "ModbusPointModel.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QLatin1Char>
#include <QPalette>

namespace modbus {

namespace {

// 可视区判定：未接滚动条（m_visLast<0）时全表视为可见（保守默认，测试语义不变）
bool rowVisible(int row, int first, int last)
{
    return last < 0 || (row >= first && row <= last);
}

} // namespace

ModbusPointModel::ModbusPointModel(QObject* parent) : QAbstractTableModel(parent)
{
}

QString ModbusPointModel::decimalText(quint16 value)
{
    return QString::number(value);
}

QString ModbusPointModel::hexText(quint16 value)
{
    // "0x04D2"：前缀小写 0x，数字大写、4 位零填充（仅数字部分取大写）
    const QString digits = QString::number(value, 16).toUpper();
    return QStringLiteral("0x%1").arg(digits, 4, QLatin1Char('0'));
}

QString ModbusPointModel::changedMark(bool increased)
{
    return increased ? QStringLiteral("↑") : QStringLiteral("↓");
}

void ModbusPointModel::rebuild(const QVector<quint16>& prev, const QVector<quint16>& cur)
{
    m_points.resize(m_spec.count);
    for (int i = 0; i < m_points.size(); ++i) {
        Point p;
        p.value = i < cur.size() ? cur.at(i) : 0;
        if (i < prev.size() && i < cur.size() && prev.at(i) != cur.at(i))
            p.changed = cur.at(i) > prev.at(i) ? 1 : 2;
        m_points[i] = p;
    }
}

void ModbusPointModel::setTarget(const QString& targetId, const PollSpec& spec,
                                 const QVector<quint16>& prev, const QVector<quint16>& cur)
{
    beginResetModel();
    m_targetId = targetId;
    m_spec = spec;
    m_visFirst = 0;
    m_visLast = -1;
    rebuild(prev, cur);
    endResetModel();
}

void ModbusPointModel::applyUpdate(const QVector<quint16>& prev, const QVector<quint16>& cur)
{
    // 行数与目标不符 = 结构性变化（spec 改了）→ 自动整表 reset
    if (cur.size() != m_points.size()) {
        beginResetModel();
        rebuild(prev, cur);
        endResetModel();
        return;
    }
    const int changedCount = qMin(prev.size(), cur.size());
    for (int row = 0; row < m_points.size(); ++row) {
        Point& p = m_points[row];
        p.value = cur.at(row);
        const quint8 mark = (row < changedCount && prev.at(row) != cur.at(row))
            ? (cur.at(row) > prev.at(row) ? 1 : 2) : 0;
        const bool refreshNeeded = (mark != 0) || (p.changed != 0);  // 新增标记或清除旧标记
        p.changed = mark;
        if (!refreshNeeded) continue;
        p.dirty = true;
        // 虚拟化：不可见行只更新模型数据不发 dataChanged（等滚入时补发）
        if (rowVisible(row, m_visFirst, m_visLast)) {
            p.dirty = false;
            emit dataChanged(index(row, ColDecimal), index(row, ColChanged));
        }
    }
}

void ModbusPointModel::setSampleMeta(qint64 wallMs, const QString& error)
{
    m_updatedText = wallMs > 0
        ? QDateTime::fromMSecsSinceEpoch(wallMs).toString(QStringLiteral("HH:mm:ss"))
        : QStringLiteral("—");
    m_error = error;
    if (m_points.isEmpty()) return;
    // 裁决 3（收敛旧全表 dataChanged）：可视区内脏行一次列级区间信号，
    // 越界行只置 metaDirty，滚入时补发——与 applyUpdate 同一虚拟化纪律
    for (Point& p : m_points) p.metaDirty = true;
    flushMetaInRange();
}

void ModbusPointModel::flushMetaInRange()
{
    int first = -1;
    int last = -1;
    for (int row = 0; row < m_points.size(); ++row) {
        if (!m_points[row].metaDirty || !rowVisible(row, m_visFirst, m_visLast)) continue;
        m_points[row].metaDirty = false;
        if (first < 0) first = row;
        last = row;
    }
    if (first < 0) return;
    // 元信息影响值列前景与更新时刻列，整段一次 Display+Foreground 信号
    emit dataChanged(index(first, ColDecimal), index(last, ColumnCount - 1),
                     {Qt::DisplayRole, Qt::ForegroundRole});
}

void ModbusPointModel::setHexMode(bool on)
{
    if (m_hex == on) return;
    m_hex = on;
    // 十/十六进制切换影响两列可见性（Widget 层 hide/show 列），前景不变无需全表信号
}

void ModbusPointModel::setVisibleRange(int first, int last)
{
    m_visFirst = first;
    m_visLast = last;
    flushDirtyInRange();
    flushMetaInRange();   // 裁决 3：滚入的 meta 脏行补发
}

void ModbusPointModel::flushDirtyInRange()
{
    for (int row = 0; row < m_points.size(); ++row) {
        if (!m_points[row].dirty || !rowVisible(row, m_visFirst, m_visLast)) continue;
        m_points[row].dirty = false;
        emit dataChanged(index(row, ColDecimal), index(row, ColChanged));
    }
}

int ModbusPointModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_points.size();
}

int ModbusPointModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

Qt::ItemFlags ModbusPointModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    // 数值经双击 → 写流程（View 的 doubleClicked），格内不直接编辑：剥除默认 Editable
    return QAbstractTableModel::flags(index) & ~Qt::ItemIsEditable;
}

QVariant ModbusPointModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_points.size()) return QVariant();
    const Point& p = m_points.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColAddress:
            return addressLabel(m_spec.regType, m_spec.startAddr + index.row());
        case ColDecimal: return decimalText(p.value);
        case ColHex:     return hexText(p.value);
        case ColChanged:
            return p.changed == 1 ? changedMark(true)
                                  : (p.changed == 2 ? changedMark(false) : QString());
        case ColUpdatedAt: return m_updatedText;
        default:         return QVariant();
        }
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() != ColUpdatedAt) return int(Qt::AlignRight | Qt::AlignVCenter);
        return QVariant();
    }
    if (role == Qt::ForegroundRole && !m_error.isEmpty()) {
        // 裁决 1：异常时值列照常显示旧 values 但置灰——取主题 PlaceholderText 派生灰，
        // 不新增十六进制色值（QSS 纪律）
        if (index.column() == ColDecimal || index.column() == ColHex)
            return QGuiApplication::palette().color(QPalette::PlaceholderText);
    }
    return QVariant();
}

QVariant ModbusPointModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
    case ColAddress:   return QStringLiteral("地址");
    case ColDecimal:   return QStringLiteral("十进制");
    case ColHex:       return QStringLiteral("十六进制");
    case ColChanged:   return QStringLiteral("变化");
    case ColUpdatedAt: return QStringLiteral("更新时刻");
    default:           return QVariant();
    }
}

} // namespace modbus
