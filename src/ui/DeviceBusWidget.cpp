/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceBusWidget.cpp
 *
 * Date: 2026-07-25（2026-10 v2.11 Task 6：设备档案名称优先 + 档案编辑对话框）
 *
 * Author: turnarond
 *
 * Description: 设备总线 UI 组件实现 — 胶囊式紧凑布局 + DeviceRegistry 档案视图。
 */

#include "DeviceBusWidget.h"
#include "config/ConfigStore.h"
#include "config/DpapiCrypto.h"
#include "device/DeviceRegistry.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QEvent>
#include <QMenu>
#include <QPoint>
#include <QSet>
#include <QStyle>
#include <QComboBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDebug>

namespace devicebus {

QStringList editableEndpointProtocols()
{
    return {QStringLiteral("ftp"), QStringLiteral("ftps"), QStringLiteral("sftp"),
            QStringLiteral("ssh"), QStringLiteral("telnet"), QStringLiteral("modbus")};
}

QString storeFtpCredential(const QString& key, const QString& user, const QString& pass,
                           const QString& host, int port)
{
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty())
        return QString();
    const QString cipher = DpapiCrypto::protect(pass);
    if (!pass.isEmpty() && cipher.isEmpty()) {
        qWarning("DeviceBus: DPAPI 加密密码失败，跳过凭证保存以免清空已存密文");
        return QString();
    }
    QVariantMap cred;
    cred.insert(QStringLiteral("username"), user.trimmed());
    cred.insert(QStringLiteral("password"), cipher);
    cred.insert(QStringLiteral("updated_at"), QDateTime::currentMSecsSinceEpoch());
    const QString trimmedHost = host.trimmed();
    if (!trimmedHost.isEmpty()) {
        cred.insert(QStringLiteral("host"), trimmedHost);
        cred.insert(QStringLiteral("port"), port);
    }
    if (!ConfigStore::instance().save(QStringLiteral("ftp.credential"), trimmedKey, cred)) {
        qWarning("DeviceBus: 保存 ftp.credential 失败 key=%s", qPrintable(trimmedKey));
        return QString();
    }
    return trimmedKey;
}

} // namespace devicebus

namespace {

// v2.11.1 凭据下拉的两枚特殊项
const QString kCredNone = QStringLiteral("（无凭据）");
const QString kCredCreate = QStringLiteral("＋ 新建凭据…");

// 胶囊主文案：名称优先（v2.11），地址为次级信息；无名称退回纯地址（旧行为）
QString pillCaption(const QString& ipPart, const QString& name)
{
    if (!name.isEmpty() && name != ipPart)
        return QStringLiteral("%1 %2").arg(name, ipPart);
    return ipPart;
}

// 档案悬浮详情：逐端点 "协议 ip:port" + 备注（规格 §8：悬停可查看协议端点和备注）
QString profileToolTip(const DeviceProfile& profile)
{
    QStringList lines;
    if (!profile.name.empty())
        lines << QStringLiteral("\xE8\xAE\xBE\xE5\xA4\x87\xE5\x90\x8D\xE7\xA7\xB0: %1")
                     .arg(QString::fromStdString(profile.name));
    for (const auto& endpoint : profile.endpoints) {
        lines << QStringLiteral("%1 %2:%3")
                     .arg(QString::fromStdString(endpoint.protocol),
                          QString::fromStdString(endpoint.ip))
                     .arg(endpoint.port);
    }
    if (!profile.note.empty())
        lines << QStringLiteral("\xE5\xA4\x87\xE6\xB3\xA8: %1")
                     .arg(QString::fromStdString(profile.note));
    for (const auto& tag : profile.tags)
        lines << QStringLiteral("\xE6\xA0\x87\xE7\xAD\xBE: %1")
                     .arg(QString::fromStdString(tag));
    return lines.join(QStringLiteral("\n"));
}

// 档案编辑对话框 — 名称/备注/标签 + 协议端点表（协议/IP/端口/凭据引用）。
// 只编辑档案数据并经 DeviceRegistry 持久化；对话框本身不写库、不触碰密码明文
//（credentialRef 仅是 ConfigStore 凭证记录的键名）。
class DeviceProfileEditorDialog : public QDialog {
public:
    explicit DeviceProfileEditorDialog(const DeviceProfile& profile, QWidget* parent = nullptr)
        : QDialog(parent), m_original(profile)
    {
        setObjectName(QStringLiteral("deviceProfileEditor"));
        setWindowTitle(tr("设备档案"));
        resize(520, 360);

        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();

        m_nameEdit = new QLineEdit(this);
        m_nameEdit->setObjectName(QStringLiteral("deviceProfileName"));
        m_nameEdit->setText(QString::fromStdString(profile.name));
        m_nameEdit->setPlaceholderText(tr("留空时按地址展示"));
        form->addRow(tr("名称"), m_nameEdit);

        m_tagsEdit = new QLineEdit(this);
        m_tagsEdit->setObjectName(QStringLiteral("deviceProfileTags"));
        QStringList tags;
        for (const auto& tag : profile.tags)
            tags << QString::fromStdString(tag);
        m_tagsEdit->setText(tags.join(QStringLiteral(", ")));
        m_tagsEdit->setPlaceholderText(tr("逗号分隔，供模板按标签圈选设备"));
        form->addRow(tr("标签"), m_tagsEdit);

        m_noteEdit = new QLineEdit(this);
        m_noteEdit->setObjectName(QStringLiteral("deviceProfileNote"));
        m_noteEdit->setText(QString::fromStdString(profile.note));
        form->addRow(tr("备注"), m_noteEdit);
        layout->addLayout(form);

        // 端点表：同一物理设备的多协议端点共享名称（规格 §3）
        m_endpoints = new QTableWidget(this);
        m_endpoints->setObjectName(QStringLiteral("deviceProfileEndpoints"));
        m_endpoints->setColumnCount(4);
        m_endpoints->setHorizontalHeaderLabels(
            {tr("协议"), tr("主机"), tr("端口"), tr("凭据引用")});
        m_endpoints->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_endpoints->verticalHeader()->setVisible(false);
        for (const auto& endpoint : profile.endpoints) {
            appendEndpointRow(QString::fromStdString(endpoint.protocol),
                              QString::fromStdString(endpoint.ip),
                              QString::number(endpoint.port),
                              QString::fromStdString(endpoint.credentialRef));
        }
        if (m_endpoints->rowCount() == 0)
            appendEndpointRow({}, {}, {}, {});
        layout->addWidget(m_endpoints, 1);

        auto* toolRow = new QHBoxLayout();
        auto* addRowBtn = new QPushButton(tr("添加端点"), this);
        addRowBtn->setObjectName(QStringLiteral("deviceProfileAddEndpoint"));
        auto* removeRowBtn = new QPushButton(tr("删除端点"), this);
        removeRowBtn->setObjectName(QStringLiteral("deviceProfileRemoveEndpoint"));
        connect(addRowBtn, &QPushButton::clicked, this, [this] {
            appendEndpointRow({}, {}, {}, {});
        });
        connect(removeRowBtn, &QPushButton::clicked, this, [this] {
            const int row = m_endpoints->currentRow();
            if (row >= 0)
                m_endpoints->removeRow(row);
        });
        toolRow->addWidget(addRowBtn);
        toolRow->addWidget(removeRowBtn);
        toolRow->addStretch();
        layout->addLayout(toolRow);

        auto* buttons = new QDialogButtonBox(this);
        auto* saveBtn = buttons->addButton(tr("保存"), QDialogButtonBox::AcceptRole);
        saveBtn->setObjectName(QStringLiteral("deviceProfileSave"));
        buttons->addButton(tr("取消"), QDialogButtonBox::RejectRole);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    // 收集编辑结果；至少需要一个含主机的端点，否则返回 false 并提示
    bool collect(DeviceProfile* out)
    {
        DeviceProfile profile = m_original;
        profile.name = m_nameEdit->text().trimmed().toStdString();
        profile.note = m_noteEdit->text().trimmed().toStdString();
        profile.tags.clear();
        // Qt6 split 默认跳过空段（CI Qt 6.9.2 兼容，不用 Qt::SplitBehavior 弃用重载）
        const QStringList tags = m_tagsEdit->text().split(QLatin1Char(','));
        for (const QString& tag : tags) {
            const QString trimmed = tag.trimmed();
            if (!trimmed.isEmpty())
                profile.tags.push_back(trimmed.toStdString());
        }

        std::vector<DeviceEndpoint> endpoints;
        for (int row = 0; row < m_endpoints->rowCount(); ++row) {
            const auto cell = [this, row](int column) -> QString {
                QTableWidgetItem* item = m_endpoints->item(row, column);
                return item ? item->text().trimmed() : QString();
            };
            const auto comboCell = [this, row](int column) -> QComboBox* {
                return qobject_cast<QComboBox*>(m_endpoints->cellWidget(row, column));
            };
            const QString ip = cell(1);
            if (ip.isEmpty())
                continue;   // 空行忽略
            DeviceEndpoint endpoint;
            // 协议/凭据来自下拉：协议不可能再被留空成 ""（修复 v2.11 终审遗留）
            QComboBox* proto = comboCell(0);
            endpoint.protocol = (proto ? proto->currentText() : QStringLiteral("ftp"))
                                    .trimmed().toLower().toStdString();
            endpoint.ip = ip.toStdString();
            endpoint.port = cell(2).toInt();   // 0 → registry 按协议补默认端口
            const QString ref = comboCell(3) ? comboCell(3)->currentText().trimmed()
                                             : QString();
            if (!ref.isEmpty() && ref != kCredNone && ref != kCredCreate)
                endpoint.credentialRef = ref.toStdString();
            endpoints.push_back(endpoint);
        }
        if (endpoints.empty()) {
            QMessageBox::warning(this, tr("设备档案"),
                                 tr("至少需要一个包含主机地址的端点。"));
            return false;
        }
        profile.endpoints = std::move(endpoints);
        *out = profile;
        return true;
    }

private:
    void appendEndpointRow(const QString& protocol, const QString& ip,
                           const QString& port, const QString& credentialRef)
    {
        const int row = m_endpoints->rowCount();
        m_endpoints->insertRow(row);
        // 协议列：下拉选择，杜绝手打留空/拼错（v2.11.1）
        auto* protoCombo = new QComboBox(m_endpoints);
        protoCombo->setObjectName(QStringLiteral("endpointProtocolCombo"));
        const QStringList protocols = devicebus::editableEndpointProtocols();
        protoCombo->addItems(protocols);
        const QString wanted = protocol.trimmed().toLower();
        protoCombo->setCurrentIndex(qMax(0, protocols.indexOf(wanted))); // 未知/空 → ftp
        m_endpoints->setCellWidget(row, 0, protoCombo);
        m_endpoints->setItem(row, 1, new QTableWidgetItem(ip));
        m_endpoints->setItem(row, 2, new QTableWidgetItem(port));
        // 凭据引用列：下拉选择已有 ftp.credential 键，或经"新建凭据…"就地创建（DPAPI 加密）
        auto* credCombo = new QComboBox(m_endpoints);
        credCombo->setObjectName(QStringLiteral("endpointCredentialCombo"));
        populateCredentialCombo(credCombo, credentialRef.trimmed());
        connect(credCombo, QOverload<int>::of(&QComboBox::activated), this,
                [this, credCombo, row](int){ handleCredentialActivated(credCombo, row); });
        m_endpoints->setCellWidget(row, 3, credCombo);
    }

    void populateCredentialCombo(QComboBox* combo, const QString& current)
    {
        combo->blockSignals(true);
        combo->clear();
        combo->addItem(kCredNone);
        QStringList existing;
        const auto rows = ConfigStore::instance().list(QStringLiteral("ftp.credential"), 1000);
        for (const QVariantMap& r : rows) {
            const QString k = r.value(QStringLiteral("key")).toString();
            if (!k.isEmpty() && !existing.contains(k))
                existing << k;
        }
        existing.sort(Qt::CaseInsensitive);
        combo->addItems(existing);
        int idx = 0;
        if (!current.isEmpty()) {
            if (!existing.contains(current))
                combo->addItem(current);   // 引用可能来自旧配置：保底可见
            idx = qMax(1, combo->findText(current));
        }
        combo->addItem(kCredCreate);
        combo->setCurrentIndex(idx);
        combo->setProperty("lastRef", idx > 0 ? current : QString());
        combo->blockSignals(false);
    }

    void handleCredentialActivated(QComboBox* combo, int row)
    {
        const QString text = combo->currentText();
        if (text != kCredCreate) {
            combo->setProperty("lastRef", text);
            return;
        }
        // 选了"新建凭据…"：先回退显示，弹创建框，成功后切到新键
        const QString prev = combo->property("lastRef").toString();
        combo->blockSignals(true);
        combo->setCurrentText(prev.isEmpty() ? kCredNone : prev);
        combo->blockSignals(false);
        QString newKey;
        if (promptCreateCredential(row, &newKey)) {
            populateCredentialCombo(combo, newKey);
            combo->setProperty("lastRef", newKey);
        }
    }

    bool promptCreateCredential(int row, QString* outKey)
    {
        const auto cellText = [this, row](int col) -> QString {
            QTableWidgetItem* it = m_endpoints->item(row, col);
            return it ? it->text().trimmed() : QString();
        };
        const QString host = cellText(1);
        const int port = cellText(2).toInt();

        QDialog dlg(this);
        dlg.setObjectName(QStringLiteral("deviceCredentialCreate"));
        dlg.setWindowTitle(tr("新建凭据"));
        auto* lay = new QVBoxLayout(&dlg);
        auto* form = new QFormLayout();
        auto* keyEdit = new QLineEdit(&dlg);
        keyEdit->setObjectName(QStringLiteral("credentialKey"));
        keyEdit->setText(host.isEmpty()
                             ? QStringLiteral("user@主机:端口")
                             : QStringLiteral("user@%1:%2").arg(host)
                                   .arg(port > 0 ? port : 21));
        keyEdit->setPlaceholderText(tr("引用键名，如 root@192.168.20.123:21"));
        auto* userEdit = new QLineEdit(&dlg);
        userEdit->setObjectName(QStringLiteral("credentialUser"));
        auto* passEdit = new QLineEdit(&dlg);
        passEdit->setObjectName(QStringLiteral("credentialPass"));
        passEdit->setEchoMode(QLineEdit::Password);
        form->addRow(tr("引用键"), keyEdit);
        form->addRow(tr("用户名"), userEdit);
        form->addRow(tr("密码"), passEdit);
        lay->addLayout(form);
        auto* buttons = new QDialogButtonBox(&dlg);
        auto* okBtn = buttons->addButton(tr("保存"), QDialogButtonBox::AcceptRole);
        okBtn->setObjectName(QStringLiteral("credentialSave"));
        buttons->addButton(tr("取消"), QDialogButtonBox::RejectRole);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(buttons);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        const QString key = keyEdit->text().trimmed();
        if (key.isEmpty()) {
            QMessageBox::warning(this, tr("新建凭据"), tr("引用键不能为空。"));
            return false;
        }
        const QString saved =
            devicebus::storeFtpCredential(key, userEdit->text(), passEdit->text(), host, port);
        if (saved.isEmpty()) {
            QMessageBox::warning(this, tr("新建凭据"),
                                 tr("凭据保存失败（加密或写入未成功），已保持原引用。"));
            return false;
        }
        *outKey = saved;
        return true;
    }

    DeviceProfile m_original;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_tagsEdit = nullptr;
    QLineEdit* m_noteEdit = nullptr;
    QTableWidget* m_endpoints = nullptr;
};

} // namespace

DeviceBusWidget::DeviceBusWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("deviceBusContainer");
    setupUi();

    // 恢复最近一条 FTP/设备总线凭证（密码字段为 DPAPI base64 密文）
    const auto creds = ConfigStore::instance().list(QStringLiteral("ftp.credential"), 1);
    if (!creds.isEmpty()) {
        const QVariantMap& c = creds.first();
        if (m_userEdit)
            m_userEdit->setText(c.value(QStringLiteral("username")).toString());
        if (m_passEdit) {
            const QString cipher = c.value(QStringLiteral("password")).toString();
            if (!cipher.isEmpty()) {
                const QString plain = DpapiCrypto::unprotect(cipher);
                if (!plain.isEmpty() || cipher.isEmpty())
                    m_passEdit->setText(plain);
            }
        }
    }
}

void DeviceBusWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 2, 0, 2);
    mainLayout->setSpacing(4);

    // ── 第一行：设备胶囊 + 添加按钮 ──
    auto* deviceRow = new QHBoxLayout();
    deviceRow->setSpacing(4);

    auto* deviceIcon = new QLabel(QStringLiteral("\xF0\x9F\x93\xA1"), this);
    deviceIcon->setStyleSheet(QStringLiteral("font-size: 14px;"));
    deviceRow->addWidget(deviceIcon);

    // 设备胶囊容器（水平流式布局）
    m_pillContainer = new QWidget(this);
    m_pillLayout = new QHBoxLayout(m_pillContainer);
    m_pillLayout->setContentsMargins(0, 0, 0, 0);
    m_pillLayout->setSpacing(4);
    m_pillLayout->addStretch(); // 胶囊左对齐
    deviceRow->addWidget(m_pillContainer, 1);

    // + 添加按钮
    m_addButton = new QPushButton(QStringLiteral("+ \xE6\xB7\xBB\xE5\x8A\xA0"), this);
    m_addButton->setObjectName(QStringLiteral("btnAddDevice"));
    m_addButton->setFixedHeight(26);
    m_addButton->setCursor(Qt::PointingHandCursor);
    connect(m_addButton, &QPushButton::clicked, this, &DeviceBusWidget::onAddClicked);
    deviceRow->addWidget(m_addButton);

    mainLayout->addLayout(deviceRow);

    // ── 第二行：凭证输入 ──
    auto* authRow = new QHBoxLayout();
    authRow->setSpacing(6);

    auto* userIcon = new QLabel(QStringLiteral("\xF0\x9F\x91\xA4"), this);
    authRow->addWidget(userIcon);

    m_userEdit = new QLineEdit(this);
    m_userEdit->setObjectName(QStringLiteral("deviceAuthUser"));
    m_userEdit->setPlaceholderText(QStringLiteral("\xE7\x94\xA8\xE6\x88\xB7\xE5\x90\x8D"));
    m_userEdit->setFixedWidth(120);
    m_userEdit->setFixedHeight(24);
    authRow->addWidget(m_userEdit);

    auto* passIcon = new QLabel(QStringLiteral("\xF0\x9F\x94\x91"), this);
    authRow->addWidget(passIcon);

    m_passEdit = new QLineEdit(this);
    m_passEdit->setObjectName(QStringLiteral("deviceAuthPass"));
    m_passEdit->setPlaceholderText(QStringLiteral("\xE5\xAF\x86\xE7\xA0\x81"));
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setFixedWidth(140);
    m_passEdit->setFixedHeight(24);
    authRow->addWidget(m_passEdit);

    authRow->addStretch();

    mainLayout->addLayout(authRow);

    // 连接凭证变更信号
    connect(m_userEdit, &QLineEdit::textChanged, this, [this]() {
        emit credentialsChanged(m_userEdit->text(), m_passEdit->text());
    });
    connect(m_passEdit, &QLineEdit::textChanged, this, [this]() {
        emit credentialsChanged(m_userEdit->text(), m_passEdit->text());
    });

    // 失焦时隐式保存凭证（密码经 DPAPI 加密后入库；v2.11.1 复用 devicebus::storeFtpCredential）
    auto saveCreds = [this]() {
        const QString user = m_userEdit ? m_userEdit->text().trimmed() : QString();
        const QString pass = m_passEdit ? m_passEdit->text() : QString();
        if (user.isEmpty() && pass.isEmpty())
            return;
        const auto selected = selectedDevices();
        QString key = user.isEmpty() ? QStringLiteral("_default") : user;
        QString host;
        int port = 0;
        if (!selected.empty()) {
            const auto& d = selected.front();
            host = QString::fromStdString(d.ip);
            port = d.port > 0 ? d.port : 21;
            key = QStringLiteral("%1@%2:%3").arg(user.isEmpty() ? QStringLiteral("anon") : user,
                                                 host,
                                                 QString::number(port));
        }
        devicebus::storeFtpCredential(key, user, pass, host, port);
    };
    connect(m_userEdit, &QLineEdit::editingFinished, this, saveCreds);
    connect(m_passEdit, &QLineEdit::editingFinished, this, saveCreds);

    // ── 从 ConfigStore 恢复历史设备（旧 device.list 回退路径；
    //    接入 DeviceRegistry 后由 setRegistry → refreshFromRegistry 统一重建）──
    m_recentDevices = ConfigStore::instance().list(QStringLiteral("device.list"), 20);
    for (const QVariantMap& row : m_recentDevices) {
        DeviceInfo di;
        di.ip   = row.value(QStringLiteral("ip")).toString().toStdString();
        di.port = row.value(QStringLiteral("port")).toInt();
        const QString display = row.value(QStringLiteral("displayName")).toString();
        if (!display.isEmpty() && display != row.value(QStringLiteral("ip")).toString())
            di.alias = display.toStdString();
        di.note = row.value(QStringLiteral("note")).toString().toStdString();
        if (di.ip.empty())
            continue;
        addDevice(di, false);
    }
}

void DeviceBusWidget::setRegistry(DeviceRegistry* registry)
{
    m_registry = registry;
    refreshFromRegistry();
}

// 以注册表档案为唯一真相重建胶囊栏：名称优先、悬浮端点详情、双击编辑。
// 只读不写库（device.list 回退行由 addDevice/编辑路径显式保留）。
// 终审 Important 4：重建前快照选中档案 deviceId，重建后按 deviceId 恢复——
// 任何档案新增/编辑/删除都不得清空胶囊选中范围（FtpDeployWidget 等宿主把
// 空选中解释为「全部设备」，静默清空会放大单次部署的目标范围）。
void DeviceBusWidget::refreshFromRegistry()
{
    QSet<QString> selectedIds;
    for (auto* pill : m_pills) {
        if (pill->property("selected").toBool()) {
            const QString id = pill->property("deviceId").toString();
            if (!id.isEmpty())
                selectedIds.insert(id);
        }
    }

    // 摘除旧胶囊与设备视图
    for (auto* pill : m_pills) {
        m_pillLayout->removeWidget(pill);
        delete pill;
    }
    m_pills.clear();
    m_devices.clear();
    if (!m_registry)
        return;

    for (const auto& profile : m_registry->list()) {
        if (profile.endpoints.empty())
            continue;
        DeviceInfo di = toDeviceInfo(profile, profile.endpoints.front());
        di.alias = profile.name;   // 空名回落旧地址展示（toDeviceInfo 会填 ip:port 占位）
        m_devices.push_back(di);

        const QString ipPart = QStringLiteral("%1:%2")
            .arg(QString::fromStdString(di.ip))
            .arg(di.port);
        const QString deviceId = QString::fromStdString(profile.deviceId);
        QPushButton* pill = createPill(ipPart, QString::fromStdString(profile.name),
                                       deviceId, profileToolTip(profile));
        if (selectedIds.contains(deviceId))
            pill->setProperty("selected", true);   // 选中范围跨重建保持
        m_pillLayout->insertWidget(m_pillLayout->count() - 1, pill);
        m_pills.push_back(pill);
    }
    emit deviceSelectionChanged();
}

QPushButton* DeviceBusWidget::createPill(const QString& ipPart, const QString& name,
                                         const QString& deviceId, const QString& toolTip)
{
    auto* pill = new QPushButton(this);
    pill->setText(pillCaption(ipPart, name) + QStringLiteral("  \xC3\x97"));
    pill->setObjectName(QStringLiteral("devicePill"));
    pill->setFixedHeight(26);
    pill->setCursor(Qt::PointingHandCursor);
    // deviceIp 恒为纯 IP（removeDevice 与在线状态查询沿用旧键语义）
    pill->setProperty("deviceIp", ipPart.section(QLatin1Char(':'), 0, 0));
    pill->setProperty("selected", false);
    if (!deviceId.isEmpty())
        pill->setProperty("deviceId", deviceId);
    if (!toolTip.isEmpty())
        pill->setToolTip(toolTip);

    // 左键点击切换选中，右键点击删除设备，双击编辑档案
    pill->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(pill, &QPushButton::clicked, this, [this, pill]() {
        bool sel = !pill->property("selected").toBool();
        pill->setProperty("selected", sel);
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
        emit deviceSelectionChanged();
    });
    connect(pill, &QPushButton::customContextMenuRequested, this, [this, pill]() {
        QString ip = pill->property("deviceIp").toString();
        QMenu menu(this);
        QAction* editAction = nullptr;
        if (m_registry)
            editAction = menu.addAction(QStringLiteral("\xE7\xBC\x96\xE8\xBE\x91\xE6\xA1\xA3\xE6\xA1\x88"));
        QAction* removeAction = menu.addAction(QStringLiteral("\xE7\xA7\xBB\xE9\x99\xA4\xE8\xAE\xBE\xE5\xA4\x87"));
        QAction* chosen = menu.exec(pill->mapToGlobal(QPoint(0, pill->height())));
        if (chosen == editAction) {
            editProfileFor(pill);
            return;
        }
        if (!chosen)
            return;
        // 确认后删除（removeDevice 会同步移除 ConfigStore 持久化记录）
        auto ans = QMessageBox::question(this, "移除设备",
            QString("确定要移除设备 \"%1\" 吗？").arg(ip));
        if (ans == QMessageBox::Yes) {
            removeDevice(ip);
        }
    });
    pill->installEventFilter(this);
    return pill;
}

bool DeviceBusWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick && m_registry) {
        if (auto* pill = qobject_cast<QPushButton*>(watched)) {
            if (pill->objectName() == QStringLiteral("devicePill")) {
                editProfileFor(pill);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// 双击/右键编辑入口：对话框确认 → registry->save 持久化 device.profile →
// 同步旧 device.list ip:port 行（回退兼容）→ 以档案视图重建胶囊 → 发射信号。
void DeviceBusWidget::editProfileFor(QPushButton* pill)
{
    if (!m_registry || !pill)
        return;
    const QString deviceId = pill->property("deviceId").toString();
    DeviceProfile profile;
    if (!deviceId.isEmpty()) {
        const auto found = m_registry->find(deviceId.toStdString());
        if (!found)
            return;
        profile = *found;
    } else {
        // 旧 device.list 胶囊（未接入注册表前添加）：由 DeviceInfo 合成档案
        const QString ip = pill->property("deviceIp").toString();
        for (const auto& device : m_devices) {
            if (QString::fromStdString(device.ip) == ip) {
                profile = fromDeviceInfo(device);
                break;
            }
        }
    }

    DeviceProfileEditorDialog dialog(profile, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    DeviceProfile edited;
    if (!dialog.collect(&edited))
        return;

    // 终审 Critical 1：编辑器收集的是「替换清单」——经 saveReplacing 以替换语义
    // 落库（未列出的既有端点删除 + 被取代旧 device.list 行清扫），
    // 并集合并仅保留给协议发现路径（addDevice/fromDeviceInfo）
    DeviceProfile saved;
    if (!m_registry->saveReplacing(edited, &saved))
        qWarning("DeviceBus: 替换保存 device.profile 失败 deviceId=%s",
                 qPrintable(QString::fromStdString(saved.deviceId)));
    if (!saved.endpoints.empty())
        persistLegacyRow(toDeviceInfo(saved, saved.endpoints.front()));
    refreshFromRegistry();
    emit deviceProfileEdited(saved);
}

void DeviceBusWidget::persistLegacyRow(const DeviceInfo& device)
{
    const QString ipStr = QString::fromStdString(device.ip);
    QVariantMap dev;
    dev.insert(QStringLiteral("ip"), ipStr);
    dev.insert(QStringLiteral("port"), device.port);
    dev.insert(QStringLiteral("displayName"),
               device.alias.empty() ? ipStr : QString::fromStdString(device.alias));
    dev.insert(QStringLiteral("note"), QString::fromStdString(device.note));
    dev.insert(QStringLiteral("updated_at"), QDateTime::currentMSecsSinceEpoch());
    const QString key = QStringLiteral("%1:%2").arg(ipStr).arg(device.port);
    if (!ConfigStore::instance().save(QStringLiteral("device.list"), key, dev))
        qWarning("DeviceBus: 保存 device.list 失败 key=%s", qPrintable(key));
}

void DeviceBusWidget::addDevice(const DeviceInfo& device, bool persist)
{
    if (m_registry) {
        // v2.11：注册表模式下所有新增统一走档案（device.profile），
        // 同时保留旧 device.list ip:port 行写入，回退读取路径不受影响。
        if (!persist)
            return;   // 启动缓存路径（persist=false）由 refreshFromRegistry 重建
        DeviceProfile synthesized = fromDeviceInfo(device);
        const DeviceProfile saved = m_registry->upsert(synthesized);
        if (!m_registry->save(saved))
            qWarning("DeviceBus: 保存 device.profile 失败（addDevice 回退旧行仍写入）");
        persistLegacyRow(device);
        refreshFromRegistry();
        emit deviceProfileEdited(saved);
        return;
    }

    // ── 旧行为（未接入注册表）──
    // 检查重复
    for (const auto& d : m_devices) {
        if (d.ip == device.ip) return;
    }
    m_devices.push_back(device);

    QString ipStr = QString::fromStdString(device.ip);
    QString ipPart = ipStr;
    if (device.port > 0 && device.port != 21) {
        ipPart += QStringLiteral(":") + QString::number(device.port);
    }
    const QString name = QString::fromStdString(device.alias);
    QString toolTip;
    if (!device.note.empty())
        toolTip = QString::fromStdString(device.note);
    QPushButton* pill = createPill(ipPart, name, QString(), toolTip);

    // 插入到 stretch 之前
    m_pillLayout->insertWidget(m_pillLayout->count() - 1, pill);
    m_pills.push_back(pill);

    if (persist)
        persistLegacyRow(device);

    emit deviceSelectionChanged();
}

void DeviceBusWidget::removeDevice(const QString& ip)
{
    int port = 0;
    QString deviceId;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].ip == ip.toStdString()) {
            port = m_devices[i].port;
            m_devices.erase(m_devices.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    for (size_t i = 0; i < m_pills.size(); ++i) {
        if (m_pills[i]->property("deviceIp").toString() == ip) {
            deviceId = m_pills[i]->property("deviceId").toString();
            m_pillLayout->removeWidget(m_pills[i]);
            delete m_pills[i];
            m_pills.erase(m_pills.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    // v2.11：档案模式下经注册表删除（registry 同时清理匹配端点的旧 device.list 行，
    // 保证对迁移设备删除持久）
    if (m_registry && !deviceId.isEmpty()) {
        m_registry->remove(deviceId.toStdString());
        refreshFromRegistry();
        emit deviceSelectionChanged();
        return;
    }
    // 同步删除持久化记录
    ConfigStore::instance().remove(
        QStringLiteral("device.list"),
        QStringLiteral("%1:%2").arg(ip).arg(port));
    emit deviceSelectionChanged();
}

std::vector<DeviceInfo> DeviceBusWidget::selectedDevices() const
{
    std::vector<DeviceInfo> result;
    size_t n = std::min(m_devices.size(), m_pills.size());
    for (size_t i = 0; i < n; ++i) {
        if (m_pills[i]->property("selected").toBool()) {
            result.push_back(m_devices[i]);
        }
    }
    return result;
}

std::vector<DeviceProfile> DeviceBusWidget::selectedDeviceProfiles() const
{
    std::vector<DeviceProfile> result;
    size_t n = std::min(m_devices.size(), m_pills.size());
    for (size_t i = 0; i < n; ++i) {
        if (!m_pills[i]->property("selected").toBool())
            continue;
        const QString deviceId = m_pills[i]->property("deviceId").toString();
        if (m_registry && !deviceId.isEmpty()) {
            if (const auto found = m_registry->find(deviceId.toStdString()))
                result.push_back(*found);
        } else {
            result.push_back(fromDeviceInfo(m_devices[i]));
        }
    }
    return result;
}

std::vector<DeviceInfo> DeviceBusWidget::allDevices() const
{
    return m_devices;
}

void DeviceBusWidget::setOnlineStatus(const QString& ip, bool online)
{
    for (auto* pill : m_pills) {
        if (pill->property("deviceIp").toString() == ip) {
            pill->setProperty("online", online);
            pill->style()->unpolish(pill);
            pill->style()->polish(pill);
            return;
        }
    }
}

void DeviceBusWidget::onAddClicked()
{
    if (m_registry) {
        // v2.11：档案模式 → 打开空档案编辑对话框（端点必填其一）
        DeviceProfileEditorDialog dialog(DeviceProfile{}, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        DeviceProfile edited;
        if (!dialog.collect(&edited))
            return;
        const DeviceProfile saved = m_registry->upsert(edited);
        if (!m_registry->save(saved))
            qWarning("DeviceBus: 保存 device.profile 失败 deviceId=%s",
                     qPrintable(QString::fromStdString(saved.deviceId)));
        if (!saved.endpoints.empty())
            persistLegacyRow(toDeviceInfo(saved, saved.endpoints.front()));
        refreshFromRegistry();
        emit deviceProfileEdited(saved);
        return;
    }

    bool ok;
    QString text = QInputDialog::getText(this, tr("添加设备"),
        tr("设备 IP 地址（例: 192.168.1.100）"),
        QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty()) {
        DeviceInfo di;
        di.ip   = text.trimmed().toStdString();
        di.port = 0;
        addDevice(di);
    }
}

QString DeviceBusWidget::user() const
{
    return m_userEdit ? m_userEdit->text().trimmed() : QString();
}

QString DeviceBusWidget::password() const
{
    return m_passEdit ? m_passEdit->text().trimmed() : QString();
}
