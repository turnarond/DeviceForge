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
 * Description: 设备总线 UI 组件实现 — 只显已选 chips + 「设备 ▾」分组菜单收纳
 *              （v2.16-B 任务4）+ DeviceRegistry 档案视图。
 */

#include "DeviceBusWidget.h"
#include "config/ConfigStore.h"
#include "config/DpapiCrypto.h"
#include "device/DeviceBusGroups.h"
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
#include <QFontMetricsF>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QSet>
#include <QStyle>
#include <QStyleOptionButton>
#include <QComboBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>

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

// v2.16-B Step6 胶囊主文案（规格 §2.3 重画）：只显名称、不带 ip:port；
// 空名必须回落地址（否则栏里出空白胶囊，比长更糟）；重名才追加 ·ip 短尾标。
QString pillCaption(const QString& ipPart, const QString& name, bool duplicateName)
{
    if (name.isEmpty())
        return ipPart;
    if (duplicateName)
        return QStringLiteral("%1 ·%2").arg(name, ipPart.section(QLatin1Char(':'), 0, 0));
    return name;
}

// v2.16-B 修复轮1（规格 §2.3 订正本）：胶囊尾部「×」尾标单源——文案拼接与
// 命中区宽度计算都读这一处。它自本版本起不再是纯视觉语言而是**点击目标**：
// 一次 release 落在尾部「×」区 ⇒ 移出选择；落在主体 ⇒ 不改选择（主体只服务
// 双击与右键）。字面 "  \xC3\x97" ＝两个半角空格 + U+00D7 乘号（UTF-8）。
const QString kPillCloseMark = QStringLiteral("  \xC3\x97");

// 胶囊显示文本（含尾部 ×——修复轮1 起为「移出选择」的真命中区，见 kPillCloseMark）
QString pillText(const QString& ipPart, const QString& name, bool duplicateName)
{
    return pillCaption(ipPart, name, duplicateName) + kPillCloseMark;
}

// 重名判定集合：判据与 DeviceRegistry::rebuildWarnings（非空原始名出现 >1 次）
// 同源。每次重建/每次开菜单算一次，逐胶囊查表，不在胶囊构造里重复扫全表。
QSet<QString> duplicateNameSet(const QStringList& names)
{
    QMap<QString, int> counts;
    for (const QString& name : names) {
        if (!name.isEmpty())
            ++counts[name];
    }
    QSet<QString> duplicates;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > 1)
            duplicates.insert(it.key());
    }
    return duplicates;
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
        // v2.16-B §4：对话框切分改用规范化真源（纯分隔符标签如 "/" 按 §2.1 判空不入库）
        // split 的单参形态在 CI 的 Qt 6.9.2 与本地 6.11.1 默认值一致，都是 Qt::KeepEmptyParts
        // （实读 qstring.h:708）⇒ 空段靠下面 normalizeTag 判空丢弃，本行不依赖 split 的默认值。
        // 这行注释曾被写成"Qt6 split 默认跳过空段"，那是误记、已证伪；别按那个口径改写本调用。
        const QStringList rawTags = m_tagsEdit->text().split(QLatin1Char(','));
        for (const QString& raw : rawTags) {
            const QString normalized = devicebus::normalizeTag(raw);
            if (!normalized.isEmpty())
                profile.tags.push_back(normalized.toStdString());
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

    // ── 第一行（v2.16-B 任务4 重画形态，规格 §2.3 / 视觉稿状态①-④）：
    //    图标 → chips 宿主（只显已选，视觉态只切可见性）→ +M 台 → 空态提示
    //    → 「设备 ▾」入口 → 行尾搜索框 → + 添加 ──
    auto* deviceRow = new QHBoxLayout();
    deviceRow->setSpacing(4);

    auto* deviceIcon = new QLabel(QStringLiteral("\xF0\x9F\x93\xA1"), this);
    deviceIcon->setStyleSheet(QStringLiteral("font-size: 14px;"));
    deviceRow->addWidget(deviceIcon);

    // 设备胶囊容器（水平流式布局）：一台设备一枚胶囊与 m_devices 平行；
    // chips 形态下"只显示已选"由 rebuildChips()/applyPillOverflow() 切可见性兑现
    m_pillContainer = new QWidget(this);
    m_pillContainer->setObjectName(QStringLiteral("selectedPillHost"));
    m_pillLayout = new QHBoxLayout(m_pillContainer);
    m_pillLayout->setContentsMargins(0, 0, 0, 0);
    m_pillLayout->setSpacing(4);
    m_pillLayout->addStretch(); // 胶囊左对齐
    deviceRow->addWidget(m_pillContainer, 1);

    // 溢出折算按钮：+M 台（M>0 才显示）；挂"同一个"菜单实例（不是复制品）
    m_moreBtn = new QPushButton(QStringLiteral("+0 台"), this);
    m_moreBtn->setObjectName(QStringLiteral("devicePillMore"));
    m_moreBtn->setFixedHeight(26);
    m_moreBtn->hide();
    deviceRow->addWidget(m_moreBtn);

    // 空态提示（规格 §3.5：未选择任何设备＝没有目标）
    m_hintLabel = new QLabel(QStringLiteral("未选择设备——点「设备 ▾」选目标"), this);
    m_hintLabel->setObjectName(QStringLiteral("deviceBusHint"));
    deviceRow->addWidget(m_hintLabel);

    // 「设备 ▾」入口：QPushButton::setMenu()（setPopupMode 是 QToolButton 成员，
    // QPushButton 没有——实读本地 qpushbutton.h 零命中，别照抄工具按钮写法）。
    // 菜单必须每次 aboutToShow 重建，否则勾选态停在第一次构建的样子（Step 4）。
    m_pickerBtn = new QPushButton(QStringLiteral("设备 ▾ 未选择"), this);
    m_pickerBtn->setObjectName(QStringLiteral("devicePickerBtn"));
    m_pickerBtn->setFixedHeight(26);
    m_pickerMenu = new QMenu(this);
    m_pickerBtn->setMenu(m_pickerMenu);
    m_moreBtn->setMenu(m_pickerMenu);
    connect(m_pickerMenu, &QMenu::aboutToShow, this, [this]() {
        m_pickerMenu->clear();                                   // 每次重建
        buildPickerMenuInto(m_pickerMenu);
    });
    connect(m_pickerMenu, &QMenu::aboutToHide, this, [this]() {
        if (!m_pendingSelectionBroadcast) return;
        m_pendingSelectionBroadcast = false;
        rebuildChips();
        emit deviceSelectionChanged();           // 勾 5 台 = 广播 1 次（用例断计数==1）
    });
    deviceRow->addWidget(m_pickerBtn);

    // 行尾搜索框：过滤"下一次打开菜单时的可见项"（不实时重开菜单，规格 §0.3）；
    // chips 显示的是已选集合，过滤全程不动已选（§3.4）
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("deviceSearchEdit"));
    m_searchEdit->setFixedHeight(26);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索 名称 / IP / 标签"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filterText = text;
    });
    deviceRow->addWidget(m_searchEdit);

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

// ── v2.16-B 任务4 Step2：选中态显式 id 集合（规格 §2.4——chips 形态下
// m_devices[i] ↔ m_pills[i] 的位置配对从机制上不能再作选中真相源）──
QString DeviceBusWidget::makeSelectionKey(const QString& deviceId, const QString& ip)
{
    return deviceId.isEmpty()
        ? QStringLiteral("ip:%1").arg(ip)          // 旧行无持久 id → ip 键
        : QStringLiteral("id:%1").arg(deviceId);
}

QString DeviceBusWidget::selectionKey(const QPushButton* pill) const
{
    return makeSelectionKey(pill->property("deviceId").toString(),
                            pill->property("deviceIp").toString());
}

QString DeviceBusWidget::keyForDevice(size_t index) const
{
    // 修复轮1 finding5：m_deviceIds ↔ m_devices 是 lockstep 平行视图；用 .at() 而非
    // operator[]，未来若新增变更点漏同步 → std::out_of_range 响亮失败（Release 下
    // Q_ASSERT 被编译掉、min() 守卫只会掩盖分叉，两者都不读越界内存也不装无事发生）。
    return makeSelectionKey(m_deviceIds.at(index),
                            QString::fromStdString(m_devices.at(index).ip));
}

QString DeviceBusWidget::ipPartForDevice(size_t index) const
{
    const DeviceInfo& d = m_devices[index];
    const QString ip = QString::fromStdString(d.ip);
    if (m_registry)
        return QStringLiteral("%1:%2").arg(ip).arg(d.port);   // 档案模式恒带端口
    return (d.port > 0 && d.port != 21)
        ? ip + QStringLiteral(":") + QString::number(d.port)  // 旧行为（addDevice 同式）
        : ip;
}

QVector<DeviceBusWidget::BusEntry> DeviceBusWidget::collectEntries() const
{
    QVector<BusEntry> entries;
    entries.reserve(static_cast<int>(m_devices.size()));
    for (size_t i = 0; i < m_devices.size(); ++i) {
        BusEntry entry;
        entry.deviceId = m_deviceIds.at(i);  // 修复轮1 finding5：lockstep 分叉即响亮失败
        entry.name = QString::fromStdString(m_devices[i].alias);
        entry.ipPart = ipPartForDevice(i);
        entry.key = keyForDevice(i);
        if (m_registry && !entry.deviceId.isEmpty()) {
            if (const auto found = m_registry->find(entry.deviceId.toStdString()))
                entry.profile = *found;
        }
        if (entry.profile.endpoints.empty())
            entry.profile = fromDeviceInfo(m_devices[i]);    // 旧行合成（editProfileFor 同口径）
        entry.toolTip = profileToolTip(entry.profile);
        entries.push_back(entry);
    }
    return entries;
}

// 以注册表档案为唯一真相重建胶囊栏：名称优先、悬浮端点详情、双击编辑。
// 只读不写库（device.list 回退行由 addDevice/编辑路径显式保留）。
// 终审 Important 4：重建不得清空选中——v2.16-B Step2 起选中态真源是显式
// m_selectedIds 集合，重建只需按当前档案集清理"已消失设备"的键（按 deviceId
// 稳定的键跨新增/编辑保持；FtpDeployWidget 等宿主依赖"重建不清选择"语义）。
void DeviceBusWidget::refreshFromRegistry()
{
    // 摘除旧胶囊与设备视图
    for (auto* pill : m_pills) {
        m_pillLayout->removeWidget(pill);
        delete pill;
    }
    m_pills.clear();
    m_devices.clear();
    m_deviceIds.clear();
    if (!m_registry) {
        m_selectedIds.clear();   // 注册表解除：旧视图整体让位给 addDevice 路径，集合从零开始
        emit deviceSelectionChanged();
        return;
    }

    QSet<QString> validKeys;
    for (const auto& profile : m_registry->list()) {
        if (profile.endpoints.empty())
            continue;
        DeviceInfo di = toDeviceInfo(profile, profile.endpoints.front());
        di.alias = profile.name;   // 空名回落旧地址展示（toDeviceInfo 会填 ip:port 占位）
        m_devices.push_back(di);
        const QString deviceId = QString::fromStdString(profile.deviceId);
        m_deviceIds.push_back(deviceId);
        const QString key = makeSelectionKey(deviceId, QString::fromStdString(di.ip));
        validKeys.insert(key);

        const QString ipPart = QStringLiteral("%1:%2")
            .arg(QString::fromStdString(di.ip))
            .arg(di.port);
        QPushButton* pill = createPill(ipPart, QString::fromStdString(profile.name),
                                       deviceId, profileToolTip(profile));
        m_pillLayout->insertWidget(m_pillLayout->count() - 1, pill);
        m_pills.push_back(pill);
    }
    m_selectedIds.intersect(validKeys);   // 只清已消失设备的键，在册设备的选择原样保留
    rebuildChips();                       // 选中范围跨重建保持（chips 可见性同步）
    emit deviceSelectionChanged();
}

QPushButton* DeviceBusWidget::createPill(const QString& ipPart, const QString& name,
                                         const QString& deviceId, const QString& toolTip)
{
    auto* pill = new QPushButton(this);
    // 初始文案先按"非重名"落笔；rebuildChips() 每次重建时按重名集合单源修正
    pill->setText(pillText(ipPart, name, false));
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

    // v2.16-B Step5/修复轮1：chip 的存在本身即选中。左键语义已拆分（规格 §2.3
    // 订正本）——尾部「×」命中区＝移出选择，主体＝不改选择（只服务双击与右键，
    // 双击"打开这台设备"因此在真鼠标下可达）；键式与菜单勾选共用 selectionKey
    //（Step2 单源）。
    // v2.16-B 任务5：右键三项「打开这台设备／编辑档案／移除设备」（规格 §2.3/§6②，
    // 首项是自解释器——老用户双击发现行为变了，右键一看就知道新含义叫什么、老含义
    // 搬去了哪）；双击的档案编辑角色已让位给「打开这台设备」（eventFilter，编辑
    // 能力零丢失）。跳转编排（openDeviceAt 接 openDeviceRequested）归任务 6。
    // 构建与分派都收口在 buildPillMenu/dispatchPillMenuAction 单源方法——
    // 测试经同一对方法驱动（exec 真弹层自动化在本环境证伪，见任务5报告）。
    pill->setContextMenuPolicy(Qt::CustomContextMenu);
    // v2.16-B 修复轮1（规格 §2.3 订正本/§10 第 8 条）：移出选择自"整枚按钮的
    // clicked"收窄到尾部「×」命中区——真鼠标的双击第一下 release 若仍移出并隐藏
    // 胶囊（clicked→m_selectedIds.remove→rebuildChips 同步 setVisible(false)），
    // 第二下 MouseButtonDblClick 永远落不到隐藏件上，"打开这台设备"对真实用户
    // 不可达。鼠标路径由 eventFilter 在 release 上打标记、此处同步消费：主体
    // 单击⇒标记存在且不在区⇒不改选择；区单击⇒移出。键盘 Space/Enter 与程序化
    // click() 无前置鼠标事件⇒标记为空⇒一律移出（可达性不丢）。
    connect(pill, &QPushButton::clicked, this, [this, pill]() {
        if (m_chipReleasePill == pill) {
            const bool inCloseZone = m_chipReleaseInCloseZone;
            m_chipReleasePill = nullptr;         // 标记只服务这一次 clicked
            if (!inCloseZone)
                return;                          // 主体单击：选择不变（规格 §5 ①）
        }
        m_selectedIds.remove(selectionKey(pill));
        rebuildChips();
        emit deviceSelectionChanged();
    });
    connect(pill, &QPushButton::customContextMenuRequested, this, [this, pill]() {
        QMenu* menu = buildPillMenu();
        QAction* chosen = menu->exec(pill->mapToGlobal(QPoint(0, pill->height())));
        const QString role = chosen
            ? chosen->property("pillMenuRole").toString()
            : QString();                     // 弹层被撤销＝chosen 空＝静默（同旧行为）
        menu->deleteLater();
        dispatchPillMenuAction(pill, role);
    });
    pill->installEventFilter(this);
    return pill;
}

// 右键弹层唯一构建真源：三项按「打开这台设备／编辑档案／移除设备」次序返回；
// 「编辑档案」显示门槛是 m_registry（§3.1 旧行胶囊在生产形态三项齐全）；
// 每项以 pillMenuRole 属性携带分派键，菜单所有权归调用方（exec 用完即弃）。
// 三项内容与具体胶囊无关（ip 在分派侧按属性回查），故无入参——不留死形参。
QMenu* DeviceBusWidget::buildPillMenu()
{
    auto* menu = new QMenu(this);
    QAction* openAction = menu->addAction(QStringLiteral("打开这台设备"));
    openAction->setProperty("pillMenuRole", QStringLiteral("open"));
    if (m_registry) {
        QAction* editAction = menu->addAction(QStringLiteral("编辑档案"));
        editAction->setProperty("pillMenuRole", QStringLiteral("edit"));
    }
    QAction* removeAction = menu->addAction(QStringLiteral("移除设备"));
    removeAction->setProperty("pillMenuRole", QStringLiteral("remove"));
    return menu;
}

// exec 返回值与测试直调共用的分派函数。role 为空＝用户撤销弹层，静默。
void DeviceBusWidget::dispatchPillMenuAction(QPushButton* pill, const QString& role)
{
    if (role == QLatin1String("open")) {
        if (const auto profile = profileForPill(pill))
            emit openDeviceRequested(*profile);
        return;
    }
    if (role == QLatin1String("edit")) {
        editProfileFor(pill);
        return;
    }
    if (role != QLatin1String("remove"))
        return;
    // 确认后删除（removeDevice 会同步移除 ConfigStore 持久化记录）
    const QString ip = pill->property("deviceIp").toString();
    auto ans = QMessageBox::question(this, "移除设备",
        QString("确定要移除设备 \"%1\" 吗？").arg(ip));
    if (ans == QMessageBox::Yes) {
        removeDevice(ip);
    }
}

// chip 尾部「×」命中区宽度（规格 §2.3 订正本：宽度判据"算出来，不许拍脑袋"）：
// QFontMetricsF 按胶囊**自身 font** 实量尾标 "  ×" 的字宽，向上取整，再加样式层
// 的按钮内部 padding（PM_ButtonMargin），下限 0 兜异常样式。口径限制如实登记（两处
// 都是实测结论，别照抄旧说法）：① QSS 的 `font-size` **会**灌进 `QWidget::font()`
// （任务 7 修复轮实测：应用默认 Microsoft YaHei UI 9pt，而胶囊 `font()` 已是
// `#devicePill` 的 12px——本函数此前记的"覆盖不进"是错的，已订正）；② 区宽随样式层
// 而变：默认样式下 `PM_ButtonMargin`=6 ⇒ 22px，生产 QSS 下为 0 ⇒ 16px。两者都只影响
// 区的**左边界**位置（区恒贴按钮右缘、注入判据几何独立），不影响「×」可达性。
int DeviceBusWidget::pillCloseZoneWidth(const QPushButton* pill)
{
    const QFontMetricsF fm(pill->font());
    const int markWidth = int(std::ceil(fm.horizontalAdvance(kPillCloseMark)));
    QStyleOptionButton opt;
    opt.initFrom(pill);
    const int padding =
        qMax(0, pill->style()->pixelMetric(QStyle::PM_ButtonMargin, &opt, pill));
    return markWidth + padding;
}

bool DeviceBusWidget::pillCloseZoneContains(const QPushButton* pill, const QPoint& pos)
{
    return pos.x() >= pill->width() - pillCloseZoneWidth(pill);
}

bool DeviceBusWidget::eventFilter(QObject* watched, QEvent* event)
{
    // v2.16-B 修复轮1：clicked 的鼠标/键盘分道标记在此维护（协议见成员注释）。
    // 顺序保证：事件过滤器先于 QAbstractButton 收到事件；release 直落本胶囊
    // 且在 rect 内时先置标记，随后按钮同步 emit clicked 消费；press/dblclick/
    // keypress 一律先清空——这挡掉的是**跨激活残留**：任何一次激活的 clicked 之前
    // 必有 press 或 key 事件到达本过滤器并清空旧标记（终审项10 措辞订正：原写
    // "杜绝跨激活残留"过头）。如实登记的例外：真双击的尾部 release 会重新置位标记
    // 且没有 clicked 消费它（DblClick 已被吞、按钮无 down 态）——该残留在下一次
    // press/key 时被清，真实键鼠路径读不到；唯一可被读到的误用是紧随真双击后对
    // **同一枚**胶囊的程序化 click()，会被读成主体单击而不移出选择。
    if (auto* pill = qobject_cast<QPushButton*>(watched)) {
        if (pill->objectName() == QStringLiteral("devicePill")) {
            switch (event->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            case QEvent::KeyPress:
                m_chipReleasePill = nullptr;
                break;
            case QEvent::MouseButtonRelease: {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton &&
                    pill->rect().contains(me->position().toPoint())) {
                    m_chipReleasePill = pill;
                    m_chipReleaseInCloseZone = pillCloseZoneContains(
                        pill, me->position().toPoint());
                }
                break;
            }
            default:
                break;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick && m_registry) {
        // 终审项2（2026-10-21，fix-before-merge）：双击只认**左键**——本分支此前不判
        // button()，右键双击会既发射 openDeviceRequested（误触本版主打动作）又照常弹
        // 出右键菜单（同函数 switch 的 MouseButtonRelease 臂判了左键、这里没判，两臂
        // 口径不一致）。非左键的 DblClick 不吞事件，回落默认分发，右键语义只归右键
        // 菜单一条路。
        auto* dbl = static_cast<QMouseEvent*>(event);
        if (dbl->button() == Qt::LeftButton) {
            if (auto* pill = qobject_cast<QPushButton*>(watched)) {
                if (pill->objectName() == QStringLiteral("devicePill")) {
                    // v2.16-B 任务5（规格 §2.3/§6②）：双击＝「打开这台设备」。
                    // 原 editProfileFor 角色让位给右键菜单第二项；本函数吞掉双击
                    // 事件（return true），QAbstractButton 收不到事件就不会补发
                    // clicked——chips 选中态不受双击影响。选中态净零的基线来自改动前
                    // HEAD eaf7bfc 的取证（旧 eventFilter 亦是 editProfileFor(pill);
                    // return true 吞同一事件，QTest::mouseDClick 只投单个 DblClick 不
                    // 经 press/release，clicked 恒为 0；三个已提交编辑槽先勾中、双击后
                    // 不再重勾即完成编辑，反证选中跨双击未变），并非在改后代码上重测旧
                    // 行为。新语义向前由 doubleClickEmitsTwoClickedAndKeepsSelection
                    // 实钉（clicked=0/广播=0/仍选中/恰好发一次 openDeviceRequested）。
                    if (const auto profile = profileForPill(pill))
                        emit openDeviceRequested(*profile);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// 胶囊→档案装配唯一单源（v2.16-B 任务5 从 editProfileFor 装配段抽出——双击、
// 右键「打开这台设备」、右键「编辑档案」三条路径共用，不再复制装配代码）：
// 两条路径口径一致，查不到一律返回空 optional——有持久 deviceId 走 registry->find
//（find 未命中即 nullopt）；旧 device.list ip:port 胶囊按纯 IP 回查 m_devices 经
// fromDeviceInfo 合成（端点身份即 ip:port，口径与 selectedDeviceProfiles()/
// collectEntries() 一致），m_devices 里查不到同样返回 nullopt（不发空档案）。
std::optional<DeviceProfile> DeviceBusWidget::profileForPill(const QPushButton* pill) const
{
    if (!pill)
        return std::nullopt;
    const QString deviceId = pill->property("deviceId").toString();
    if (!deviceId.isEmpty()) {
        if (!m_registry)
            return std::nullopt;
        const auto found = m_registry->find(deviceId.toStdString());
        if (!found)
            return std::nullopt;
        return *found;
    }
    // 旧 device.list 胶囊（未接入注册表前添加）：由 DeviceInfo 合成档案
    const QString ip = pill->property("deviceIp").toString();
    for (const auto& device : m_devices) {
        if (QString::fromStdString(device.ip) == ip)
            return fromDeviceInfo(device);
    }
    // 查不到＝档案不可用（ip 不在设备全集，如陈旧/错配的胶囊）：与 deviceId 路径
    // 一致返回空 optional，调用方各自早退——绝不发默认构造的空档案（否则任务 6 会
    // 把 ip 为空、无端点的载荷路由到 Page::None，用户双击却收到「无可用端点」提示）
    return std::nullopt;
}

// 右键菜单「编辑档案」入口：对话框确认 → registry->save 持久化 device.profile →
// 同步旧 device.list ip:port 行（回退兼容）→ 以档案视图重建胶囊 → 发射信号。
// v2.16-B 任务5：档案装配段改走 profileForPill 单源（原双击入口已让位）。
void DeviceBusWidget::editProfileFor(QPushButton* pill)
{
    if (!m_registry || !pill)
        return;
    const auto assembled = profileForPill(pill);
    if (!assembled)
        return;
    const DeviceProfile profile = *assembled;

    DeviceProfileEditorDialog dialog(profile, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    DeviceProfile edited;
    // #46：同键冲突默认拒绝，用户放弃时回到对话框继续改，不丢编辑内容
    for (;;) {
        if (!dialog.collect(&edited))
            return;
        if (confirmEndpointConflicts(edited))
            break;
        if (dialog.exec() != QDialog::Accepted)
            return;
    }

    // 终审 Critical 1：编辑器收集的是「替换清单」——经 saveReplacing 以替换语义
    // 落库（未列出的既有端点删除 + 被取代旧 device.list 行清扫），
    // 并集合并仅保留给协议发现路径（addDevice/fromDeviceInfo）
    DeviceProfile saved;
    if (!m_registry->saveReplacing(edited, &saved)) {
        // v2.14 #49-7：落盘失败——注册表已回滚内存、stored 未被填充；
        // 告警措辞就实取入参档案（不读 stored），不写 legacy 行、
        // 不广播 deviceProfileEdited，仅刷新回真内存态后早退
        qWarning("DeviceBus: 替换保存 device.profile 失败 name=%s deviceId=%s",
                 qPrintable(QString::fromStdString(edited.name)),
                 qPrintable(QString::fromStdString(edited.deviceId)));
        refreshFromRegistry();
        return;
    }
    if (!saved.endpoints.empty())
        persistLegacyRow(toDeviceInfo(saved, saved.endpoints.front()));
    refreshFromRegistry();
    emit deviceProfileEdited(saved);
}

// #46：保存入口源端同键校验——与他档同协议同键（协议+ip+port）端点重复时弹确认，
// 可保存但需确认（默认"否"）；返回 false 表示用户放弃本次保存。
bool DeviceBusWidget::confirmEndpointConflicts(const DeviceProfile& edited)
{
    if (!m_registry)
        return true;
    const auto conflicts = m_registry->endpointConflicts(edited);
    if (conflicts.empty())
        return true;
    QStringList lines;
    for (const auto& conflict : conflicts)
        lines << QStringLiteral("・") + QString::fromStdString(conflict);
    const auto ans = QMessageBox::question(this, tr("端点同键重复"),
        tr("检测到以下端点与既有档案的同协议端点重复：\n\n%1\n\n"
           "同地址重复档案实为同一目标：部署清目录会互相踩、任务按档案串行时会重复操作同一设备。\n"
           "仍要保存吗？").arg(lines.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return ans == QMessageBox::Yes;
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
    m_deviceIds.push_back(QString());   // 旧行无持久 deviceId → ip 键

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

    rebuildChips();   // 新设备未选 → 先收起（chips 只显已选），可见性由集合统一判定
    emit deviceSelectionChanged();
}

void DeviceBusWidget::removeDevice(const QString& ip)
{
    int port = 0;
    QString deviceId;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].ip == ip.toStdString()) {
            port = m_devices[i].port;
            m_selectedIds.remove(keyForDevice(i));   // v2.16-B Step2：设备消失即出集合
            m_devices.erase(m_devices.begin() + static_cast<ptrdiff_t>(i));
            m_deviceIds.erase(m_deviceIds.begin() + static_cast<ptrdiff_t>(i));
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
    rebuildChips();   // 视图/文案同步（被删胶囊已摘除，其余可见性不变）
    emit deviceSelectionChanged();
}

std::vector<DeviceInfo> DeviceBusWidget::selectedDevices() const
{
    // v2.16-B Step2：按显式集合从 m_devices 取，不再遍历 m_pills 的位置配对
    std::vector<DeviceInfo> result;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_selectedIds.contains(keyForDevice(i)))
            result.push_back(m_devices[i]);
    }
    return result;
}

std::vector<DeviceProfile> DeviceBusWidget::selectedDeviceProfiles() const
{
    std::vector<DeviceProfile> result;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (!m_selectedIds.contains(keyForDevice(i)))
            continue;
        const QString deviceId = m_deviceIds.at(i);   // 终审项6：lockstep 视图统一 .at() 响亮失败（同 :625/:646）
        if (m_registry && !deviceId.isEmpty()) {
            if (const auto found = m_registry->find(deviceId.toStdString())) {
                result.push_back(*found);
                continue;
            }
        }
        result.push_back(fromDeviceInfo(m_devices[i]));
    }
    return result;
}

std::vector<DeviceInfo> DeviceBusWidget::allDevices() const
{
    return m_devices;
}

// v2.16-B 任务4 Step2：菜单底部两条动作（真实实现；Step4 弹层按钮接同一缝）。
// selectAllVisible 作用"当前过滤可见"的设备——可见集由 segments() 复算（与
// 菜单构建同一条纯头真源；过滤词为空时可见=全集）。
void DeviceBusWidget::selectAllVisible()
{
    const QVector<BusEntry> entries = collectEntries();
    std::vector<DeviceProfile> profiles;
    profiles.reserve(entries.size());
    for (const auto& entry : entries)
        profiles.push_back(entry.profile);   // 下标载体同源同序（规格 §2.1 定档）

    const QList<devicebus::Segment> segments = devicebus::segments(profiles, m_filterText);
    for (const auto& segment : segments) {
        for (const int index : segment.profileIndexes)
            m_selectedIds.insert(entries[index].key);
    }
    // 修复轮1 finding1：本场菜单会话若先勾过成员（pending），底部动作的即时
    // 广播已覆盖整场——必须吃掉 pending，否则 aboutToHide 会补发第二次广播，
    // 违反"一场会话一次广播"（规格 §3.8）。
    m_pendingSelectionBroadcast = false;
    rebuildChips();
    emit deviceSelectionChanged();
}

void DeviceBusWidget::clearSelection()
{
    // 规格 §2.3（2026-10-21 订正本）：清空是逃生门——清的是**整个**选中集合，
    // 不受当前过滤可见集限制（过滤态下只清可见的会留下一堆看不见的选中项
    // 继续当部署目标）；范围由菜单项文案自述（buildPickerMenuInto）。
    m_selectedIds.clear();
    m_pendingSelectionBroadcast = false;     // 同 selectAllVisible：即时广播覆盖整场
    rebuildChips();
    emit deviceSelectionChanged();
}

// ── v2.16-B 任务4 Step4：「设备 ▾」弹层构建（分组即子菜单，勾选即选中）──
// 每次 aboutToShow 清空重建：成本 = 一次 segments()；不持久化、不记忆展开态（§2.3）。
// 成员 checked == 该设备在 m_selectedIds 里的成员判定——跨组一致性由集合兜住。
void DeviceBusWidget::buildPickerMenuInto(QMenu* menu)
{
    const QVector<BusEntry> entries = collectEntries();
    std::vector<DeviceProfile> profiles;
    profiles.reserve(entries.size());
    for (const auto& entry : entries)
        profiles.push_back(entry.profile);   // 下标载体硬约束：与 entries 同源同序（§2.1 定档）

    const QList<devicebus::Segment> segs = devicebus::segments(profiles, m_filterText);
    // 终审项1（2026-10-21）：「全选可见 N 台」的 N = **按选中键去重后的台数**——多标签
    // 设备在多个分组各出现一次，逐组求和（seg.profileIndexes.size() 直加）会把同一台
    // 计进两组（80 台×2 标签 ⇒ 标 ~160、点完只入 80，标出数与实选数分叉）。去重键与
    // m_selectedIds 同一个 selectionKey 口径（规格 §2.4），也与手册「选中集合里只算
    // 一台」同源；selectAllVisible() 本就经集合插进去重，标出 N 与其实际补入台数恒等。
    QSet<QString> visibleKeys;
    for (const auto& seg : segs) {              // 零命中组已由纯头排除，UI 不再判空
        for (const int index : seg.profileIndexes)
            visibleKeys.insert(entries[index].key);
    }
    const int visibleTotal = visibleKeys.size();

    // 菜单项与 chips 同一文案真源（重名 ·ip 尾标，规格 §3.6）；每次开菜单算一次
    QStringList allNames;
    allNames.reserve(entries.size());
    for (const auto& entry : entries)
        allNames.append(entry.name);
    const QSet<QString> duplicates = duplicateNameSet(allNames);

    for (const auto& seg : segs) {
        QMenu* sub = menu->addMenu(QStringLiteral("%1 %2/%3")
                                       .arg(seg.tag)
                                       .arg(seg.profileIndexes.size())
                                       .arg(seg.total));
        sub->setProperty("deviceGroup", seg.tag);
        for (const int index : seg.profileIndexes) {
            const BusEntry& entry = entries[index];
            const QString text = pillCaption(entry.ipPart, entry.name,
                                             duplicates.contains(entry.name));
            QAction* act = sub->addAction(text);
            act->setCheckable(true);
            act->setProperty("deviceKey", entry.key);
            act->setToolTip(entry.toolTip);      // 逐端点 协议 ip:port + 备注（profileToolTip）
            const QString key = entry.key;
            // 初始勾选态必须先于接线回灌：构建期 setChecked 不得计入 toggled 风暴
            act->setChecked(m_selectedIds.contains(key));
            connect(act, &QAction::toggled, this, [this, key](bool on) {
                if (on)
                    m_selectedIds.insert(key);
                else
                    m_selectedIds.remove(key);
                m_pendingSelectionBroadcast = true;   // 由 aboutToHide 统一广播一次
            });
        }
    }
    if (visibleTotal == 0) {
        // 规格 §3.4：全不命中只留一行不可点说明，底部两动作一并撤下
        QAction* none = menu->addAction(QStringLiteral("无匹配设备（%1 台）")
                                            .arg(static_cast<int>(entries.size())));
        none->setEnabled(false);
        return;
    }
    menu->addSeparator();
    QAction* selectAll = menu->addAction(QStringLiteral("全选可见 %1 台").arg(visibleTotal));
    connect(selectAll, &QAction::triggered, this, &DeviceBusWidget::selectAllVisible);
    // 修复轮1 finding3（规格 §2.3 订正本）：两条底部动作作用域刻意不对称——过滤生效时
    // 清空项文案必须自述范围（M=已选总数，含被过滤不可见的已选），否则用户会以为漏清；
    // 无过滤时保持朴素「清空选择」。
    // 终审项8（2026-10-21）：已选为空时范围说明没有可自述的对象——过滤态+零已选曾渲染
    // 字面「清空选择（全部 0 台）」，读起来像漏写；回退朴素文案（M>0 才带范围后缀）。
    const QString clearText = (m_filterText.isEmpty() || m_selectedIds.isEmpty())
        ? QStringLiteral("清空选择")
        : QStringLiteral("清空选择（全部 %1 台）").arg(static_cast<int>(m_selectedIds.size()));
    menu->addAction(clearText, this, &DeviceBusWidget::clearSelection);
}

void DeviceBusWidget::rebuildChips()
{
    // v2.16-B Step5：chips 视图同步——胶囊仍一台一枚与 m_devices 平行存在，
    // "只显已选"与溢出收纳都只切可见性（视觉态不得删数据，规格 §2.3 +
    // 本仓"少挂胶囊"事故教训：数据集合与视图必须分层）。
    if (!m_pickerBtn || !m_hintLabel)
        return;
    // Step6：文案（含重名 ·ip 尾标）在每次重建时单源计算一次，逐胶囊查表
    const QVector<BusEntry> entries = collectEntries();
    QStringList allNames;
    allNames.reserve(entries.size());
    for (const auto& entry : entries)
        allNames.append(entry.name);
    const QSet<QString> duplicates = duplicateNameSet(allNames);

    const size_t n = std::min(m_devices.size(), m_pills.size());
    for (size_t i = 0; i < n; ++i) {
        const bool sel = m_selectedIds.contains(keyForDevice(i));
        QPushButton* pill = m_pills[i];
        const QString text = pillText(entries[i].ipPart, entries[i].name,
                                      duplicates.contains(entries[i].name));
        if (pill->text() != text)
            pill->setText(text);
        if (pill->property("selected").toBool() != sel) {
            pill->setProperty("selected", sel);         // 样式镜像（QSS/后续 eventFilter 消费）
            pill->style()->unpolish(pill);
            pill->style()->polish(pill);
        }
    }
    m_hintLabel->setVisible(m_selectedIds.isEmpty());
    m_pickerBtn->setText(m_selectedIds.isEmpty()
        ? QStringLiteral("设备 ▾ 未选择")
        : QStringLiteral("设备 ▾ 已选 %1 台").arg(static_cast<int>(m_selectedIds.size())));
    applyPillOverflow();
}

int DeviceBusWidget::visiblePillBudget() const
{
    // 行宽预算 = 设备栏自身宽度 − 同行固定控件（可见者）的 sizeHint 宽度。
    // 刻意不读 chips 宿主子控件的 width()：窗口未显示/布局瞬态时子控件宽度不可靠
    //（实踩：隐藏窗口容器 100px 假预算把测试上下文折成全收起；show 时 0 宽瞬变
    // 又反向放行全部，撑爆布局最小宽）。守恒用例只判数量不变，不判具体枚数
    //（tests/AGENTS.md 约定 6）。预算 ≤0（极窄）时全部折进 +M 台，是合法收纳态。
    if (!m_pickerBtn)
        return std::numeric_limits<int>::max();   // setupUi 前（不可能有胶囊）= 永不折叠
    const auto widthOf = [](const QWidget* w) {
        return (w && !w->isHidden()) ? w->sizeHint().width() : 0;
    };
    int overhead = 40;   // 图标 + 布局 margin/spacing 的保守预留
    // 修复轮1 finding6：m_moreBtn 不进本预算——它的占位宽度改由 applyPillOverflow
    // 在"确认要折叠"时预留。若按当前可见性计入，预算随按钮上屏/收起摆动，
    // 首折瞬间整行恰好超出一个按钮宽（折叠抖动）。
    overhead += widthOf(m_hintLabel) + widthOf(m_pickerBtn)
              + widthOf(m_searchEdit) + widthOf(m_addButton);
    return width() - overhead;
}

void DeviceBusWidget::applyPillOverflow()
{
    if (!m_moreBtn || m_pills.empty())
        return;
    const int spacing = 4;
    const size_t n = std::min(m_devices.size(), m_pills.size());
    struct Fold { int shown = 0; int folded = 0; int selectedTotal = 0; };
    // 单遍折算：未选→收起；已选按预算从尾部起折进 +M 台。可见性一律重设
    //（两遍调用幂等，守恒 shown+folded==selectedTotal 由循环构造保证）。
    auto foldPass = [&](int budget) -> Fold {
        Fold f;
        int used = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!m_selectedIds.contains(keyForDevice(i))) {
                m_pills[i]->setVisible(false);          // 未选 → 收起（chips 只显已选）
                continue;
            }
            ++f.selectedTotal;
            const int w = m_pills[i]->sizeHint().width();
            const int cost = (f.shown > 0 ? spacing : 0) + w;
            if (used + cost > budget) {
                m_pills[i]->setVisible(false);          // 从尾部起折进 +M 台
                ++f.folded;
            } else {
                m_pills[i]->setVisible(true);
                used += cost;
                ++f.shown;
            }
        }
        return f;
    };
    int budget = visiblePillBudget();
    Fold f = foldPass(budget);
    // 修复轮1 finding6：折叠一旦发生，+M 台按钮上屏占据行宽——预算必须预留它的
    // sizeHint（按钮带来的一个 spacing 槽由 overhead 的 40px 保守预留覆盖），并复算一遍。
    // 第二遍预算只会更小 → folded 不减、shown 不多，按钮可见性判定与两遍结果一致
    //（folded>0 ⇔ 按钮可见），无振荡；守恒断言（tests：shown+folded==选中总数）不依
    // 赖具体枚数（tests/AGENTS.md 约定 6）。
    if (f.folded > 0) {
        budget -= m_moreBtn->sizeHint().width();
        f = foldPass(budget);
    }
    m_moreBtn->setVisible(f.folded > 0);
    m_moreBtn->setText(QStringLiteral("+%1 台").arg(f.folded));   // 守恒：shown + folded == selectedTotal
    // 终审项3（2026-10-21）：resizeEvent 每帧重算，此前每次 resize 都打一条 qInfo
    // ⇒ 拖窗口=逐帧刷屏用户可见日志面板。改为**状态变化才记一行**（shown/folded/
    // selected 三元组比对上次留档）——折叠态迁移本身是信息，同态重排是纯噪声；
    // 不选"降级 qDebug"是因为 qInfo/qDebug 都经 LogBridge 进 lwlog、面板可见性由
    // 级别过滤决定，本组件不可控——门控与级别配置无关，是更稳的口径。消息文本与
    // 字段一字不改（tests/deploy/tst_deploy_toolbar_layout 各量测槽读的是自己的
    // qInfo 打点，不消费本条，已先行核过）。
    if (f.shown != m_lastOverflowShown || f.folded != m_lastOverflowFolded
        || f.selectedTotal != m_lastOverflowSelected) {
        qInfo("DeviceBus chips overflow: budget=%d shown=%d folded=%d selected=%d",
              budget, f.shown, f.folded, f.selectedTotal);
        m_lastOverflowShown = f.shown;
        m_lastOverflowFolded = f.folded;
        m_lastOverflowSelected = f.selectedTotal;
    }
}

void DeviceBusWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    applyPillOverflow();
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
        DeviceProfile saved;
        for (;;) {
            DeviceProfile edited;
            if (!dialog.collect(&edited))
                return;
            if (confirmEndpointConflicts(edited)) {
                saved = m_registry->upsert(edited);
                break;
            }
            if (dialog.exec() != QDialog::Accepted)
                return;
        }
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
