/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: DeviceBusWidget.h
 *
 * Date: 2026-07-25（2026-10 v2.11 Task 6：设备档案名称优先展示 + 档案编辑对话框；
 *       2026-10-20 v2.16-B 任务4：只显已选 chips + 分组菜单收纳；
 *       2026-10-21 v2.16-B 任务5：双击改判「打开这台设备」+ 右键三项置顶入口）
 *
 * Author: turnarond
 *
 * Description: 设备总线 UI 组件 — 设备栏第 1 行只显「已选 chips」+「设备 ▾」
 *              分组菜单收纳（v2.16-B 重画形态），支持添加/删除/选中，
 *              凭证输入。工业仪表盘风格。
 *              v2.11：接入 DeviceRegistry 后胶囊名称优先（地址为次级信息），
 *              悬浮显示协议端点与备注。v2.16-B 任务5：双击改判「打开这台设备」
 *              （发射 openDeviceRequested，按端点协议跳页的编排归任务 6），
 *              档案编辑退到右键菜单第二项（保存仍经 registry->saveReplacing
 *              以端点替换语义持久化并发射 deviceProfileEdited，能力零丢失）；
 *              未接入注册表时保持旧 ip:port 行为与 device.list 回退写入。
 *              v2.16-B 任务4：选中态改为显式 deviceId 集合（位置配对废弃）；
 *              chips 只显名称（重名才加 ·ip 尾标、空名回落地址）；胶囊仍一台
 *              一枚与全集平行创建销毁——"只显已选"与溢出收纳只切可见性，
 *              allDevices() 恒为全集（批量命令页目标集硬线）；菜单每次
 *              aboutToShow 重建，组项「组名 命中/总数」，「未分组」最后，
 *              底部「全选可见 N 台」（只作用可见集）与「清空选择」（清全量，
 *              过滤态文案自述范围「清空选择（全部 M 台）」），全不命中时两动作
 *              撤下；溢出折成「+M 台」并与入口共用同一菜单实例。
 */

#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <QList>
#include <QStringList>
#include <QVector>
#include <optional>
#include <vector>
#include "device/DeviceProfile.h"
#include "framework/DeviceInfo.h"

// v2.11.1 设备编辑器可用性：协议枚举与凭据持久化对测试与内联新建对话框复用
namespace devicebus {

// 端点编辑允许的协议集合（小写；对齐引擎路由：ftp/ftps/sftp/ssh/telnet/modbus）
QStringList editableEndpointProtocols();

// 凭据入库：密码经 DPAPI 加密后写 ftp.credential；成功返回 key，失败返回空串。
// 密码明文只存在于调用栈内（写前即加密，失败不覆盖已存密文）。
QString storeFtpCredential(const QString& key, const QString& user, const QString& pass,
                           const QString& host, int port);

} // namespace devicebus

class QHBoxLayout;
class QLabel;
class QMenu;
class QResizeEvent;
class DeviceRegistry;

class DeviceBusWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeviceBusWidget(QWidget* parent = nullptr);

    // 设备列表操作
    // persist=false：从 ConfigStore 恢复时只更新 UI，避免启动时反复写库
    void addDevice(const DeviceInfo& device, bool persist = true);
    void removeDevice(const QString& ip);
    std::vector<DeviceInfo> selectedDevices() const;
    std::vector<DeviceInfo> allDevices() const;
    void setOnlineStatus(const QString& ip, bool online);

    // v2.11 Task 6：设备档案注册表（可选）。设置后立即以档案重建胶囊栏；
    // 添加/编辑/删除均经 registry 持久化，同时保留旧 device.list ip:port 行写入
    //（供回退读取的旧路径）。
    void setRegistry(DeviceRegistry* registry);
    // 选中胶囊对应的设备档案：有注册表时按稳定 deviceId 回查；无注册表时
    // 由旧 DeviceInfo 合成（端点身份即 ip:port）。
    std::vector<DeviceProfile> selectedDeviceProfiles() const;

    // v2.16-B 任务4：选中态显式集合（只读；键式见 selectionKey —— id 优先，
    // 旧行退化为 ip 键）。chips/菜单/测试共用同一真相源。
    QSet<QString> selectedIds() const { return m_selectedIds; }
    // 菜单底部两条公开动作（与弹层内按钮同一条路径）：
    // selectAllVisible —— 按当前过滤词把「可见」设备并入选中集合（全选可见 N 台）
    // clearSelection  —— 清空整个选中集合，不受过滤可见集限制（规格 §2.3 订正本：
    // 清空是逃生门；过滤生效时菜单项文案自述范围「清空选择（全部 M 台）」）。
    // 两者即时广播并吃掉 m_pendingSelectionBroadcast——同一场菜单会话只广播一次。
    void selectAllVisible();
    void clearSelection();

    // 凭证访问
    QString user() const;
    QString password() const;

    // 历史设备列表（ConfigStore device.list，按 updated_at DESC；启动时缓存）
    QList<QVariantMap> historyDevices() const { return m_recentDevices; }

signals:
    void deviceSelectionChanged();
    void credentialsChanged(const QString& user, const QString& password);
    // 档案新增/编辑保存成功后发射（宿主 Tool 可即时刷新设备视图）
    void deviceProfileEdited(const DeviceProfile& profile);
    // v2.16-B 任务5（规格 §2.2/§2.4 纯加法）：双击 chips 或右键「打开这台设备」
    // 发射。载荷=胶囊对应的档案（旧行经 profileForPill 的 fromDeviceInfo 合成）。
    // 本组件只发射、不切页；DeviceForge::openDeviceAt 经 deviceopen::routeTo
    // 按端点协议跳对应工具页并处理 Page::None 提示（任务 6 接线）。
    void openDeviceRequested(const DeviceProfile& profile);

protected:
    // v2.16-B 任务5：双击胶囊＝「打开这台设备」——吞掉双击事件并发射
    // openDeviceRequested(profileForPill(pill))（仅接入注册表后启用；原
    // 「打开档案编辑对话框」角色让位，编辑退到右键菜单第二项，规格 §6②）。
    // 终审项2（2026-10-21）：只认**左键**双击——右键双击不再误发本信号（右键语义
    // 唯一路径是右键菜单），非左键 DblClick 不吞事件、回落默认分发。
    bool eventFilter(QObject* watched, QEvent* event) override;
    // v2.16-B 任务4 Step5：行宽预算随窗口尺寸变化，重排 chips 可见性（§3.7）
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAddClicked();

private:
    void setupUi();
    void refreshFromRegistry();
    // 创建一枚胶囊（name 为空时纯地址展示；toolTip 承载端点详情与备注）
    QPushButton* createPill(const QString& ipPart, const QString& name,
                            const QString& deviceId, const QString& toolTip);
    // v2.16-B 任务4 Step2：选中态显式集合的键式单源——deviceId 优先（"id:xxx"），
    // 无持久 id 的旧行退化为 "ip:<纯地址>"。胶囊属性与设备视图必须共用本式
    //（selectionKey(pill) 与 keyForDevice(index) 都是它的转发，两处不一致会
    // 出现"勾了却取不到"）。
    static QString makeSelectionKey(const QString& deviceId, const QString& ip);
    QString selectionKey(const QPushButton* pill) const;
    QString keyForDevice(size_t index) const;
    // 展示地址段：档案模式恒带端口（与 refreshFromRegistry 旧行为一致），
    // 旧行仅非默认端口带端口（与 addDevice 旧行为一致）。
    QString ipPartForDevice(size_t index) const;
    // 设备视图+档案快照（胶囊/菜单成员共用的唯一一条装配路径，索引对齐：
    // segments() 的下标载体必须来自同一 collectEntries() 产出的 profiles——规格 §2.1）
    struct BusEntry {
        DeviceProfile profile;   // 注册表档案，或旧行经 fromDeviceInfo 合成（同 editProfileFor 口径）
        QString key;             // makeSelectionKey 结果（选中集合的键）
        QString deviceId;        // 可空（旧行）
        QString name;            // 档案名/别名（可空 → 胶囊回落 ipPart）
        QString ipPart;          // 展示地址
        QString toolTip;         // 逐端点 协议 ip:port + 备注
    };
    QVector<BusEntry> collectEntries() const;
    // v2.16-B 任务4 Step3-5：第 1 行装配三件套（chips 只显已选，视觉态只切可见性——
    // 胶囊仍一台一枚与 m_devices 平行创建/销毁，选中/折叠只 setVisible）
    //  chips 可见性同步 + 空态提示/入口文案 + 溢出折算（Step5）
    void rebuildChips();
    // 行宽预算 = 设备栏自身宽度 − 同行固定控件（可见者；+M 台按钮除外——其占位宽度
    // 由 applyPillOverflow 在确认折叠时预留并复算，防首折瞬间超一个按钮宽的抖动）。
    // 预算 ≤0（极窄）时全部折进 +M 台，是合法收纳态
    int  visiblePillBudget() const;
    // 从尾部起收起超预算的 chips，被折数量写进 +M 台按钮（守恒：可见+M==已选总数）
    void applyPillOverflow();
    // 「设备 ▾」弹层：每次 aboutToShow 清空重建（勾选态==m_selectedIds 成员判定）
    void buildPickerMenuInto(QMenu* menu);
    // 胶囊→档案装配唯一单源（v2.16-B 任务5，从 editProfileFor 装配段抽出，
    // 双击/右键「打开这台设备」/右键「编辑档案」三处共用）：两条路径口径一致——
    // 胶囊带持久 deviceId 走 registry->find，旧 device.list ip:port 胶囊（无 id）
    // 按纯 IP 回查 m_devices 经 fromDeviceInfo 合成；任一路径查不到＝档案不可用，
    // 返回空 optional（绝不返回默认构造空档案），调用方各自早退。
    std::optional<DeviceProfile> profileForPill(const QPushButton* pill) const;
    // v2.16-B 修复轮1（规格 §2.3 订正本/§10 第 8 条）：chip 尾部「×」命中区判据。
    // 区宽＝胶囊自身 font 下尾标 "  ×" 的字宽（QFontMetricsF 实算，不拍脑袋）
    // + 样式按钮内部 padding（PM_ButtonMargin）；区为按钮右缘起覆盖该区宽的矩形。
    // 口径限制见 cpp 侧 pillCloseZoneWidth 注释（终审项7 同批订正：QSS font-size 会
    // 灌进 QWidget::font()；默认样式 22px、生产 QSS 16px）。
    static int pillCloseZoneWidth(const QPushButton* pill);
    static bool pillCloseZoneContains(const QPushButton* pill, const QPoint& pos);
    // ── v2.16-B 任务5：右键弹层"构建/分派"双段单源 ──
    // 生产 contextMenuRequested 用 exec 返回值→dispatchPillMenuAction 分派
    //（时序与旧实现一致：弹层关闭后才开对话框）。QMenu::exec 真弹层自动化在本环境
    // 不可达（QTest 右键不产生 customContextMenuRequested，且弹层可见性恒 0 挂死
    // exec 嵌套循环——任务5报告留证），故测试经 QMetaObject::invokeMethod 直调
    // 同一对生产方法驱动"构建→按文本找项→分派"。覆盖范围如实登记：这对方法是生产
    // 真用的方法（非测试专用脚手架），但 customContextMenuRequested 里 menu->exec
    // 的那段弹层/取返回值 wiring 不经自动化，归任务 9 人工/实机验收。Q_INVOKABLE
    // 仅为让测试借元对象可达这两个内部方法，不改可见性、不扩公开 API。
    Q_INVOKABLE QMenu* buildPillMenu();
    Q_INVOKABLE void dispatchPillMenuAction(QPushButton* pill, const QString& role);
    // 右键菜单「编辑档案」入口：打开档案对话框，保存经 registry 持久化
    // （v2.16-B 任务5 起对话框角色自让位给双击，此处不再是双击目标）
    void editProfileFor(QPushButton* pill);
    // 旧 device.list ip:port 行写入/删除（回退兼容，registry 路径同样保留）
    void persistLegacyRow(const DeviceInfo& device);
    // #46 保存入口源端校验：与他档同协议同键（协议+ip+port）端点重复时弹确认，
    // 可保存但需确认（默认"否"）；返回 false 表示用户放弃本次保存
    bool confirmEndpointConflicts(const DeviceProfile& edited);

    // 胶囊式设备栏
    QWidget*      m_pillContainer = nullptr;
    QHBoxLayout*  m_pillLayout    = nullptr;
    std::vector<QPushButton*> m_pills;

    // v2.16-B 任务4（Step1 桩，Step2 起为选中态唯一真源）：显式选中 id 集合
    QSet<QString> m_selectedIds;
    // 与 m_devices 平行维护的档案 deviceId 视图（空串=旧行无持久 id → ip 键）
    std::vector<QString> m_deviceIds;
    // 搜索框会话态过滤词（不持久化，规格 §2.5）：只作用于下一次菜单构建的可见集
    QString m_filterText;
    // v2.16-B 修复轮1（规格 §2.3/§10 第 8 条）：chip 的 clicked 激活源判别标记。
    // QAbstractButton 对鼠标 release 与键盘 Space/Enter 都 emit clicked()，二者必须
    // 分道——鼠标：仅尾部「×」命中区移出选择（主体单击不改选择，双击因此才可达）；
    // 键盘：一律移出（可达性不丢）。eventFilter 在胶囊的鼠标 press/release/key-press
    // 上维护本标记，clicked 处理器同步消费（release 直达 clicked 无事件间隙）。
    // 终审项10（2026-10-21 措辞订正，原句「杜绝跨激活残留」过头）：press/key 先于
    // 每次 clicked 清空 ⇒ 键盘与后续鼠标激活读不到旧标记；唯一残留形态是真双击被吞
    // 后的那次尾部 release 重新置位且无 clicked 消费——它在下一次 press/key 时被清，
    // 真实键鼠路径不可达，唯一可被读到的误用是紧随真双击后对同一枚胶囊的程序化
    // click()（会被读成主体单击而不移出）。QPointer 防悬挂。
    QPointer<QPushButton> m_chipReleasePill;
    bool m_chipReleaseInCloseZone = false;

    // v2.16-B 任务4 Step3：第 1 行新形态控件（chips 宿主复用 m_pillContainer/
    // m_pillLayout，objectName=selectedPillHost）
    QLabel*      m_hintLabel  = nullptr;   // deviceBusHint：空态提示
    QPushButton* m_pickerBtn  = nullptr;   // devicePickerBtn：「设备 ▾」入口
    QPushButton* m_moreBtn    = nullptr;   // devicePillMore：+M 台（溢出折算）
    QLineEdit*   m_searchEdit = nullptr;   // deviceSearchEdit：行尾搜索框
    QMenu*       m_pickerMenu = nullptr;   // 入口与 +M 台共用同一个菜单实例
    bool         m_pendingSelectionBroadcast = false;   // 菜单连勾多台→aboutToHide 只广播一次
    // 终审项3（2026-10-21）：applyPillOverflow 的 qInfo 只在 shown/folded/selected
    // 三元组变化时打印——resizeEvent 每帧重算，逐帧刷屏用户可见日志面板即缺陷；
    // -1 哨兵=尚未记录过任何状态（首帧必打一条）。
    int          m_lastOverflowShown = -1;
    int          m_lastOverflowFolded = -1;
    int          m_lastOverflowSelected = -1;

    QPushButton* m_addButton    = nullptr;
    QLineEdit*   m_userEdit     = nullptr;
    QLineEdit*   m_passEdit     = nullptr;
    QList<QVariantMap> m_recentDevices;       // ConfigStore device.list 启动缓存
    std::vector<DeviceInfo> m_devices;        // 设备列表
    DeviceRegistry* m_registry = nullptr;     // v2.11 设备档案注册表（可空=旧行为）
};
