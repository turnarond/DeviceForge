#include "ModbusWidget.h"

#include "ModbusBackend.h"
#include "ModbusPointModel.h"
#include "ModbusTargetListModel.h"
#include "ModbusTargetTypes.h"
#include "config/ConfigStore.h"
#include "device/DeviceProfile.h"
#include "device/DeviceRegistry.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// 端点列就地编辑端口的 delegate：编辑器是 QLineEdit(objectName=mbPortEdit)，
// 只显示/校验端口数字；提交后由模型解析并经 DeviceRegistry::saveReplacing 写回档案
class MbEndpointPortDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex&) const override
    {
        auto* edit = new QLineEdit(parent);
        edit->setObjectName(QStringLiteral("mbPortEdit"));
        edit->setValidator(new QIntValidator(1, 65535, edit));
        return edit;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        const QString text = index.data(Qt::DisplayRole).toString();
        const int sep = text.lastIndexOf(QLatin1Char(':'));
        const QString port = sep >= 0 ? text.mid(sep + 1) : text;
        if (auto* edit = qobject_cast<QLineEdit*>(editor))
            edit->setText(port);
    }
    // setModelData 用默认实现：编辑器文本（纯端口数字）→ EditRole → 模型 setData
};

// 添加目标对话框（无模态，parented → 测试经 findChild 观察）：
// 一级 = 持有 modbus 端点的设备（档案优先名，回退 ip:port）+ 「其它设备…」入口；
// 二级 = 其余设备，选中即建默认 502 modbus 端点写回档案
class AddTargetDialog : public QDialog {
public:
    using PickFn = std::function<void(const QString& deviceId, bool createEndpoint)>;

    AddTargetDialog(DeviceRegistry* registry, QWidget* parent, PickFn pick)
        : QDialog(parent), m_pick(std::move(pick))
    {
        setObjectName(QStringLiteral("mbAddTargetDialog"));
        setWindowTitle(QStringLiteral("添加目标"));
        auto* lay = new QVBoxLayout(this);
        m_hint = new QLabel(
            QStringLiteral("选择持有 modbus 端点的设备，或经「其它设备…」建立默认端点"), this);
        m_hint->setWordWrap(true);
        lay->addWidget(m_hint);
        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("mbAddTargetList"));
        lay->addWidget(m_list);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
        box->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);
        lay->addWidget(box);
        populate(registry);
        connect(m_list, &QListWidget::itemClicked, this, &AddTargetDialog::onItemClicked);
    }

private:
    enum Mode { ModeDevice = 0, ModeOtherEntry = 1, ModeCreateEntry = 2 };

    void populate(DeviceRegistry* registry)
    {
        if (!registry) {
            m_hint->setText(QStringLiteral("未接入设备档案：请先在设备总线中添加设备"));
            return;
        }
        for (const auto& profile : registry->list()) {
            const QString id = QString::fromStdString(profile.deviceId);
            if (hasModbusEndpoint(profile)) {
                addItem(modbusDeviceLabel(profile), id, ModeDevice);
            } else {
                // 二级「其它设备…」候选：构造时缓存，点击时展开
                QString ip = QStringLiteral("?");
                if (!profile.endpoints.empty())
                    ip = QString::fromLatin1(profile.endpoints.front().ip.c_str());
                const QString name = profile.name.empty() ? ip : QString::fromStdString(profile.name);
                m_otherDevices.push_back({QStringLiteral("%1（%2）").arg(name, ip), id});
            }
        }
        addItem(QStringLiteral("其它设备…"), QString(), ModeOtherEntry);
    }

    static bool hasModbusEndpoint(const DeviceProfile& profile)
    {
        for (const auto& ep : profile.endpoints) {
            if (ep.protocol == "modbus") return true;
        }
        return false;
    }

    static QString modbusDeviceLabel(const DeviceProfile& profile)
    {
        QString endpoint = QStringLiteral("—");
        for (const auto& ep : profile.endpoints) {
            if (ep.protocol != "modbus") continue;
            endpoint = QString::fromLatin1(ep.ip.c_str()) + QLatin1Char(':') + QString::number(ep.port);
            break;
        }
        const QString name = profile.name.empty() ? endpoint : QString::fromStdString(profile.name);
        return QStringLiteral("%1（%2）").arg(name, endpoint);
    }

    void addItem(const QString& text, const QString& deviceId, int mode)
    {
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, deviceId);
        item->setData(Qt::UserRole + 1, mode);
    }

    void onItemClicked(QListWidgetItem* item)
    {
        if (!item) return;
        const int mode = item->data(Qt::UserRole + 1).toInt();
        const QString deviceId = item->data(Qt::UserRole).toString();
        if (mode == ModeDevice) {
            m_pick(deviceId, false);
            accept();
            return;
        }
        if (mode == ModeOtherEntry) {
            if (m_stage2) return;
            m_stage2 = true;
            m_list->clear();
            m_hint->setText(QStringLiteral("选择需要建立 modbus 端点的设备（默认端口 502）："));
            for (const LabelledId& entry : m_otherDevices) {
                addItem(entry.label, entry.deviceId, ModeCreateEntry);
            }
            return;
        }
        if (mode == ModeCreateEntry) {
            m_pick(deviceId, true);
            accept();
        }
    }

    // 无档案设备清单在构造时缓存（避免二级阶段再依赖 registry 指针）
    struct LabelledId { QString label; QString deviceId; };

    QLabel* m_hint = nullptr;
    QListWidget* m_list = nullptr;
    PickFn m_pick;
    bool m_stage2 = false;
    QVector<LabelledId> m_otherDevices;
};

} // namespace

// ── ModbusWidget ────────────────────────────────────────────────────

ModbusWidget::ModbusWidget(QWidget* parent) : ToolWidget(parent)
{
    setupUi();
    refreshTargets();
}

void ModbusWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("mbSplitter"));
    m_splitter->setChildrenCollapsible(false);

    // 左栏：连接清单（320~460px，规格 §4）
    auto* left = new QWidget(m_splitter);
    left->setMinimumWidth(320);
    left->setMaximumWidth(460);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    m_addTargetBtn = new QPushButton(QStringLiteral("＋ 添加目标"), left);
    m_addTargetBtn->setObjectName(QStringLiteral("mbAddTarget"));
    m_pauseBtn = new QPushButton(QStringLiteral("暂停轮询"), left);
    m_pauseBtn->setObjectName(QStringLiteral("mbPauseToggle"));
    m_pauseBtn->setCheckable(true);
    m_readOnceBtn = new QPushButton(QStringLiteral("立即读取"), left);
    m_readOnceBtn->setObjectName(QStringLiteral("mbReadOnce"));
    m_searchEdit = new QLineEdit(left);
    m_searchEdit->setObjectName(QStringLiteral("mbSearch"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索名称/IP"));
    toolbar->addWidget(m_addTargetBtn);
    toolbar->addWidget(m_pauseBtn);
    toolbar->addWidget(m_readOnceBtn);
    toolbar->addStretch();
    toolbar->addWidget(m_searchEdit);
    leftLayout->addLayout(toolbar);

    m_targetModel = new modbus::ModbusTargetListModel(&m_targetStore, &m_dataStore, this);
    m_targetView = new QTableView(left);
    m_targetView->setObjectName(QStringLiteral("mbTargetTable"));
    m_targetView->setModel(m_targetModel);
    m_targetView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_targetView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_targetView->verticalHeader()->hide();
    m_targetView->setAlternatingRowColors(true);
    m_targetView->setItemDelegateForColumn(
        modbus::ModbusTargetListModel::ColEndpoint,
        new MbEndpointPortDelegate(m_targetView));
    leftLayout->addWidget(m_targetView, 1);

    // 右栏：逐寄存器视图（唯一垂直扩展主体）
    auto* right = new QWidget(m_splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto* header = new QHBoxLayout();
    m_pointTitle = new QLabel(QStringLiteral("未选择目标"), right);
    m_pointTitle->setObjectName(QStringLiteral("mbPointTitle"));
    m_hexToggleBtn = new QPushButton(QStringLiteral("十六进制"), right);
    m_hexToggleBtn->setObjectName(QStringLiteral("mbHexToggle"));
    m_hexToggleBtn->setCheckable(true);
    header->addWidget(m_pointTitle);
    header->addStretch();
    header->addWidget(m_hexToggleBtn);
    rightLayout->addLayout(header);

    m_pointModel = new modbus::ModbusPointModel(this);
    m_pointView = new QTableView(right);
    m_pointView->setObjectName(QStringLiteral("mbPointTable"));
    m_pointView->setModel(m_pointModel);
    m_pointView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pointView->verticalHeader()->hide();
    m_pointView->setAlternatingRowColors(true);
    rightLayout->addWidget(m_pointView, 1);

    // 列宽：全部 Interactive + 合理初始宽（v2.11.1 ③教训入族规），末列 Stretch 铺满
    auto* th = m_targetView->horizontalHeader();
    const int targetWidths[] = {56, 110, 130, 48, 150, 80};
    for (int col = 0; col < modbus::ModbusTargetListModel::ColDetail; ++col) {
        th->setSectionResizeMode(col, QHeaderView::Interactive);
        m_targetView->setColumnWidth(col, targetWidths[col]);
    }
    th->setSectionResizeMode(modbus::ModbusTargetListModel::ColDetail, QHeaderView::Stretch);

    auto* ph = m_pointView->horizontalHeader();
    const int pointWidths[] = {80, 80, 96, 56};
    for (int col = 0; col < modbus::ModbusPointModel::ColUpdatedAt; ++col) {
        ph->setSectionResizeMode(col, QHeaderView::Interactive);
        m_pointView->setColumnWidth(col, pointWidths[col]);
    }
    ph->setSectionResizeMode(modbus::ModbusPointModel::ColUpdatedAt, QHeaderView::Stretch);
    // 十六进制列默认隐藏，经 mbHexToggle 切换（十进制 ↔ 十六进制呈现切换）
    m_pointView->setColumnHidden(modbus::ModbusPointModel::ColHex, true);

    m_splitter->addWidget(left);
    m_splitter->addWidget(right);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({380, 760});
    mainLayout->addWidget(m_splitter, 1);

    // 信号接线
    connect(m_addTargetBtn, &QPushButton::clicked, this, &ModbusWidget::onAddTargetClicked);
    connect(m_pauseBtn, &QPushButton::toggled, this, &ModbusWidget::onPauseToggled);
    connect(m_readOnceBtn, &QPushButton::clicked, this, &ModbusWidget::onReadOnceClicked);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_targetModel->setSearchText(text);
    });
    connect(m_hexToggleBtn, &QPushButton::toggled, this, [this](bool on) {
        m_pointModel->setHexMode(on);
        m_pointView->setColumnHidden(modbus::ModbusPointModel::ColDecimal, on);
        m_pointView->setColumnHidden(modbus::ModbusPointModel::ColHex, !on);
    });
    connect(m_targetView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ModbusWidget::onTargetSelectionChanged);
    connect(m_pointView, &QTableView::doubleClicked, this, &ModbusWidget::onPointDoubleClicked);
    connect(m_targetModel, &modbus::ModbusTargetListModel::portEditFailed,
            this, &ModbusWidget::onPortEditFailed);

    // 虚拟化：滚动条驱动模型可视区跟踪（不可见行只更新模型数据，不发 dataChanged 风暴）
    connect(m_pointView->verticalScrollBar(), &QScrollBar::valueChanged, m_pointView,
            [this, view = m_pointView, model = m_pointModel](int) {
                Q_UNUSED(view);
                updateVisibleRangeTracking(m_pointView, model);
            });
}

void ModbusWidget::updateVisibleRangeTracking(QTableView* view,
                                              modbus::ModbusPointModel* model)
{
    const int rowH = qMax(1, view->verticalHeader()->defaultSectionSize());
    const int first = view->verticalScrollBar()->value() / rowH;
    const int last = first + view->viewport()->height() / rowH + 1;
    model->setVisibleRange(first, last);
}

void ModbusWidget::setBackend(ModbusBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;
    // Backend 生命周期长于 Widget：std::function 回调以 QPointer 守卫防 UAF（长稳纪律）
    QPointer<ModbusWidget> self(this);
    m_backend->setLogCallback([self](const std::string& msg) {
        if (!self) return;
        const QString text = QString::fromStdString(msg);
        QMetaObject::invokeMethod(self, [self, text]() {
            if (self) self->appendLog(text);
        }, Qt::QueuedConnection);
    });
    m_backend->setResultCallback([self](const std::string& device, const QVector<quint16>& values,
                                        qint64 elapsedMs, const std::string& errorMsg) {
        if (!self) return;
        const QString ip = QString::fromStdString(device);
        const QString err = QString::fromStdString(errorMsg);
        const QVector<quint16> snapshot = values;
        QMetaObject::invokeMethod(self, [self, ip, snapshot, elapsedMs, err]() {
            if (self) self->handleBackendResult(ip, snapshot, elapsedMs, err);
        }, Qt::QueuedConnection);
    });
}

void ModbusWidget::setRegistry(DeviceRegistry* registry)
{
    m_registry = registry;
    m_targetModel->setRegistry(registry);  // 空指针安全：模型内部回退 device.list
    refreshTargets();
    if (m_targetModel->rowCount() > 0 && !m_targetView->selectionModel()->hasSelection())
        m_targetView->selectRow(0);
}

void ModbusWidget::onToolStart()
{
    appendLog(QStringLiteral("Modbus 测试工具已就绪"));
    refreshTargets();
}

void ModbusWidget::onToolStop()
{
    appendLog(QStringLiteral("Modbus 测试工具已停止"));
}

bool ModbusWidget::pollingPaused() const
{
    return m_pauseBtn && m_pauseBtn->isChecked();
}

void ModbusWidget::refreshTargets()
{
    QString keep;
    if (auto* row = m_targetModel->rowAt(m_targetView->selectionModel()->currentIndex().row()))
        keep = row->targetId;
    m_targetModel->refresh();
    if (keep.isEmpty()) return;
    const int row = m_targetModel->rowIndexOfTarget(keep);
    if (row >= 0) m_targetView->selectRow(row);
}

void ModbusWidget::onTargetSelectionChanged()
{
    const QModelIndex current = m_targetView->selectionModel()->currentIndex();
    const auto* row = m_targetModel->rowAt(current.row());
    if (!row) return;
    showTargetDetail(row->targetId);
}

void ModbusWidget::showTargetDetail(const QString& targetId)
{
    const auto* row = m_targetModel->rowByTargetId(targetId);
    if (!row) return;
    QVector<quint16> values;
    QString error;
    if (const auto s = m_dataStore.sample(targetId)) {
        values = s->values;
        error = s->error;
    }
    m_pointModel->setTarget(targetId, row->poll, values, values);
    m_pointModel->setSampleMeta(m_wallMsByTarget.value(targetId, 0), error);
    m_pointTitle->setText(QStringLiteral("%1 · 从站 %2 · %3")
        .arg(row->name).arg(row->slaveId)
        .arg(modbus::ModbusTargetListModel::rangeLabel(row->poll)));
    updateVisibleRangeTracking(m_pointView, m_pointModel);
}

void ModbusWidget::handleBackendResult(const QString& deviceIp, const QVector<quint16>& values,
                                       qint64 elapsedMs, const QString& error)
{
    // 临时归位（任务 4 换 targetId 精确交付）：旧广播读 API 只带设备 ip，
    // 按端点 ip 前缀匹配归属到清单行；同 ip 多从站时全部更新（与旧行为一致）。
    // 遍历过滤前全集：搜索过滤只隐藏视图行，未命中行的样本仍必须入 dataStore
    // （绑定约束"不可见行只更新 store"，视图信号由 syncSamples 自行按可见性抑制）
    const qint64 wall = QDateTime::currentMSecsSinceEpoch();
    bool touched = false;
    for (int i = 0; i < m_targetModel->allRowCount(); ++i) {
        const auto* row = m_targetModel->allRowAt(i);
        if (!row || row->endpoint == QStringLiteral("—")) continue;
        const int sep = row->endpoint.lastIndexOf(QLatin1Char(':'));
        if (sep <= 0 || row->endpoint.left(sep) != deviceIp) continue;
        const QString id = row->targetId;
        QVector<quint16> prev;
        if (const auto s = m_dataStore.sample(id)) prev = s->values;
        if (!error.isEmpty()) {
            m_dataStore.updateError(id, error, elapsedMs);  // 保留旧值（裁决 1 的 store 语义）
        } else if (!values.isEmpty()) {
            m_dataStore.updatePoll(id, values, elapsedMs);
        }
        // values 空且无错误 = 写成功回执：不更新数值
        m_wallMsByTarget.insert(id, wall);
        if (m_pointModel->targetId() == id) {
            const QVector<quint16>& cur = (error.isEmpty() && !values.isEmpty()) ? values : prev;
            m_pointModel->applyUpdate(prev, cur);
            m_pointModel->setSampleMeta(wall, error);
        }
        touched = true;
    }
    if (touched) m_targetModel->syncSamples();
}

void ModbusWidget::onAddTargetClicked()
{
    if (findChild<QDialog*>(QStringLiteral("mbAddTargetDialog"))) return;  // 防重复开窗
    auto* dlg = new AddTargetDialog(m_registry, this,
        [this](const QString& deviceId, bool createEndpoint) {
            ensureDefaultTarget(deviceId, createEndpoint);
        });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void ModbusWidget::ensureDefaultTarget(const QString& deviceId, bool createEndpoint)
{
    if (!m_registry) {
        appendLog(QStringLiteral("未接入设备档案，无法添加目标"));
        return;
    }
    auto profile = m_registry->find(deviceId.toStdString());
    if (!profile) {
        appendLog(QStringLiteral("设备档案缺失：%1").arg(deviceId));
        return;
    }
    if (createEndpoint) {
        bool hasModbus = false;
        for (const auto& ep : profile->endpoints) {
            if (ep.protocol == "modbus") hasModbus = true;
        }
        if (!hasModbus) {
            const std::string ip = profile->endpoints.empty()
                ? std::string("127.0.0.1") : profile->endpoints.front().ip;
            DeviceProfile updated = *profile;
            updated.endpoints.push_back({"modbus", ip, 502, std::string()});
            DeviceProfile stored;
            if (!m_registry->saveReplacing(updated, &stored)) {
                appendLog(QStringLiteral("端点写回档案失败：%1").arg(deviceId));
                return;
            }
            appendLog(QStringLiteral("已为 %1 建立默认 modbus 端点 %2:502")
                          .arg(deviceId, QString::fromLatin1(ip.c_str())));
        }
    }
    const auto ids = m_targetStore.allDeviceIds();
    modbus::DeviceTargets dt;
    dt.deviceId = deviceId.toStdString();
    const bool exists = std::find(ids.begin(), ids.end(), deviceId.toStdString()) != ids.end();
    if (exists) {
        if (!m_targetStore.load(deviceId.toStdString(), &dt)) {
            appendLog(QStringLiteral("目标记录读取失败：%1").arg(deviceId));
            return;
        }
        for (const auto& t : dt.targets) {
            if (t.slaveId == 1) {
                appendLog(QStringLiteral("该设备从站 1 目标已存在：%1").arg(deviceId));
                return;
            }
        }
    }
    dt.targets.push_back(modbus::SlaveTarget{});  // 默认从站 1：保持 40001×1 @1000ms
    if (!m_targetStore.save(dt)) {
        appendLog(QStringLiteral("目标保存失败：%1").arg(deviceId));
        return;
    }
    appendLog(QStringLiteral("已添加目标：%1 从站 1").arg(deviceId));
    refreshTargets();
}

void ModbusWidget::onPauseToggled(bool checked)
{
    m_pauseBtn->setText(checked ? QStringLiteral("恢复轮询") : QStringLiteral("暂停轮询"));
    appendLog(checked ? QStringLiteral("轮询已暂停") : QStringLiteral("轮询已恢复"));
    // 调度器驱动接线在任务 4：pollingPaused() 将被后端 tick 循环读取
}

void ModbusWidget::onReadOnceClicked()
{
    if (!m_backend) {
        appendLog(QStringLiteral("后端未就绪，无法读取"));
        return;
    }
    const auto* row = m_targetModel->rowAt(
        m_targetView->selectionModel()->currentIndex().row());
    if (!row || row->endpoint == QStringLiteral("—")) {
        appendLog(QStringLiteral("请先在左侧清单选择目标"));
        return;
    }
    syncDevicesToBackend();
    // 旧广播 API 的定向读：临时只绑目标所在端点，发起后恢复全量绑定
    const int sep = row->endpoint.lastIndexOf(QLatin1Char(':'));
    DeviceInfo target;
    target.ip = row->endpoint.left(sep).toStdString();
    target.port = row->endpoint.mid(sep + 1).toInt();
    target.note = row->name.toStdString();
    const auto full = m_backend->devices();
    m_backend->bindDevices({target});
    m_backend->readAllRegisters(row->slaveId, row->poll.regType,
                                row->poll.startAddr, row->poll.count);
    m_backend->bindDevices(full);
    appendLog(QStringLiteral("已对 %1 发起立即读取（%2）")
                  .arg(row->name, modbus::ModbusTargetListModel::rangeLabel(row->poll)));
}

void ModbusWidget::syncDevicesToBackend()
{
    if (!m_backend) return;
    std::vector<DeviceInfo> devices;
    if (m_registry) {
        // 档案模式：设备源 = 各档案 modbus 端点（ip:port 去重）
        std::vector<std::string> seen;
        for (const auto& profile : m_registry->list()) {
            for (const auto& ep : profile.endpoints) {
                if (ep.protocol != "modbus") continue;
                const std::string key = ep.ip + ":" + std::to_string(ep.port);
                if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
                seen.push_back(key);
                DeviceInfo info;
                info.ip = ep.ip;
                info.port = ep.port > 0 ? ep.port : 502;
                info.note = profile.name;
                devices.push_back(info);
            }
        }
    } else {
        // 回退模式：device.list 与设备栏持久化同源（沿用旧 UI 口径）
        const auto rows = ConfigStore::instance().list(QStringLiteral("device.list"), 20);
        for (const auto& row : rows) {
            DeviceInfo info;
            info.ip = row.value(QStringLiteral("ip")).toString().toStdString();
            info.port = row.value(QStringLiteral("port")).toInt();
            info.note = row.value(QStringLiteral("note")).toString().toStdString();
            if (!info.ip.empty()) devices.push_back(info);
        }
    }
    m_backend->bindDevices(devices);
}

void ModbusWidget::onPointDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    const int col = index.column();
    if (col != modbus::ModbusPointModel::ColDecimal
        && col != modbus::ModbusPointModel::ColHex) return;
    if (m_pointView->isColumnHidden(col)) return;
    const QString targetId = m_pointModel->targetId();
    const auto* row = m_targetModel->rowByTargetId(targetId);
    if (!row) {
        appendLog(QStringLiteral("请先在左侧清单选择目标"));
        return;
    }
    const modbus::PollSpec spec = row->poll;
    const bool coil = spec.regType == QModbusDataUnit::Coils;
    const int addr = spec.startAddr + index.row();
    const int currentValue = m_pointModel->data(
        m_pointModel->index(index.row(), modbus::ModbusPointModel::ColDecimal),
        Qt::DisplayRole).toInt();

    bool ok = false;
    const int maxValue = coil ? 1 : 65535;  // 线圈写 0/1（全局约束）
    const int v = QInputDialog::getInt(this,
        QStringLiteral("写入 %1").arg(modbus::addressLabel(spec.regType, addr)),
        coil ? QStringLiteral("值（线圈：0=断开，1=闭合）") : QStringLiteral("值:"),
        qBound(0, currentValue, maxValue), 0, maxValue, 1, &ok);
    if (!ok) return;
    const quint16 value = static_cast<quint16>(v);

    if (shouldConfirmWrite(targetId, addr, value) && !confirmWrite(targetId, addr, value)) {
        appendLog(QStringLiteral("写入已取消：%1 地址 %2").arg(targetId).arg(addr));
        return;
    }
    if (!m_backend) {
        appendLog(QStringLiteral("后端未就绪，写入未发出"));
        return;
    }
    syncDevicesToBackend();
    const int sep = row->endpoint.lastIndexOf(QLatin1Char(':'));
    const QString ip = sep > 0 ? row->endpoint.left(sep) : QString();
    if (ip.isEmpty()) {
        appendLog(QStringLiteral("目标端点缺失，写入无法定位设备：%1").arg(targetId));
        return;
    }
    m_backend->writeRegister(ip.toStdString(), row->slaveId, spec.regType, addr, value);
    appendLog(QStringLiteral("已发起写入: %1 从站%2 %3 地址=%4 值=%5")
                  .arg(targetId).arg(row->slaveId)
                  .arg(modbus::ModbusTargetListModel::registerTypeName(spec.regType))
                  .arg(addr).arg(value));
}

bool ModbusWidget::shouldConfirmWrite(const QString& targetId, int addr, quint16 value)
{
    Q_UNUSED(targetId);
    Q_UNUSED(addr);
    Q_UNUSED(value);
    // 保守方向：单点写改变 PLC 侧数据，恒需人工确认（同任务中心风险确认精神）
    return true;
}

bool ModbusWidget::confirmWrite(const QString& targetId, int addr, quint16 value)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("写入确认"),
                    QStringLiteral("即将向目标 %1 写入寄存器 %2，值 %3。\n"
                                   "该操作会改变 PLC 侧数据，请确认。")
                        .arg(targetId)
                        .arg(addr)
                        .arg(value),
                    QMessageBox::Ok | QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("mbWriteConfirm"));
    if (auto* okBtn = box.button(QMessageBox::Ok)) okBtn->setText(QStringLiteral("写入"));
    if (auto* cancelBtn = box.button(QMessageBox::Cancel)) cancelBtn->setText(QStringLiteral("取消"));
    box.setDefaultButton(QMessageBox::Cancel);  // 默认取消（全局约束）
    return box.exec() == QMessageBox::Ok;
}

void ModbusWidget::onPortEditFailed(const QString& message)
{
    appendLog(QStringLiteral("端口编辑失败：%1").arg(message));
}

void ModbusWidget::appendLog(const QString& msg)
{
    if (m_globalLogCb) {
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
        m_globalLogCb(QStringLiteral("[%1] %2").arg(ts, msg));
    }
}
