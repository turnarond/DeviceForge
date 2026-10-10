#include "ModbusTargetListModel.h"

#include "config/ConfigStore.h"
#include "device/DeviceProfile.h"
#include "device/DeviceRegistry.h"
#include "tools/ModbusTool/ModbusDataStore.h"
#include "tools/ModbusTool/ModbusTargetStore.h"

#include <QLatin1Char>
#include <QLatin1String>

#include <optional>

namespace modbus {

namespace {

// 端点字符串解析：要求末段为 1..65535 端口；失败返回 false
bool parseEndpointPort(const QString& endpoint, QString* ip, int* port)
{
    const int sep = endpoint.lastIndexOf(QLatin1Char(':'));
    if (sep <= 0 || sep == endpoint.size() - 1) return false;
    bool ok = false;
    const int p = endpoint.mid(sep + 1).toInt(&ok);
    if (!ok || p < 1 || p > 65535) return false;
    if (ip) *ip = endpoint.left(sep);
    if (port) *port = p;
    return true;
}

const DeviceEndpoint* findModbusEndpoint(const DeviceProfile& profile)
{
    for (const auto& endpoint : profile.endpoints) {
        if (endpoint.protocol == "modbus") return &endpoint;
    }
    return nullptr;
}

} // namespace

ModbusTargetListModel::ModbusTargetListModel(ModbusTargetStore* store, ModbusDataStore* dataStore,
                                             QObject* parent)
    : QAbstractTableModel(parent), m_store(store), m_data(dataStore)
{
}

void ModbusTargetListModel::setRegistry(DeviceRegistry* registry)
{
    m_registry = registry;
    refresh();
}

QString ModbusTargetListModel::resolveName(const QString& profileName, const QString& endpoint)
{
    // 名称优先，档案缺席/无名回退 ip:port（裁决 3，与设备栏一致）
    return profileName.isEmpty() ? endpoint : profileName;
}

QString ModbusTargetListModel::registerTypeName(QModbusDataUnit::RegisterType type)
{
    switch (type) {
    case QModbusDataUnit::DiscreteInputs: return QStringLiteral("离散");
    case QModbusDataUnit::Coils:          return QStringLiteral("线圈");
    case QModbusDataUnit::InputRegisters: return QStringLiteral("输入");
    case QModbusDataUnit::HoldingRegisters: return QStringLiteral("保持");
    default: return QStringLiteral("非法");
    }
}

QString ModbusTargetListModel::rangeLabel(const PollSpec& spec)
{
    const QString from = addressLabel(spec.regType, spec.startAddr);
    const QString to = addressLabel(spec.regType, spec.startAddr + spec.count - 1);
    return QStringLiteral("%1 %2..%3").arg(registerTypeName(spec.regType), from, to);
}

QString ModbusTargetListModel::intervalLabel(int intervalMs)
{
    return QStringLiteral("%1 ms").arg(intervalMs);
}

QString ModbusTargetListModel::truncateDetail(const QString& text, int limit)
{
    if (text.size() <= limit) return text;
    return text.left(limit) + QStringLiteral("…");
}

bool ModbusTargetListModel::filterBySearch(const TargetRow& row, const QString& needle)
{
    if (needle.trimmed().isEmpty()) return true;
    return row.name.contains(needle, Qt::CaseInsensitive)
        || row.endpoint.contains(needle, Qt::CaseInsensitive);
}

QColor ModbusTargetListModel::statusColor(bool success)
{
    // 双主题状态色：QSS 无法按行设置前景（非 QWidget），按主题取色；
    // 色值与两套 QSS 常量一致（暗 #40C8A0/#E85848，亮 #2FA88A/#D6453A），不新增色值
    const QString theme = ConfigStore::instance()
        .load(QStringLiteral("appearance"), QStringLiteral("theme"))
        .value(QStringLiteral("theme")).toString();
    const bool dark = (theme != QLatin1String("light"));
    return success ? QColor(dark ? QStringLiteral("#40C8A0") : QStringLiteral("#2FA88A"))
                   : QColor(dark ? QStringLiteral("#E85848") : QStringLiteral("#D6453A"));
}

void ModbusTargetListModel::cacheThemeColors()
{
    // 裁决 4：主题色只在 refresh 查库一次——data() 是视图每格回调，查库属禁改缺陷
    m_errorColor = statusColor(false);
    // 裁决 5：退避黄复用既有琴色 token #F0A030（信号态专用、双主题同值，不新增色值）
    m_backoffColor = QColor(QStringLiteral("#F0A030"));
}

QVector<ModbusTargetListModel::SourceDevice> ModbusTargetListModel::collectDevices() const
{
    QVector<SourceDevice> devices;
    if (m_registry) {
        if (!m_store) return devices;
        // 档案模式：清单行来自 modbus.target 记录（添加目标后入库），名称/端点向档案联查
        for (const auto& id : m_store->allDeviceIds()) {
            const QString deviceId = QString::fromStdString(id);
            SourceDevice dev;
            dev.deviceId = deviceId;
            dev.endpoint = QStringLiteral("—");
            const auto profile = m_registry->find(id);
            if (profile) {
                dev.name = QString::fromStdString(profile->name);
                const DeviceEndpoint* mb = findModbusEndpoint(*profile);
                if (mb) {
                    dev.endpoint = QString::fromLatin1(mb->ip.c_str())
                        + QLatin1Char(':') + QString::number(mb->port > 0 ? mb->port : 502);
                }
            }
            devices.push_back(dev);
        }
        return devices;
    }
    // 回退模式（无档案注册表）：与旧 UI/设备栏同源 device.list，身份 = "ip:port"
    const auto rows = ConfigStore::instance().list(QStringLiteral("device.list"), 20);
    for (const auto& row : rows) {
        const QString ip = row.value(QStringLiteral("ip")).toString();
        if (ip.isEmpty()) continue;
        int port = row.value(QStringLiteral("port")).toInt();
        if (port <= 0) port = 502;
        SourceDevice dev;
        dev.deviceId = ip + QLatin1Char(':') + QString::number(port);
        dev.endpoint = dev.deviceId;
        devices.push_back(dev);
    }
    return devices;
}

void ModbusTargetListModel::fillSampleFields(TargetRow& row) const
{
    const auto sample = m_data ? m_data->sample(row.targetId) : std::nullopt;
    row.hasSample = sample.has_value();
    row.detailFull.clear();
    if (!sample) {
        row.hasError = false;
        row.status = QStringLiteral("—");
        row.detail = QStringLiteral("等待首次轮询");
        return;
    }
    // 裁决 1：健康判定以 Sample.error 非空为唯一权威，不得单信 ok
    row.hasError = !sample->error.isEmpty();
    // 裁决 5：异常且调度器所属连接退避等待 → "退避黄"；探针未注入恒非退避（回退异常红）
    row.inBackoff = row.enabled && row.hasError && m_backoffProbe
                        && m_backoffProbe(row.targetId);
    if (!row.enabled) {
        row.status = QStringLiteral("停用");
    } else if (row.hasError) {
        row.status = row.inBackoff ? QStringLiteral("退避") : QStringLiteral("异常");
    } else if (sample->ok) {
        row.status = QStringLiteral("OK");
    } else {
        row.status = QStringLiteral("—");
    }
    if (row.hasError) {
        row.detail = truncateDetail(sample->error);
        row.detailFull = sample->error;  // tooltip 全文
    } else {
        row.detail = QStringLiteral("耗时 %1 ms").arg(sample->elapsedMs);
    }
    // 终审 I-2（规格 §2.3）：调度器轮询溢出跳过计数外露——>0 才追加，不制造噪声
    if (sample->skippedCount > 0)
        row.detail += QStringLiteral(" 跳过%1").arg(sample->skippedCount);
}

int ModbusTargetListModel::refresh()
{
    cacheThemeColors();   // 裁决 4：主题色每次 refresh 取一次，data() 不再查库
    QVector<TargetRow> all;
    for (const auto& dev : collectDevices()) {
        DeviceTargets dt;
        QVector<SlaveTarget> targets;
        const bool loaded = m_store && m_store->load(dev.deviceId.toStdString(), &dt);
        if (loaded)
            targets = dt.targets;
        // 裁决 6：档案模式下"空 targets 记录"= 删除全部目标的落库形态——
        // load 已不回退旧行（store 修复），清单侧同样不得合成默认行让它视图复活；
        // 仅回退模式（无档案注册表）设备缺记录时给默认从站 1 展示行
        if (targets.isEmpty() && !(m_registry && loaded)) {
            targets.push_back(SlaveTarget{});
        }
        for (const auto& slave : targets) {
            TargetRow row;
            row.deviceId = dev.deviceId;
            row.slaveId = slave.slaveId;
            row.enabled = slave.enabled;
            row.poll = slave.poll;
            row.targetId = makeTargetId(dev.deviceId.toStdString(), slave.slaveId);
            row.endpoint = dev.endpoint;
            const QString fallbackName =
                (dev.endpoint == QStringLiteral("—")) ? dev.deviceId : dev.endpoint;
            row.name = resolveName(dev.name, fallbackName);
            fillSampleFields(row);
            all.push_back(row);
        }
    }
    m_allRows = all;
    applyFilter();
    return m_rows.size();
}

void ModbusTargetListModel::syncSamples()
{
    // 增量同步：结构不变，只重算状态/详情。全集照常更新（含被过滤隐藏的行，
    // 不发信号——store-only）；仅可见行且字段实际变化时才 dataChanged，杜绝风暴
    for (int i = 0; i < m_allRows.size(); ++i) {
        TargetRow& all = m_allRows[i];
        const QString before = all.status + all.detail + all.detailFull;
        fillSampleFields(all);
        if ((all.status + all.detail + all.detailFull) == before) continue;
        const int visRow = rowIndexOfTarget(all.targetId);
        if (visRow < 0) continue;  // 被搜索过滤隐藏：只更新 store 侧数据
        m_rows[visRow] = all;
        emit dataChanged(index(visRow, ColStatus), index(visRow, ColDetail));
    }
}

void ModbusTargetListModel::setSearchText(const QString& text)
{
    if (m_search == text) return;
    m_search = text;
    applyFilter();
}

void ModbusTargetListModel::applyFilter()
{
    beginResetModel();
    m_rows.clear();
    for (const auto& row : m_allRows) {
        if (filterBySearch(row, m_search)) m_rows.push_back(row);
    }
    endResetModel();
}

const TargetRow* ModbusTargetListModel::rowAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return nullptr;
    return &m_rows.at(row);
}

const TargetRow* ModbusTargetListModel::allRowAt(int index) const
{
    if (index < 0 || index >= m_allRows.size()) return nullptr;
    return &m_allRows.at(index);
}

const TargetRow* ModbusTargetListModel::rowByTargetId(const QString& targetId) const
{
    const int row = rowIndexOfTarget(targetId);
    return row >= 0 ? &m_rows.at(row) : nullptr;
}

int ModbusTargetListModel::rowIndexOfTarget(const QString& targetId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).targetId == targetId) return i;
    }
    return -1;
}

int ModbusTargetListModel::allIndexOfTarget(const QString& targetId) const
{
    for (int i = 0; i < m_allRows.size(); ++i) {
        if (m_allRows.at(i).targetId == targetId) return i;
    }
    return -1;
}

bool ModbusTargetListModel::applyPortEdit(const QString& deviceId, int newPort, QString* errText)
{
    if (!m_registry) {
        if (errText) *errText = QStringLiteral("未接入设备档案，端口编辑不可用");
        return false;
    }
    auto profile = m_registry->find(deviceId.toStdString());
    if (!profile) {
        if (errText) *errText = QStringLiteral("设备档案缺失：%1").arg(deviceId);
        return false;
    }
    bool edited = false;
    // 终审 Minor①：只改首个 modbus 端点——与 collectDevices/findModbusEndpoint 的
    // "首个 modbus 端点即连接端点"口径一致，多端点档案不误改后备端点
    for (auto& endpoint : profile->endpoints) {
        if (endpoint.protocol == "modbus") {
            endpoint.port = newPort;
            edited = true;
            break;
        }
    }
    if (!edited) {
        if (errText) *errText = QStringLiteral("档案无 modbus 端点：%1").arg(deviceId);
        return false;
    }
    // v2.11.1 替换语义：端点整体写回 + 旧 device.list 行清扫（saveReplacing 内建）
    DeviceProfile stored;
    if (!m_registry->saveReplacing(*profile, &stored)) {
        if (errText) *errText = QStringLiteral("档案写回失败：%1").arg(deviceId);
        return false;
    }
    // 同步重建：同 deviceId 的其余行立即呈现新端点（reset 在 commitData 栈内安全，
    // 视图对已失效 editor 的 closeEditor 为空操作）
    refresh();
    return true;
}

int ModbusTargetListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ModbusTargetListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ModbusTargetListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return QVariant();
    const TargetRow& row = m_rows.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColStatus:   return row.status;
        case ColName:     return row.name;
        case ColEndpoint: return row.endpoint;
        case ColSlave:    return row.slaveId;
        case ColRange:    return rangeLabel(row.poll);
        case ColInterval: return intervalLabel(row.poll.intervalMs);
        case ColDetail:   return row.detail;
        default:          return QVariant();
        }
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColStatus || index.column() == ColSlave)
            return int(Qt::AlignCenter);
        return QVariant();
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == ColDetail && !row.detailFull.isEmpty()) return row.detailFull;
        return QVariant();
    }
    if (role == Qt::ForegroundRole) {
        if (row.hasError && (index.column() == ColStatus || index.column() == ColDetail))
            return row.inBackoff ? m_backoffColor : m_errorColor;   // 裁决 4 缓存色 / 裁决 5 退避黄
        return QVariant();
    }
    return QVariant();
}

QVariant ModbusTargetListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
    case ColStatus:   return QStringLiteral("状态");
    case ColName:     return QStringLiteral("名称");
    case ColEndpoint: return QStringLiteral("端点");
    case ColSlave:    return QStringLiteral("从站");
    case ColRange:    return QStringLiteral("段");
    case ColInterval: return QStringLiteral("周期");
    case ColDetail:   return QStringLiteral("详情");
    default:          return QVariant();
    }
}

Qt::ItemFlags ModbusTargetListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    // QAbstractItemModel 默认含 ItemIsEditable——必须显式剥除，否则双击任意格误开编辑器
    Qt::ItemFlags f = QAbstractTableModel::flags(index) & ~Qt::ItemIsEditable;
    // 端点列就地编辑端口（需档案注册表；回退/孤儿行只读）
    if (index.column() == ColEndpoint && m_registry) {
        const TargetRow* row = rowAt(index.row());
        if (row && row->endpoint != QStringLiteral("—")) f |= Qt::ItemIsEditable;
    }
    return f;
}

bool ModbusTargetListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != ColEndpoint) return false;
    const TargetRow* row = rowAt(index.row());
    if (!row) return false;
    // 编辑器给出纯端口数字；容忍 "ip:port" 全串两种写法
    const QString text = value.toString().trimmed();
    bool ok = false;
    int port = 0;
    if (text.contains(QLatin1Char(':'))) {
        ok = parseEndpointPort(text, nullptr, &port);
    } else {
        port = text.toInt(&ok);
    }
    if (!ok || port < 1 || port > 65535) return false;
    QString err;
    if (!applyPortEdit(row->deviceId, port, &err)) {
        emit portEditFailed(err);
        return false;
    }
    return true;
}

} // namespace modbus
