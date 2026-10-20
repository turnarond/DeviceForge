// v2.11 Task 6：DeviceBus 设备档案展示/编辑 + TaskCenterWidget 三栏任务中心 UI 测试。
// 覆盖：① 设备胶囊名称优先展示 + 端点详情悬浮；② 右键「编辑档案」打开档案编辑
// 对话框并经 DeviceRegistry 持久化（deviceProfileEdited 信号；v2.16-B 任务5 起双击
// 语义改判「打开这台设备」，编辑入口迁至右键菜单）；③ 无档案注册表时旧 ip:port
// 行保持可用（回退兼容）；④ 任务中心模板列表/步骤摘要/目标设备名称+地址；
// ⑤ 执行前确认（含风险提示），拒绝则不启动引擎、不落执行记录；
// ⑥ 校验失败运行收口后历史表出现「失败」行，选中后「从失败阶段重试」可用，
// 点击走 TaskExecutionEngine::retryFailed 并产生第二条执行记录；
// ⑦ MultiProgressWidget 行标签显示 名称 + 地址（单次部署无名称时保持原样）；
// ⑧ v2.13 模板导出：公共 API 直写契约文件 + 按钮选中启用规则 + 文件名清洗口径；
// ⑨ v2.13 模板导入：新建计数+悬空引用保留/同名三选（覆盖继承 ID 与版本、改名
//    "(2)"、取消静默）/坏文件不落库。
// ⑩ v2.13 #54 Task 2：模板编辑器对话框骨架——元信息/设备多选（含悬空档案展示与
//    默认勾选）/步骤列表/reboot 参数页/validate+collect+canonicalDeployParameters。
// ⑪ v2.13 #54 Task 3：deploy 步骤面板——teItemList 表格绑定 items[]、单元格编辑
//    即时写回、方向/覆盖下拉规范串、collect 从可见表格快照。
// ⑫ v2.13 #54 Task 4：commands/recover 面板（多行命令/timeoutSec/共享 protocol）。
// ⑬ v2.13 #54 Task 5：任务中心接线——新建/编辑/删除三按钮存在与启用态翻转、
//    删除确认「否」不删/「是」删除并复位、applyEditorResult 公共编排缝
//    （原位覆盖保 id / 新模板生成 id / 无效模板拒 save 返回 false）。
// ⑭ v2.13 #54 Task 6：e2e「新建→执行」链——编辑器 collect 产出的 deploy+reboot
//    两步模板经 applyEditorResult 落库（store 生成 id）后 startTemplate 全链
//    发起执行并收口落库（FTP-only 档案不触网，重启步骤必校验失败，模式同⑥）。
// ⑮ v2.14 #49-4：阶段恢复拒绝文案三分派静态直测（retryRejectionMessage 纯函数，
//    lineage 未知 / 引擎 32 轮已淘汰 / 活跃运行三种入参各得对应关键词且两两不同）。
// ⑯ v2.15 #49-6：运行终态聚合纯头直测（taskrun::aggregateRunStatus 分桶收敛规则，
//    单源见 task/TaskRunAggregate.h）。
// 模式参照 tests/panel_async：QSignalSpy + QTRY_* 超时兜底，禁止 sleep 等待；
// 模态对话框经已布防的 QTimer 处理（exec 的嵌套事件循环会触发定时器）。
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <memory>   // v2.16-B 任务5：每用例可取消定时器上下文 m_timerGuard（unique_ptr）

#include "config/ConfigStore.h"
#include "device/DeviceRegistry.h"
#include "task/TaskCenterWidget.h"
#include "task/TaskExecutionEngine.h"
#include "task/TaskRunAggregate.h"
#include "task/TaskRunStore.h"
#include "task/TaskTemplateCodec.h"
#include "task/TaskTemplateEditorDialog.h"
#include "task/TaskTemplateStore.h"
#include "tools/FtpDeployTool/MultiProgressWidget.h"
#include "ui/DeviceBusWidget.h"
// v2.16-B 任务5：openDeviceRequested 载荷的目标页判定消费任务2纯头（勿在测试里
// 另写一份协议→页映射；规格 §2.2）
#include "ui/DeviceOpenRouter.h"

namespace {

DeviceProfile makeProfile(const QString& deviceId, const QString& name,
                          const QString& ip, int port,
                          const QString& protocol = QStringLiteral("ftp"))
{
    DeviceProfile profile;
    profile.deviceId = deviceId.toStdString();
    profile.name = name.toStdString();
    DeviceEndpoint endpoint;
    endpoint.protocol = protocol.toStdString();
    endpoint.ip = ip.toStdString();
    endpoint.port = port;
    profile.endpoints.push_back(endpoint);
    return profile;
}

// 追加第二个端点（同物理设备跨协议）
void addEndpoint(DeviceProfile& profile, const QString& protocol,
                 const QString& ip, int port)
{
    DeviceEndpoint endpoint;
    endpoint.protocol = protocol.toStdString();
    endpoint.ip = ip.toStdString();
    endpoint.port = port;
    profile.endpoints.push_back(endpoint);
}

TaskStep makeStep(TaskStepType type, const QVariantMap& parameters = {})
{
    TaskStep step;
    step.type = type;
    step.parameters = parameters;
    return step;
}

} // namespace

class TstTaskCenterWidget : public QObject {
    Q_OBJECT

private:
    QString m_workDir;
    QString m_dbPath;
    // v2.13 Task 3：模板导出用例的临时目录（init() 逐用例重建）
    QTemporaryDir m_tempDir;
    // 档案编辑对话框 RejectRole 按钮计数（布防定时器在对话框嵌套事件循环中写入，
    // -1 = 未命中对话框；回归：重复 addButton「取消」会计数 >1）
    int m_editorRejectButtonCount = -1;
    // v2.11.1 取证暂存：端点表协议/凭据下拉内容（布防定时器写入，空 = 未命中）
    QStringList m_captureProtocolItems;
    QStringList m_captureCredentialItems;

    // 每个用例独立数据库（同 tst_task_run_store 模式）
    void freshDb()
    {
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
    }

    QPushButton* firstPill(DeviceBusWidget& bus)
    {
        const auto pills = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        return pills.isEmpty() ? nullptr : pills.first();
    }

    // ── v2.16-B 任务4 夹具：设备栏第 1 行只显「已选 chips」，选择必须经菜单路径 ──
    // 可见 chips = 已选且未被溢出折算收起的胶囊（未选/折叠的胶囊 isHidden()==true）。
    QList<QPushButton*> visibleChips(DeviceBusWidget& bus)
    {
        QList<QPushButton*> out;
        const auto pills = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        for (QPushButton* p : pills) {
            if (!p->isHidden())
                out.append(p);
        }
        return out;
    }

    // 递归找菜单成员项（QAction 携带 deviceKey 属性，分组子菜单同样命中）
    QAction* pickerActionForKey(QMenu* menu, const QString& deviceKey)
    {
        const auto acts = menu->actions();
        for (QAction* a : acts) {
            if (a->property("deviceKey").toString() == deviceKey)
                return a;
            if (a->menu()) {
                if (QAction* sub = pickerActionForKey(a->menu(), deviceKey))
                    return sub;
            }
        }
        return nullptr;
    }

    // 收集弹层里全部成员项（携带 deviceKey 属性的 QAction，跨分组子菜单递归）
    QList<QAction*> menuMemberActions(QMenu* menu)
    {
        QList<QAction*> out;
        const auto acts = menu->actions();
        for (QAction* a : acts) {
            if (a->menu())
                out += menuMemberActions(a->menu());
            else if (a->property("deviceKey").isValid())
                out.append(a);
        }
        return out;
    }

    // 经真实菜单路径勾中一台设备：invokeMethod 直发 aboutToShow/aboutToHide，
    // 等价于弹出并关闭菜单（重建与勾选走同一批接线，不弹真实弹层，CI 无窗口依赖）。
    void selectDeviceViaMenu(DeviceBusWidget& bus, const QString& deviceKey)
    {
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QAction* act = pickerActionForKey(menu, deviceKey);
        QVERIFY2(act, qPrintable(QStringLiteral("菜单缺少键 %1 的成员项").arg(deviceKey)));
        act->toggle();
        QMetaObject::invokeMethod(menu, "aboutToHide");
    }

    // 勾中全部可见设备（菜单底部「全选可见 N 台」的公开动作路径）
    void selectAllViaMenu(DeviceBusWidget& bus)
    {
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QAction* selectAll = nullptr;
        const auto acts = menu->actions();
        for (QAction* a : acts) {
            if (a->text().startsWith(QStringLiteral("全选可见")))
                selectAll = a;
        }
        QVERIFY2(selectAll, qPrintable(QStringLiteral("菜单底部未见「全选可见 N 台」动作，实际首项: %1")
                                            .arg(acts.isEmpty() ? QStringLiteral("(空)") : acts.first()->text())));
        selectAll->trigger();
        QMetaObject::invokeMethod(menu, "aboutToHide");
    }

    // ── v2.16-B 任务5：胶囊右键菜单自动化缝（brief 核准的备选缝）──
    // brief 主缝是 QTest::mouseClick 右键 + 布防定时器在 topLevelWidgets 里猎取
    // QMenu 触发「编辑档案」。本环境主缝不可达（见任务5报告的探针实测）：
    // QTest::mouseClick(pill, Qt::RightButton) 不产生 customContextMenuRequested
    //（不经 QWidgetWindow 的 release→contextMenu 分发），主缝连生产 lambda 都进
    // 不去。故采用 brief 核准的备选缝：菜单构建与分派都收口在产品的 Q_INVOKABLE
    // 单源方法 buildPillMenu/dispatchPillMenuAction 上（生产 contextMenuRequested
    // 走的正是这对方法，非测试专用脚手架），测试经元对象驱动"同一构建→按文本找项
    // →同一分派"。覆盖如实登记：本缝覆盖菜单构建/次序/分派路由，未覆盖 lambda 内
    // menu->exec 的弹层取返回值 wiring——那段归任务 9 人工/实机验收。
    // 失败响亮：构建返回空→m_menuNotFound；目标项不在菜单→m_menuActionMissing；
    // 两者都让用例判红，"模拟缝没生效"不许伪装成绿。
    bool m_menuNotFound = false;
    bool m_menuActionMissing = false;
    bool m_menuTriggered = false;
    QStringList m_menuCapturedTexts;   // 触发时刻菜单项全文（次序/形态断言用）

    void triggerPillMenuAction(DeviceBusWidget& bus, QPushButton* pill, const QString& actionText)
    {
        m_menuNotFound = false;
        m_menuActionMissing = false;
        m_menuTriggered = false;
        m_menuCapturedTexts.clear();
        QMenu* menu = nullptr;
        const bool built = QMetaObject::invokeMethod(&bus, "buildPillMenu",
                                                     Qt::DirectConnection,
                                                     Q_RETURN_ARG(QMenu*, menu));
        if (!built || !menu) {
            m_menuNotFound = true;
            return;
        }
        const auto actions = menu->actions();
        for (QAction* action : actions)
            m_menuCapturedTexts << action->text();
        QString role;
        for (QAction* action : actions) {
            if (action->text() == actionText) {
                role = action->property("pillMenuRole").toString();
                break;
            }
        }
        // 分派只依赖 role 字符串与 pill，菜单此刻即可销毁（防悬垂/防泄漏两吃）
        delete menu;
        if (role.isEmpty()) {
            m_menuActionMissing = true;
            return;
        }
        m_menuTriggered = true;
        QMetaObject::invokeMethod(&bus, "dispatchPillMenuAction",
                                  Qt::DirectConnection,
                                  Q_ARG(QPushButton*, pill), Q_ARG(QString, role));
    }

    // 三个现存槽的迁移入口：原 mouseDClick（任务5 起双击语义已让位给
    // 「打开这台设备」）改走右键菜单「编辑档案」
    void openEditMenu(DeviceBusWidget& bus, QPushButton* pill)
    {
        triggerPillMenuAction(bus, pill, QStringLiteral("编辑档案"));
    }

    // ── v2.13 Task 4：模板导入用例 helper ──
    // 种子模板：name=「导入体」、1 步、1 个 device-dead 悬空引用（encodeTemplateFile 直造文件）
    TaskTemplate makeExportableTemplate() const
    {
        TaskTemplate tmpl;
        tmpl.name = QStringLiteral("导入体").toUtf8().toStdString();
        tmpl.description = QStringLiteral("导入用例种子").toUtf8().toStdString();
        tmpl.deviceIds = {"device-dead"};
        TaskStep step;
        step.type = TaskStepType::Reboot;
        tmpl.steps.push_back(step);
        return tmpl;
    }

    QString writeToTemp(const QByteArray& data)
    {
        const QString path = m_tempDir.filePath(QStringLiteral("template.json"));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString();
        f.write(data);
        f.close();
        return path;
    }

    // 布防-捕获模态对话框：exec 嵌套事件循环触发定时器 → 抓文案 + 关闭。
    // 仅在「本用例必弹框」路径使用；静默路径（取消）布防会把定时器漏进后续用例。
    // 负对照：未弹框则 m_dialogClosed 保持 false（center 析构后布防 lambda 随上下文销毁，不外溢）。
    void armDialogCapture(TaskCenterWidget& center)
    {
        m_dialogText.clear();
        m_dialogClosed = false;
        QTimer::singleShot(50, &center, [this, &center] {
            QMessageBox* box = center.findChild<QMessageBox*>();
            if (!box)
                return;
            m_dialogText = box->text();
            m_dialogClosed = true;
            box->done(QMessageBox::Ok);
        });
    }

    QString m_dialogText;
    bool m_dialogClosed = false;

    // v2.16-B 任务5 Step1 取证：双击是否弹出了档案编辑对话框（布防定时器命中置真）
    bool m_forensicEditorDialogOpened = false;
    QString m_legacyNote;   // legacyIpPort 槽诊断（仅失败信息导出用）
    // v2.16-B 修复轮1 finding2：「移除设备」role 分派时确认框的文案与应答实况
    //（未弹框/未命中则 m_removeBoxAnswered 保持 false，用例响亮判红）
    QString m_removeBoxText;
    bool m_removeBoxAnswered = false;

    // v2.16-B 任务5：每个用例独享的可取消定时器上下文。设备类用例常在对话框
    // exec 的嵌套事件循环里用 QTimer::singleShot 布防"命中即改名保存 / 兜底强关
    // 编辑器 / 反向兜底探测误弹编辑框"。裸 lambda 或以 this 为上下文的 singleShot
    // 会在本用例返回后仍悬挂到默认事件循环，被**下一个**用例的 dialog.exec 兑现，
    // 提前 close/answer 掉后一用例刚弹出的编辑器（实测把 legacy 槽的 exec 秒退回
    // Rejected）。绑到本对象上：cleanup() 里 reset 即撤销所有未兑现的挂起定时器。
    std::unique_ptr<QObject> m_timerGuard;

private slots:
    void initTestCase()
    {
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_task_center_widget_%1")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        QVERIFY(QDir().mkpath(m_workDir));
        m_dbPath = QDir(m_workDir).filePath(QStringLiteral("config.db"));
    }

    void cleanupTestCase()
    {
        ConfigStore::instance().close();
        QDir(m_workDir).removeRecursively();
    }

    void init()
    {
        freshDb();
        m_timerGuard = std::make_unique<QObject>();   // 本用例定时器上下文（cleanup 撤销挂起定时器）
        m_tempDir = QTemporaryDir();            // 每用例独立临时目录（模板导出落盘用）
        QVERIFY2(m_tempDir.isValid(), qPrintable(m_tempDir.errorString()));
    }
    void cleanup()
    {
        m_timerGuard.reset();   // 撤销所有绑到本用例、尚未兑现的 singleShot（防跨用例误关编辑器）
        ConfigStore::instance().close();
    }

    // ── ① 名称优先 + 端点详情悬浮 ────────────────────────────────
    // v2.16-B 任务4 Step7 迁移：第 1 行只显已选 chips——夹具先经菜单勾中目标，
    // 再取 chips；原断言（名称优先/端点悬浮/选中集合）逐条保留，仅
    // "文本含 ip"判据按新形态反转为"文本不含 ip + tooltip 仍含 ip"（规格 §6 交代）。
    void deviceBus_nameFirstDisplay()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile profile = makeProfile(
            QStringLiteral("dev-boiler"), QStringLiteral("锅炉控制器"),
            QStringLiteral("10.0.0.5"), 21);
        addEndpoint(profile, QStringLiteral("telnet"), QStringLiteral("10.0.0.5"), 23);
        profile.note = QStringLiteral("车间 A").toUtf8().toStdString();
        QVERIFY(registry.save(profile));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        // 新形态守卫：未选=无 chips，但全集/胶囊数据不删（视觉态只切可见性）
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 1);
        QCOMPARE(visibleChips(bus).size(), 0);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-boiler"));

        const auto chips = visibleChips(bus);
        QCOMPARE(chips.size(), 1);
        QPushButton* pill = chips.first();
        QVERIFY(pill);
        // 名称优先展示（新形态：地址不再进文案，重名才带 ·ip 尾标）
        QVERIFY2(pill->text().startsWith(QStringLiteral("锅炉控制器")),
                 qPrintable(QStringLiteral("胶囊应名称优先，实际: %1").arg(pill->text())));
        QVERIFY2(!pill->text().contains(QStringLiteral("10.0.0.5")),
                 qPrintable(QStringLiteral("chips 不应再带 ip:port，实际: %1").arg(pill->text())));

        // ② 端点详情：悬浮可见协议端点与备注（地址经 tooltip 承载，§6）
        QVERIFY2(pill->toolTip().contains(QStringLiteral("10.0.0.5")),
                 qPrintable(pill->toolTip()));
        QVERIFY2(pill->toolTip().contains(QStringLiteral("ftp 10.0.0.5:21")),
                 qPrintable(pill->toolTip()));
        QVERIFY2(pill->toolTip().contains(QStringLiteral("telnet 10.0.0.5:23")),
                 qPrintable(pill->toolTip()));
        QVERIFY(pill->toolTip().contains(QStringLiteral("车间 A")));

        // 选中集合以档案（DeviceProfile）暴露，deviceId 稳定
        const auto selected = bus.selectedDeviceProfiles();
        QCOMPARE(selected.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(selected.front().deviceId),
                 QStringLiteral("dev-boiler"));
        // click()＝无鼠标事件的 clicked 激活（修复轮1 起属键盘等价路径，仍移出选择；
        // 真鼠标左键已拆分：主体不改选择、移出只走尾部「×」区）；移出后集合与视图归零
        pill->click();
        QCOMPARE(static_cast<int>(bus.selectedDeviceProfiles().size()), 0);
        QCOMPARE(visibleChips(bus).size(), 0);
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 1);   // 硬线
    }

    // ── ② 右键「编辑档案」打开编辑对话框 → 经 DeviceRegistry 持久化 + 信号 ──
    //（v2.16-B 任务5：原双击入口改判「打开这台设备」，本槽迁移到右键菜单，
    //  编辑链/信号/持久化断言逐条原样保留）
    void deviceBus_editorDialog_persistsViaRegistryAndEmits()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-plc"), QStringLiteral("旧名称"),
            QStringLiteral("10.0.0.9"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        // Step7 迁移：先经菜单勾中（chips 只显已选），右键对象是真实可见的 chip
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-plc"));
        // 修复轮1 finding4：判据对象取「可见 chips」首枚——firstPill 不过滤可见性，
        // 夹具只有一枚可见胶囊时两者同物，但"第 N 枚胶囊==第 N 台"不得在测试层回潜
        const auto chips = visibleChips(bus);
        QVERIFY(!chips.isEmpty());
        QPushButton* pill = chips.first();

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(spy.isValid());
        m_editorRejectButtonCount = -1;

        // 布防：对话框 exec 的嵌套事件循环中改名并保存。
        // v2.16-B 任务5 迁移：对话框由菜单缝分派后在嵌套 exec 循环中出现，
        // 捕获布防 150ms（须晚于分派点，早于对话框出现会静默 miss）
        // （早于对话框出现会静默 miss，靠 3s 兜底也拿不到断言对象）
        QTimer::singleShot(150, m_timerGuard.get(), [this] {
            QDialog* dialog = nullptr;
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    dialog = qobject_cast<QDialog*>(w);
                    break;
                }
            }
            if (!dialog)
                return;
            auto* nameEdit = dialog->findChild<QLineEdit*>(QStringLiteral("deviceProfileName"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("deviceProfileSave"));
            if (!nameEdit || !save)
                return;
            // 回归断言取证：对话框必须只暴露一个 RejectRole（取消）按钮
            if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
                int rejectCount = 0;
                const auto buttons = box->buttons();
                for (QAbstractButton* b : buttons) {
                    if (box->buttonRole(b) == QDialogButtonBox::RejectRole)
                        ++rejectCount;
                }
                m_editorRejectButtonCount = rejectCount;
            }
            nameEdit->setText(QStringLiteral("新名称"));
            save->click();
        });
        // 兜底：若布防未命中（对话框实现漂移），3s 后强制关闭，避免测试挂死
        QTimer::singleShot(3000, m_timerGuard.get(), [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor"))
                    w->close();
            }
        });

        // v2.16-B 任务5 迁移：双击已让位「打开这台设备」，编辑链改走右键菜单
        // 「编辑档案」（菜单缝 + 非虚断言见 triggerPillMenuAction 注释）
        openEditMenu(bus, pill);

        // 修复轮1 minor4：缝守卫前移到任何 QTRY 等待之前——缝失效要当场指名，
        // 不能先退化成一记泛用 spy 超时（QTRY 红的是等待物，不是根因）
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QVERIFY2(!m_menuActionMissing, "右键菜单缺少「编辑档案」项");
        QVERIFY2(m_menuTriggered, "「编辑档案」项未被触发");
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        const DeviceProfile edited =
            spy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(QString::fromStdString(edited.name), QStringLiteral("新名称"));

        // 注册表持久化（内存视图即时更新 + ConfigStore 可回读）
        const auto stored = registry.find("dev-plc");
        QVERIFY(stored.has_value());
        QCOMPARE(QString::fromStdString(stored->name), QStringLiteral("新名称"));
        QVERIFY(ConfigStore::instance().exists(
            QStringLiteral("device.profile"), QStringLiteral("dev-plc")));

        // 胶囊文案即时刷新为新名称（修复轮1 finding4：判据对象=可见 chips——且必须
        // 每次轮询重新 findChildren：保存触发 refreshFromRegistry 删建全部胶囊，
        // 缓存旧 pill 指针是悬垂读（QTRY 内解引用 == UAF，绝不允许）
        QTRY_VERIFY_WITH_TIMEOUT(
            !visibleChips(bus).isEmpty() &&
                visibleChips(bus).first()->text().startsWith(QStringLiteral("新名称")),
            5000);

        // 对话框取消按钮唯一（修复回归：曾因 addButton 重复渲染两个「取消」）
        QVERIFY2(m_editorRejectButtonCount >= 0,
                 "布防定时器未命中编辑对话框，无法取证按钮计数");
        QCOMPARE(m_editorRejectButtonCount, 1);
    }

    // ── ②b 终审 Critical 1：编辑器改地址 → 旧端点/旧 device.list 行彻底消失 ──
    void deviceBus_editorAddressChange_dropsOldEndpointAndStaleLegacyRow()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-ip"), QStringLiteral("改址设备"),
            QStringLiteral("10.3.0.1"), 21)));

        // 模拟旧增添路径持久化过的 device.list 回退行（key=ip:port）+ 无关设备旧行
        QVariantMap legacyRow;
        legacyRow.insert(QStringLiteral("ip"), QStringLiteral("10.3.0.1"));
        legacyRow.insert(QStringLiteral("port"), 21);
        legacyRow.insert(QStringLiteral("displayName"), QStringLiteral("改址设备"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.3.0.1:21"), legacyRow));
        QVariantMap unrelatedRow;
        unrelatedRow.insert(QStringLiteral("ip"), QStringLiteral("10.3.0.9"));
        unrelatedRow.insert(QStringLiteral("port"), 21);
        unrelatedRow.insert(QStringLiteral("displayName"), QStringLiteral("无关设备"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.3.0.9:21"), unrelatedRow));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        // Step7 迁移：先经菜单勾中；v2.16-B 任务5：编辑入口改右键「编辑档案」
        //（编辑链与旧行清扫断言逐条原样保留）
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-ip"));
        const auto chips = visibleChips(bus);   // 修复轮1 finding4：判据对象=可见 chip（防胶囊位置配对回潜）
        QVERIFY(!chips.isEmpty());
        QPushButton* pill = chips.first();

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(spy.isValid());

        // 布防：把端点表首行主机地址 ip1→ip2 后保存。
        // v2.16-B 任务5 迁移：对话框由菜单缝分派后在嵌套 exec 循环中出现，
        // 捕获布防 150ms（须晚于分派点，早于对话框出现会静默 miss）
        QTimer::singleShot(150, m_timerGuard.get(), [this] {
            QDialog* dialog = nullptr;
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    dialog = qobject_cast<QDialog*>(w);
                    break;
                }
            }
            if (!dialog)
                return;
            auto* table = dialog->findChild<QTableWidget*>(
                QStringLiteral("deviceProfileEndpoints"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("deviceProfileSave"));
            if (!table || !save || table->rowCount() == 0 || !table->item(0, 1))
                return;
            table->item(0, 1)->setText(QStringLiteral("10.3.0.2"));
            save->click();
        });
        QTimer::singleShot(3000, m_timerGuard.get(), [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor"))
                    w->close();
            }
        });

        openEditMenu(bus, pill);
        // 修复轮1 minor4：缝守卫前移到 QTRY 之前（缝失效当场指名，不退化成泛用超时）
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QVERIFY2(!m_menuActionMissing, "右键菜单缺少「编辑档案」项");
        QVERIFY2(m_menuTriggered, "「编辑档案」项未被触发");
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        // 信号携带的已保存档案只剩新地址（旧端点未被并集写回）
        const DeviceProfile saved = spy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(saved.endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(saved.endpoints.front().ip),
                 QStringLiteral("10.3.0.2"));

        // 内存注册表 + device.profile 落库均只剩新端点
        const auto stored = registry.find("dev-ip");
        QVERIFY(stored.has_value());
        QCOMPARE(stored->endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(stored->endpoints.front().ip),
                 QStringLiteral("10.3.0.2"));

        // 被取代旧行清扫；persistLegacyRow 回写新地址行；无关旧行保留
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("device.list"),
                                                QStringLiteral("10.3.0.1:21")));
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("10.3.0.2:21")));
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.list"),
                                               QStringLiteral("10.3.0.9:21")));

        // 重载：旧地址与旧行不复活（本档案唯一端点 ip2 + 无关旧设备一台）
        DeviceRegistry reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.list().size(), std::size_t{2});
        const auto after = reloaded.find("dev-ip");
        QVERIFY(after.has_value());
        QCOMPARE(after->endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(after->endpoints.front().ip),
                 QStringLiteral("10.3.0.2"));
    }

    // ── ②c 终审 Important 4：档案新增/编辑/删除不得清空胶囊选中范围 ──────
    // ── v2.11.1 ①：端点表协议下拉 + 凭据下拉/就地新建 ──
    void deviceBus_editorProtocolAndCredentialCombos()
    {
        // 存储层直测：空键拒绝、DPAPI 密文入库、无明文
        QCOMPARE(devicebus::storeFtpCredential(QString(), QStringLiteral("u"),
                                              QStringLiteral("p"), QString(), 0),
                 QString());
        const QString credKey = devicebus::storeFtpCredential(
            QStringLiteral("root@10.6.0.1:21"), QStringLiteral("root"),
            QStringLiteral("s3cret"), QStringLiteral("10.6.0.1"), 21);
        QCOMPARE(credKey, QStringLiteral("root@10.6.0.1:21"));
        const QVariantMap raw = ConfigStore::instance().load(
            QStringLiteral("ftp.credential"), credKey);
        QCOMPARE(raw.value(QStringLiteral("username")).toString(),
                 QStringLiteral("root"));
        const QString storedPass = raw.value(QStringLiteral("password")).toString();
        QVERIFY2(!storedPass.isEmpty(), "DPAPI 密文缺失");
        QVERIFY(!storedPass.contains(QStringLiteral("s3cret")));

        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-dd"), QStringLiteral("焊机"),
            QStringLiteral("10.6.0.1"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        // Step7 迁移：先经菜单勾中；v2.16-B 任务5：编辑入口改右键「编辑档案」
        //（下拉取证与落库断言逐条原样保留）
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-dd"));
        const auto chips = visibleChips(bus);   // 修复轮1 finding4：判据对象=可见 chip（防胶囊位置配对回潜）
        QVERIFY(!chips.isEmpty());
        QPushButton* pill = chips.first();

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(spy.isValid());
        m_captureProtocolItems.clear();
        m_captureCredentialItems.clear();

        // v2.16-B 任务5 迁移：对话框由菜单缝分派后在嵌套 exec 循环中出现，
        // 捕获布防 150ms（须晚于分派点，早于对话框出现会静默 miss）
        QTimer::singleShot(150, m_timerGuard.get(), [this] {
            QDialog* dialog = nullptr;
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    dialog = qobject_cast<QDialog*>(w);
                    break;
                }
            }
            if (!dialog)
                return;
            auto* table = dialog->findChild<QTableWidget*>(
                QStringLiteral("deviceProfileEndpoints"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("deviceProfileSave"));
            if (!table || !save || table->rowCount() == 0)
                return;
            auto* proto = qobject_cast<QComboBox*>(table->cellWidget(0, 0));
            auto* cred = qobject_cast<QComboBox*>(table->cellWidget(0, 3));
            if (!proto || !cred)
                return;
            for (int i = 0; i < proto->count(); ++i)
                m_captureProtocolItems << proto->itemText(i);
            for (int i = 0; i < cred->count(); ++i)
                m_captureCredentialItems << cred->itemText(i);
            proto->setCurrentIndex(proto->findText(QStringLiteral("telnet")));
            cred->setCurrentIndex(cred->findText(QStringLiteral("root@10.6.0.1:21")));
            save->click();
        });
        QTimer::singleShot(3000, m_timerGuard.get(), [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor"))
                    w->close();
            }
        });

        openEditMenu(bus, pill);
        // 修复轮1 minor4：缝守卫前移到 QTRY 之前（缝失效当场指名，不退化成泛用超时）
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QVERIFY2(!m_menuActionMissing, "右键菜单缺少「编辑档案」项");
        QVERIFY2(m_menuTriggered, "「编辑档案」项未被触发");
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        // 下拉内容取证
        QVERIFY2(!m_captureProtocolItems.isEmpty(), "布防未命中协议下拉");
        for (const char* p : {"ftp", "ftps", "sftp", "ssh", "telnet", "modbus"})
            QVERIFY(m_captureProtocolItems.contains(QString::fromLatin1(p)));
        QVERIFY2(!m_captureCredentialItems.isEmpty(), "布防未命中凭据下拉");
        QVERIFY(m_captureCredentialItems.contains(QStringLiteral("（无凭据）")));
        QVERIFY(m_captureCredentialItems.contains(QStringLiteral("root@10.6.0.1:21")));
        QVERIFY(m_captureCredentialItems.contains(QStringLiteral("＋ 新建凭据…")));

        // 选择结果落库：协议 telnet + 凭据引用回填
        const DeviceProfile saved = spy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(saved.endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(saved.endpoints.front().protocol),
                 QStringLiteral("telnet"));
        QCOMPARE(QString::fromStdString(saved.endpoints.front().credentialRef),
                 QStringLiteral("root@10.6.0.1:21"));
    }

    void deviceBus_registryRefreshKeepsPillSelection()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-keep"),
                                           QStringLiteral("保留我"),
                                           QStringLiteral("10.4.0.1"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-other"),
                                           QStringLiteral("旁站"),
                                           QStringLiteral("10.4.0.2"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        // Step7 迁移：胶囊仍一台一枚（视觉态只切可见性），findChildren 计数保持
        // 原语义；"选中"改经菜单路径（chip 左键已变为移出语义）。
        const auto pillsBefore = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        QCOMPARE(pillsBefore.size(), 2);
        QCOMPARE(visibleChips(bus).size(), 0);          // 新形态：未选=无可见 chips
        QPushButton* keep = nullptr;
        for (QPushButton* p : pillsBefore) {
            if (p->property("deviceId").toString() == QStringLiteral("dev-keep"))
                keep = p;
        }
        QVERIFY(keep);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-keep"));
        QCOMPARE(keep->property("selected").toBool(), true);
        QCOMPARE(visibleChips(bus).size(), 1);          // 勾中后只显这一枚

        // 新增一台设备 → 胶囊栏整体重建（refreshFromRegistry）
        DeviceInfo third;
        third.ip = "10.4.0.3";
        third.port = 21;
        bus.addDevice(third);

        // 选中范围必须原样保持：仍只选中 dev-keep
        const auto selected = bus.selectedDeviceProfiles();
        QCOMPARE(selected.size(), std::size_t{1});
        if (!selected.empty())
            QCOMPARE(QString::fromStdString(selected.front().deviceId),
                     QStringLiteral("dev-keep"));
        const auto pillsAfter = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        QCOMPARE(pillsAfter.size(), 3);
        int selectedCount = 0;
        for (QPushButton* p : pillsAfter) {
            if (p->property("selected").toBool())
                ++selectedCount;
        }
        QCOMPARE(selectedCount, 1);
        QCOMPARE(visibleChips(bus).size(), 1);          // 重建后 chips 视图仍只显已选
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 3);   // 硬线
    }

    // ── ③ 无注册表：旧 ip:port 胶囊与 device.list 回退保持可用 ───
    // v2.16-B 任务4 Step7 迁移：无注册表时菜单同样工作（entries 由 fromDeviceInfo
    // 合成，全进「未分组」）；选中改经菜单，胶囊文案与旧行回退断言逐条原样保留。
    void deviceBus_legacyRowsKeepWorking()
    {
        DeviceBusWidget bus;   // 不设 registry → 纯旧行为
        bus.resize(1400, 100);
        DeviceInfo device;
        device.ip = "192.168.9.9";
        device.port = 2122;
        bus.addDevice(device);

        QPushButton* pill = firstPill(bus);
        QVERIFY(pill);
        QCOMPARE(pill->text(), QStringLiteral("192.168.9.9:2122  ×"));
        QCOMPARE(visibleChips(bus).size(), 0);   // 新形态：未选=无可见 chips（空名回落地址仍由文案钉）

        // 旧 device.list 行仍写入（key=ip:port），供回退读取
        const QVariantMap record = ConfigStore::instance().load(
            QStringLiteral("device.list"), QStringLiteral("192.168.9.9:2122"));
        QVERIFY(!record.isEmpty());
        QCOMPARE(record.value(QStringLiteral("ip")).toString(),
                 QStringLiteral("192.168.9.9"));

        // 无档案时 selectedDeviceProfiles 由 DeviceInfo 合成稳定端点身份——
        // 旧行无持久 id ⇒ 选择键退化为 "ip:<地址>"（selectionKey 同款单源式）
        selectDeviceViaMenu(bus, QStringLiteral("ip:192.168.9.9"));
        const auto selected = bus.selectedDeviceProfiles();
        QCOMPARE(selected.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(selected.front().endpoints.front().ip),
                 QStringLiteral("192.168.9.9"));
        QCOMPARE(visibleChips(bus).size(), 1);
        // click()＝键盘等价激活仍移出（修复轮1：真鼠标左键移出改走尾部「×」区）；
        // 集合与视图归零，设备全集不动
        visibleChips(bus).first()->click();
        QCOMPARE(static_cast<int>(bus.selectedDeviceProfiles().size()), 0);
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 1);
    }

    // ── v2.16-B 任务5 Step1：双击副作用取证（规格 §5 前提复核，不是验收）──
    // 规格 §5 声称现状是「双击弹编辑框而选中态不变，依赖两次 clicked 的净零抵消」。
    // **改动前在 HEAD eaf7bfc 实测的基线数字**（见规格 §5 执行注记）：
    //   visible-chip: clicked=0 selectionBroadcasts=0 selectedAfter=1 dialogOpened=1
    //   —— 可观察结论与 §5 一致（弹编辑框而选中态不变），但机制与 §5 说法不符：
    //   不是"两次 clicked 净零抵消"，而是 QTest::mouseDClick 投递**单个**
    //   MouseButtonDblClick 事件，eventFilter 在 QAbstractButton 消费前就吞掉它，
    //   clicked 根本一次都不发。任务5 改判后该消费关系不变 ⇒ 跳转后"净零"依旧。
    // 本槽保留为净零守卫：双击不得产生 clicked/selection 广播、不得改选中，
    // 且（新语义）不得再弹编辑对话框、必须恰好发一次 openDeviceRequested。
    // 隐藏胶囊上的合成双击同批登记：该路径现在只有测试可达（QTest 直投目标
    // widget，绕过真实用户的可见性别拦），eventFilter 不查可见性仍会触发——
    // 实况写进规格 §5 执行注记，是否加闸由控制器裁定。
    void doubleClickEmitsTwoClickedAndKeepsSelection()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-dblmeasure"),
                                          QStringLiteral("双击取证机"),
                                          QStringLiteral("10.5.0.1"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        // 任务4 后真实用户可双击的对象只有「可见 chips」＝已选设备（brief 里
        // "未选中设备 + firstPill" 的夹具形态已过期——未选胶囊是隐藏的）
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-dblmeasure"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();

        QSignalSpy clickedSpy(pill, &QPushButton::clicked);
        QVERIFY(clickedSpy.isValid());
        QSignalSpy selectionSpy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(selectionSpy.isValid());
        QSignalSpy openSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openSpy.isValid());
        m_forensicEditorDialogOpened = false;

        // 反向兜底：若双击仍回归成弹模态编辑框，布防定时器记录并关闭（响亮判红）
        QTimer::singleShot(80, m_timerGuard.get(), [this] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    m_forensicEditorDialogOpened = true;
                    w->close();
                    return;
                }
            }
        });

        QTest::mouseDClick(pill, Qt::LeftButton);

        const bool stillSelected =
            bus.selectedIds().contains(QStringLiteral("id:dev-dblmeasure"));
        qInfo().noquote() << QStringLiteral(
            "dblclick-forensic[visible-chip]: clicked=%1 selectionBroadcasts=%2 "
            "openRequests=%3 selectedAfter=%4 visibleAfter=%5 dialogOpened=%6")
            .arg(clickedSpy.count())
            .arg(selectionSpy.count())
            .arg(openSpy.count())
            .arg(stillSelected)
            .arg(!pill->isHidden())
            .arg(m_forensicEditorDialogOpened);

        // 净零守卫（规格 §5 前提的可观察部分，改判前后都必须成立）：
        QCOMPARE(clickedSpy.count(), 0);
        QCOMPARE(selectionSpy.count(), 0);
        QVERIFY2(stillSelected,
                 qPrintable(QStringLiteral("双击后选中态被改变（移出集合）：clicked=%1 广播=%2")
                                .arg(clickedSpy.count()).arg(selectionSpy.count())));
        // 新语义正向钉桩：
        QCOMPARE(openSpy.count(), 1);
        QVERIFY2(!m_forensicEditorDialogOpened, "双击回归成了编辑对话框");

        // 隐藏胶囊（未选中）上的合成双击同样登记——该路径现在只有测试可达，
        // eventFilter 不查可见性仍会触发（QTest 直接投递到目标 widget，绕过
        // 真实用户路径的可见性别拦）。实况仅登记，不断言（是否加闸待控制器裁定）。
        bus.clearSelection();                     // 公开 API 使其隐藏（禁用鼠标点击隐藏件作选择手段）
        QVERIFY(pill->isHidden());
        m_forensicEditorDialogOpened = false;
        QSignalSpy clickedHiddenSpy(pill, &QPushButton::clicked);
        QVERIFY(clickedHiddenSpy.isValid());
        QSignalSpy selectionHiddenSpy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(selectionHiddenSpy.isValid());
        QSignalSpy openHiddenSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openHiddenSpy.isValid());
        QTimer::singleShot(80, m_timerGuard.get(), [this] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    m_forensicEditorDialogOpened = true;
                    w->close();
                    return;
                }
            }
        });
        QTest::mouseDClick(pill, Qt::LeftButton);
        qInfo().noquote() << QStringLiteral(
            "dblclick-forensic[hidden-pill]: clicked=%1 selectionBroadcasts=%2 "
            "openRequests=%3 dialogOpened=%4")
            .arg(clickedHiddenSpy.count())
            .arg(selectionHiddenSpy.count())
            .arg(openHiddenSpy.count())
            .arg(m_forensicEditorDialogOpened);
    }

    // ── v2.16-B 任务5：双击＝「打开这台设备」正向钉桩（载荷 ip/协议 + 路由同源）──
    void doubleClickOpensRequestedEmitsProfile()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile profile = makeProfile(QStringLiteral("dev-dbl-route"),
                                            QStringLiteral("双击开页机"),
                                            QStringLiteral("10.8.0.1"), 23,
                                            QStringLiteral("telnet"));
        addEndpoint(profile, QStringLiteral("ftp"), QStringLiteral("10.8.0.1"), 21);
        QVERIFY(registry.save(profile));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-dbl-route"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();

        QSignalSpy openSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openSpy.isValid());
        // 反向兜底：若双击误仍走编辑对话框，记录并关闭后由断言响亮红（防挂死）
        m_forensicEditorDialogOpened = false;
        QTimer::singleShot(50, m_timerGuard.get(), [this] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    m_forensicEditorDialogOpened = true;
                    w->close();
                    return;
                }
            }
        });

        QTest::mouseDClick(pill, Qt::LeftButton);

        QVERIFY2(!m_forensicEditorDialogOpened, "双击仍会打开档案编辑对话框（改判未生效）");
        QCOMPARE(openSpy.count(), 1);
        const DeviceProfile emitted = openSpy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(QString::fromStdString(emitted.deviceId),
                 QStringLiteral("dev-dbl-route"));
        // 载荷经任务2 deviceopen::routeTo 判定：首个可解析端点（telnet 在前）
        // 决定目标页与选中键 ip——本任务只发射信号，切页编排归任务6（规格 §2.2）
        const deviceopen::Route route = deviceopen::routeTo(emitted);
        QVERIFY(route.page == deviceopen::Page::BatchCommand);
        QCOMPARE(route.protocol, QStringLiteral("telnet"));
        QCOMPARE(route.ip, QStringLiteral("10.8.0.1"));
        QCOMPARE(route.port, 23);
        // 双击不得改变选中（规格 §5 验收 4；Step1 实测量得 clicked=0）
        QVERIFY(bus.selectedIds().contains(QStringLiteral("id:dev-dbl-route")));
        QCOMPARE(static_cast<int>(visibleChips(bus).size()), 1);
    }

    // ══ v2.16-B 修复轮1（规格 §2.3/§5/§10 第 8 条）：移出选择改走尾部「×」命中区 ══
    // 缺陷根因（评审定档，勿再翻案）：b17d775 下第一次 release 的 clicked ⇒
    // m_selectedIds.remove ⇒ rebuildChips 同步 setVisible(false)（cpp:720-724+cpp:1212），
    // 真鼠标第二下 MouseButtonDblClick 永远落不到隐藏件上——双击功能对真实用户
    // 不可达；套件绿只是因为 QTest::mouseDClick 单事件直投绕开了可见性别拦。
    // 以下 ①② 按规格 §5 要求"实现前先行 RED"，取证口径＝完整事件序列 +
    // "可达性是被测件widget状态的性质"（单击后胶囊仍 isVisible()）。

    // ── ① 主体一次单击 ⇒ 选中集不变且胶囊仍可见（b17d775 上先行 RED）──
    void bodySingleClickKeepsSelectionAndChipVisible()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-body1"),
                                          QStringLiteral("主体单击守卫机"),
                                          QStringLiteral("10.8.20.1"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-body1"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();
        // 夹具前置：胶囊够宽，x=4（主体）与 x=宽-3（尾部「×」区）落进互不重叠的区域
        QVERIFY2(pill->width() >= 60,
                 qPrintable(QStringLiteral("夹具胶囊过窄（width=%1），无法分离主体/尾标注入点")
                                .arg(pill->width())));

        QSignalSpy selectionSpy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(selectionSpy.isValid());
        QSignalSpy openSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openSpy.isValid());

        // 定位注入：x=4 在主体（尾部「×」区按生产算式只贴右缘，最宽也远不到 4px）
        QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, QPoint(4, pill->height() / 2));

        QVERIFY2(!pill->isHidden(),
                 "主体单击后胶囊被隐藏——真指针再也打不中它，双击路径即断（规格 §10 第 8 条）");
        QVERIFY2(bus.selectedIds().contains(QStringLiteral("id:dev-body1")),
                 "主体单击不得改变选择（移出只走尾部「×」命中区，规格 §2.3）");
        QCOMPARE(selectionSpy.count(), 0);
        QCOMPARE(openSpy.count(), 0);   // 单击不得误触发「打开这台设备」
    }

    // ── ② 完整事件序列双击 ⇒ openDeviceRequested 恰好一次、选中仍不变 ──
    // Qt 只在平台事件分派器里合成 DblClick，QTest::mouseClick 连打两下产生不了它——
    // 第二下"按压"显式构造 QMouseEvent(MouseButtonDblClick) 经 QApplication::sendEvent
    // 投递（与真实通道等价：sendEvent 走 notify()，先过装在胶囊上的 eventFilter）。
    void fullDoubleClickSequenceEmitsOpenOnce()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile profile = makeProfile(QStringLiteral("dev-dblseq"),
                                            QStringLiteral("全序列双击机"),
                                            QStringLiteral("10.8.20.2"), 23,
                                            QStringLiteral("telnet"));
        QVERIFY(registry.save(profile));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-dblseq"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();
        QVERIFY2(pill->width() >= 60,
                 qPrintable(QStringLiteral("夹具胶囊过窄（width=%1）").arg(pill->width())));

        QSignalSpy selectionSpy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(selectionSpy.isValid());
        QSignalSpy openSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openSpy.isValid());

        const QPoint body(4, pill->height() / 2);
        // press → release：第一下落在主体
        QTest::mousePress(pill, Qt::LeftButton, Qt::NoModifier, body);
        QTest::mouseRelease(pill, Qt::LeftButton, Qt::NoModifier, body);
        // 可达性＝widget 状态的性质：与双击断言同槽，序列读作
        // press/release → 仍可见 → DblClick → 发射一次
        QVERIFY2(!pill->isHidden(),
                 "第一下 release 后胶囊已隐藏——真鼠标第二下 DblClick 不可达（规格 §10 第 8 条根因）");
        QVERIFY2(bus.selectedIds().contains(QStringLiteral("id:dev-dblseq")),
                 "双击序列的第一次主体 release 不得改变选择");

        // press(MouseButtonDblClick)：显式构造并投递
        QMouseEvent dbl(QEvent::MouseButtonDblClick,
                        QPointF(body),
                        QPointF(pill->mapToGlobal(body)),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pill, &dbl);
        // release（真双击收尾）
        QTest::mouseRelease(pill, Qt::LeftButton, Qt::NoModifier, body);

        QCOMPARE(openSpy.count(), 1);
        const DeviceProfile emitted = openSpy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(QString::fromStdString(emitted.deviceId), QStringLiteral("dev-dblseq"));
        QVERIFY2(bus.selectedIds().contains(QStringLiteral("id:dev-dblseq")),
                 "完整双击序列后选中集必须不变（规格 §5 验收 4）");
        QCOMPARE(selectionSpy.count(), 0);
        QVERIFY(!pill->isHidden());
    }

    // ── ③ 尾部「×」区一次单击 ⇒ 移出选择并广播恰好一次（+命中区实测数字留证）──
    void closeZoneClickRemovesSelection()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-xzone"),
                                          QStringLiteral("Times区移出机"),
                                          QStringLiteral("10.8.20.3"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-xzone"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();
        QVERIFY2(pill->width() >= 60,
                 qPrintable(QStringLiteral("夹具胶囊过窄（width=%1）").arg(pill->width())));

        // 取证（报告引用）：区宽实算＝「  ×」按胶囊自身 font 的字宽 + 样式内部 padding。
        // 注意 QSS 的 font-size 覆盖不进 QWidget::font()，本数字是默认字体口径。
        const QFontMetricsF fm(pill->font());
        const int padding = pill->style()->pixelMetric(QStyle::PM_ButtonMargin, nullptr, pill);
        qInfo().noquote() << QStringLiteral(
            "close-zone measure: mark-advance=%1px PM_ButtonMargin=%2px pill-width=%3px")
            .arg(fm.horizontalAdvance(QStringLiteral("  \xC3\x97")), 0, 'f', 2)
            .arg(padding)
            .arg(pill->width());

        QSignalSpy selectionSpy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(selectionSpy.isValid());

        // 注入点贴右缘 3px——独立于算式（不读 zone 宽度），几何上必须落进「×」区
        QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier,
                          QPoint(pill->width() - 3, pill->height() / 2));

        QVERIFY2(!bus.selectedIds().contains(QStringLiteral("id:dev-xzone")),
                 "尾部「×」区单击必须移出选择（规格 §2.3 订正本）");
        QCOMPARE(selectionSpy.count(), 1);
        QVERIFY2(pill->isHidden(), "移出后胶囊应收起（chips 只显已选）");
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 1);   // 硬线：全集不动
    }

    // ── ④ 键盘激活（Space/Enter 走 clicked，无鼠标事件）⇒ 仍移出：可达性不丢 ──
    // click() 是"无前置鼠标事件、直接 emit clicked"的程序化等价注入（与既有
    // :412/:811 槽的移出手法同通道）；真键盘 Space/Enter 同样以 clicked 收口。
    void keyboardActivationRemovesSelection()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-kbd"),
                                          QStringLiteral("键盘移出机"),
                                          QStringLiteral("10.8.20.4"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-kbd"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();

        QSignalSpy selectionSpy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(selectionSpy.isValid());

        pill->click();   // ≙ 键盘激活：clicked 在无鼠标事件下发出

        QVERIFY2(!bus.selectedIds().contains(QStringLiteral("id:dev-kbd")),
                 "键盘激活必须移出选择（空格/回车可达性，规格 §2.3/§5 ④）");
        QCOMPARE(selectionSpy.count(), 1);
        QVERIFY(pill->isHidden());
    }

    // ── ⑤ 重名 ·ip 尾标与长名称下「×」区仍贴右缘：区数学与文案长度无关 ──
    // 区按"按钮右缘起、覆盖『  ×』实宽+padding"几何计算，与文案无关：重名 chip
    // （文案加宽）与长名 chip（绘制可能截断）都必须——主体单击不动、贴右缘单击移出。
    void closeZoneAtRightEdgeForSuffixAndLongName()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-dup1"),
                                          QStringLiteral("同名机"),
                                          QStringLiteral("10.8.20.11"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-dup2"),
                                          QStringLiteral("同名机"),
                                          QStringLiteral("10.8.20.12"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-long"),
                                          QStringLiteral("很长很长很长很长很长很长很长很长很长很长很长很长很长机器"),
                                          QStringLiteral("10.8.20.13"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-dup1"));
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-dup2"));
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-long"));
        QCOMPARE(visibleChips(bus).size(), 3);

        auto chipFor = [this, &bus](const QString& deviceId) {
            for (QPushButton* c : visibleChips(bus))
                if (c->property("deviceId").toString() == deviceId)
                    return c;
            return static_cast<QPushButton*>(nullptr);
        };

        // 夹具有效性：重名机文案确实带 ·ip 短尾标（规格 §2.3），长名机文案超 20 字符
        QPushButton* dup1 = chipFor(QStringLiteral("dev-dup1"));
        QVERIFY(dup1);
        QVERIFY2(dup1->text().contains(QStringLiteral(" ·10.8.20.11")),
                 qPrintable(QStringLiteral("重名 chip 应带 ·ip 尾标，实际: %1").arg(dup1->text())));
        QPushButton* longChip = chipFor(QStringLiteral("dev-long"));
        QVERIFY(longChip);
        QVERIFY2(longChip->text().length() > 20,
                 qPrintable(QStringLiteral("长名夹具不成立: %1").arg(longChip->text())));

        const QStringList ids{QStringLiteral("dev-dup1"), QStringLiteral("dev-dup2"),
                              QStringLiteral("dev-long")};
        for (const QString& id : ids) {
            QPushButton* chip = chipFor(id);
            QVERIFY(chip);
            QVERIFY2(chip->width() >= 60,
                     qPrintable(QStringLiteral("夹具胶囊过窄（%1 width=%2）")
                                    .arg(id).arg(chip->width())));
            // 主体（左缘 4px）：不改选择
            QTest::mouseClick(chip, Qt::LeftButton, Qt::NoModifier,
                              QPoint(4, chip->height() / 2));
            QVERIFY2(bus.selectedIds().contains(QStringLiteral("id:") + id),
                     qPrintable(QStringLiteral("%1 主体单击不得移出选择").arg(id)));
            // 尾部右缘 3px：必须移出（「×」贴右缘的数学与文案长度无关）
            QTest::mouseClick(chip, Qt::LeftButton, Qt::NoModifier,
                              QPoint(chip->width() - 3, chip->height() / 2));
            QVERIFY2(!bus.selectedIds().contains(QStringLiteral("id:") + id),
                     qPrintable(QStringLiteral("%1 尾部「×」区单击必须移出选择").arg(id)));
        }
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 3);   // 硬线
    }

    // ── 修复轮1 finding2：「移除设备」role 真实分派并删除档案（补声称覆盖的缺口）──
    // 任务5报告 §1 曾把该角色列为已覆盖，实为无用例派发 role "remove"——它会挡在
    // 模态 QMessageBox::question（cpp:776）上。本轮按同文件模板删除槽的先例
    // （timer 布防应答 QMessageBox）钉桩：布防 → 分派（question 进嵌套事件循环 →
    // 定时器点「是」）→ 档案真删（总线视图/registry/ConfigStore 三处齐判）。
    void removeMenuActionConfirmsAndDeletesProfile()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-rmx"),
                                          QStringLiteral("删除试验台"),
                                          QStringLiteral("10.8.20.9"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-rmx"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();

        m_removeBoxText.clear();
        m_removeBoxAnswered = false;
        QTimer::singleShot(60, m_timerGuard.get(), [this] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (auto* box = qobject_cast<QMessageBox*>(w)) {
                    m_removeBoxText = box->text();
                    // 真用户等价：点 Yes 按钮本体（QMessageBox::done(StandardButton)
                    // 在本轮实测中不映射为 question() 的返回值——只有按钮 clicked
                    // 路径才走 QMessageBox 内部的 standardButton 回报，见修复报告）
                    if (auto* yes = box->button(QMessageBox::Yes))
                        yes->click();
                    m_removeBoxAnswered = true;
                    return;
                }
            }
        });

        // 同步分派：dispatchPillMenuAction("remove") 内 question() 阻塞在嵌套循环，
        // 布防定时器在循环里兑现「是」→ removeDevice 删档（pill 随之析构，
        // 本槽此后不再解引用它）
        triggerPillMenuAction(bus, pill, QStringLiteral("移除设备"));
        // 缝守卫先行（minor4 同法）：任何等待/断言之前，缝失效当场指名
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QVERIFY2(!m_menuActionMissing, "右键菜单缺少「移除设备」项");
        QVERIFY2(m_menuTriggered, "「移除设备」项未被触发");
        QVERIFY2(m_removeBoxAnswered,
                 "分派「移除设备」未弹确认框——对话框漂移或分派被旁路");
        QVERIFY2(m_removeBoxText.contains(QStringLiteral("10.8.20.9")),
                 qPrintable(QStringLiteral("确认框文案应含设备地址，实际: %1").arg(m_removeBoxText)));
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 0);
        QVERIFY(!registry.find("dev-rmx").has_value());
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("device.profile"),
                                                QStringLiteral("dev-rmx")));
    }

    // ── v2.16-B 任务5：右键菜单次序 + 无注册表形态（规格 §6② 登记的行为变更）──
    void rightClickMenuOpensTopItemAndNoRegistryShape()
    {
        // 形态A：接入注册表——三项按「打开这台设备／编辑档案／移除设备」次序，
        // 首项触发即发射 openDeviceRequested
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-menu"),
                                          QStringLiteral("菜单形态机"),
                                          QStringLiteral("10.8.0.2"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-menu"));
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);

        QSignalSpy openSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openSpy.isValid());
        triggerPillMenuAction(bus, chips.first(), QStringLiteral("打开这台设备"));
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QVERIFY2(m_menuTriggered,
                 qPrintable(QStringLiteral("右键菜单缺少「打开这台设备」首项，实际项: %1")
                                .arg(m_menuCapturedTexts.join(QStringLiteral(" | ")))));
        QCOMPARE(m_menuCapturedTexts,
                 (QStringList{QStringLiteral("打开这台设备"),
                              QStringLiteral("编辑档案"),
                              QStringLiteral("移除设备")}));
        QCOMPARE(openSpy.count(), 1);
        QCOMPARE(QString::fromStdString(openSpy.at(0).at(0).value<DeviceProfile>().deviceId),
                 QStringLiteral("dev-menu"));

        // 形态B：无注册表（旧 ip:port 胶囊）——「打开这台设备」仍在首且仍发射
        //（旧版此处只有「移除设备」；生产恒注入注册表，本形态是单测/降级形态，
        //  规格 §6② 登记的行为变更；「编辑档案」依旧缺席=门槛 m_registry 不变）；
        // 载荷经 fromDeviceInfo 合成：协议列为空 → routeTo 按 ftp 兜底（§2.2）
        DeviceBusWidget bare;
        bare.resize(1400, 100);
        DeviceInfo device;
        device.ip = "192.168.5.5";
        device.port = 21;
        bare.addDevice(device);
        selectDeviceViaMenu(bare, QStringLiteral("ip:192.168.5.5"));
        const auto bareChips = visibleChips(bare);
        QVERIFY(bareChips.size() == 1);
        QSignalSpy bareOpenSpy(&bare, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(bareOpenSpy.isValid());
        triggerPillMenuAction(bare, bareChips.first(), QStringLiteral("打开这台设备"));
        QVERIFY2(!m_menuNotFound, "无注册表形态 buildPillMenu 直调缝未返回菜单（结果不可信）");
        QVERIFY2(m_menuTriggered,
                 qPrintable(QStringLiteral("无注册表菜单缺少「打开这台设备」首项，实际项: %1")
                                .arg(m_menuCapturedTexts.join(QStringLiteral(" | ")))));
        QCOMPARE(m_menuCapturedTexts,
                 (QStringList{QStringLiteral("打开这台设备"),
                              QStringLiteral("移除设备")}));
        QCOMPARE(bareOpenSpy.count(), 1);
        const DeviceProfile synthesized = bareOpenSpy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(synthesized.endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(synthesized.endpoints.front().ip),
                 QStringLiteral("192.168.5.5"));
        const deviceopen::Route route = deviceopen::routeTo(synthesized);
        QVERIFY(route.page == deviceopen::Page::FtpDeploy);
    }

    // ── v2.16-B 任务5：旧 ip:port 行右键「编辑档案」仍可达（规格 §3.1 降级用例）──
    // device.list 旧行（无 device.profile 记录）经注册表兼容迁移成档案后，
    // 「编辑档案」项照常显示、保存成功并发射 deviceProfileEdited——让位双击
    // 不丢任何编辑能力（§10 第 5 条实测结论的回归钉桩）。
    void legacyIpPortPillStillEditableViaMenu()
    {
        QVariantMap legacyRow;
        legacyRow.insert(QStringLiteral("ip"), QStringLiteral("10.7.0.1"));
        legacyRow.insert(QStringLiteral("port"), 21);
        legacyRow.insert(QStringLiteral("displayName"), QStringLiteral("旧行设备"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.7.0.1:21"), legacyRow));
        DeviceRegistry registry;
        QVERIFY(registry.load());   // 旧行迁移为档案（端点身份确定性 id）

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        QPushButton* legacyPill = nullptr;
        const auto pills = bus.findChildren<QPushButton*>(QStringLiteral("devicePill"));
        for (QPushButton* p : pills) {
            if (p->property("deviceIp").toString() == QStringLiteral("10.7.0.1"))
                legacyPill = p;
        }
        QVERIFY(legacyPill);
        // 选中键按迁移档案的实际 id 组（不硬编码 stableId 散列值）
        const QString key = QStringLiteral("id:") + legacyPill->property("deviceId").toString();
        selectDeviceViaMenu(bus, key);
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        QPushButton* pill = chips.first();

        QSignalSpy editSpy(&bus, &DeviceBusWidget::deviceProfileEdited);
        QVERIFY(editSpy.isValid());
        m_legacyNote.clear();
        // 布防：对话框改名后保存（150ms——晚于菜单缝分派后对话框的出现点）
        QTimer::singleShot(150, m_timerGuard.get(), [this] {
            QDialog* dialog = nullptr;
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor")) {
                    dialog = qobject_cast<QDialog*>(w);
                    break;
                }
            }
            if (!dialog) {
                QString tops;
                const auto ws = QApplication::topLevelWidgets();
                for (QWidget* x : ws)
                    tops += QStringLiteral("<%1:%2>").arg(QString::fromLatin1(x->metaObject()->className()), x->objectName());
                m_legacyNote += QStringLiteral("[150:no-dialog tops=%1]").arg(tops);
                return;
            }
            auto* nameEdit = dialog->findChild<QLineEdit*>(QStringLiteral("deviceProfileName"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("deviceProfileSave"));
            if (!nameEdit || !save) {
                m_legacyNote += QStringLiteral("[150:no-widgets]");
                return;
            }
            m_legacyNote += QStringLiteral("[150:dialog]");
            nameEdit->setText(QStringLiteral("旧行已编辑"));
            save->click();
        });
        // 400ms 探针+应答：#46 端点同键确认框旁路。本槽实测定档不弹（迁移档案
        // id 非空自排除）；保留应答是为了万一弹框时按"可保存但需确认"既有语义
        // 答"是"继续保存而非挂死（默认钮"否"会循环重开对话框），并把实况记进
        // m_legacyNote 随成功路径 qInfo 留证
        QTimer::singleShot(400, m_timerGuard.get(), [this] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (auto* box = qobject_cast<QMessageBox*>(w)) {
                    m_legacyNote += QStringLiteral("[400:msgbox %1]")
                                            .arg(box->text().left(40));
                    // 修复轮1：QMessageBox::done(StandardButton) 实测不映射为
                    // question() 的返回值（移除确认框同型缺陷当场撞出，见本轮报告），
                    // 真用户等价=点 Yes 按钮本体；无 Yes 按钮时与原 done 一样静默
                    if (auto* yes = box->button(QMessageBox::Yes))
                        yes->click();
                    return;
                }
            }
        });
        QTimer::singleShot(3000, m_timerGuard.get(), [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (w->objectName() == QStringLiteral("deviceProfileEditor"))
                    w->close();
            }
        });

        openEditMenu(bus, pill);
        // 修复轮1 minor4：缝守卫前移到任何等待之前（缝失效当场指名，不退化成泛用超时）
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QVERIFY2(!m_menuActionMissing, "旧行胶囊右键菜单缺少「编辑档案」项");
        QVERIFY2(m_menuTriggered, "「编辑档案」项未被触发");
        qInfo().noquote() << QStringLiteral("legacy-menu post-seam notFound=%1 missing=%2 triggered=%3 spy=%4")
                                 .arg(m_menuNotFound ? 1 : 0).arg(m_menuActionMissing ? 1 : 0)
                                 .arg(m_menuTriggered ? 1 : 0).arg(editSpy.count());
        QTRY_VERIFY2_WITH_TIMEOUT(editSpy.count() == 1,
                                  qPrintable(QStringLiteral("等待 deviceProfileEdited 超时（editSpy=%1 note=%2）")
                                                 .arg(editSpy.count()).arg(m_legacyNote)),
                                  5000);
        qInfo().noquote() << QStringLiteral("legacy-menu-chain note: %1").arg(m_legacyNote);

        const DeviceProfile saved = editSpy.at(0).at(0).value<DeviceProfile>();
        QCOMPARE(QString::fromStdString(saved.name), QStringLiteral("旧行已编辑"));
        QCOMPARE(saved.endpoints.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(saved.endpoints.front().ip),
                 QStringLiteral("10.7.0.1"));
        // 落库可回读（编辑真实经 registry 持久化，不是只发信号）
        QVERIFY(ConfigStore::instance().exists(QStringLiteral("device.profile"),
                                               QString::fromStdString(saved.deviceId)));
    }

    // ── v2.16-B 任务5：profileForPill 契约——档案查不到一律不发射（缺陷2钉桩）──
    // 「打开这台设备」分派对不可解析胶囊必须早退，绝不发默认构造的空档案
    //（ip 空、无端点）——否则任务 6 会把空载荷路由到 Page::None，用户双击却收到
    //「该设备没有可用的协议端点」提示。两条路径都要一致：旧行 ip 不在设备全集 /
    // deviceId 在注册表里查不到。
    void unresolvableProfileEmitsNothing()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-real"),
                                          QStringLiteral("真实机"),
                                          QStringLiteral("10.4.0.1"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-real"));

        QSignalSpy openSpy(&bus, &DeviceBusWidget::openDeviceRequested);
        QVERIFY(openSpy.isValid());

        // 路径A：无 deviceId、deviceIp 又不在总线设备全集 → profileForPill 合成回查
        // 未命中 → 空 optional → 分派早退。修复前该路径 fall through 返回默认构造空
        // 档案并发射 openDeviceRequested（openSpy==1），本用例即判红。
        // 幽灵胶囊不设 objectName="devicePill"：dispatch 只读属性、不查对象名
        //（查对象名的是 eventFilter 双击路径），且避免 visibleChips 误计入可见 chips。
        QPushButton legacyGhost(&bus);
        legacyGhost.setProperty("deviceIp", QStringLiteral("10.99.99.99"));   // 不在 m_devices
        QMetaObject::invokeMethod(&bus, "dispatchPillMenuAction", Qt::DirectConnection,
                                  Q_ARG(QPushButton*, &legacyGhost),
                                  Q_ARG(QString, QStringLiteral("open")));

        // 路径B：带 deviceId 但注册表里没有该档案 → registry->find 未命中 → 早退
        //（这条修复前后皆 nullopt，作为契约一致性的并列钉桩，非缺陷针）
        QPushButton idGhost(&bus);
        idGhost.setProperty("deviceId", QStringLiteral("no-such-device-id"));
        QMetaObject::invokeMethod(&bus, "dispatchPillMenuAction", Qt::DirectConnection,
                                  Q_ARG(QPushButton*, &idGhost),
                                  Q_ARG(QString, QStringLiteral("open")));

        QCOMPARE(openSpy.count(), 0);
        // 对照：真实胶囊仍可发射（证明上面两次早退是"查不到"而非缝本身坏了）
        const auto chips = visibleChips(bus);
        QVERIFY(chips.size() == 1);
        triggerPillMenuAction(bus, chips.first(), QStringLiteral("打开这台设备"));
        QVERIFY2(!m_menuNotFound, "buildPillMenu 直调缝未返回菜单（本用例结果不可信）");
        QCOMPARE(openSpy.count(), 1);
    }

    // ── v2.16-B 任务4 Step1 硬线：allDevices() 恒为全集（chips 只是"已选"的视图）──
    // Telnet 批量命令页（TelnetWidget.cpp:252）读的就是 allDevices()；搜索过滤、
    // 打开菜单、全选、清空任何组合都不得缩小它（规格 §2.4 / §3）。
    void allDevicesStaysFullRegardlessOfSelectionAndFilter()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        for (int i = 0; i < 6; ++i)
            QVERIFY(registry.save(makeProfile(QStringLiteral("dev-%1").arg(i),
                                              QStringLiteral("设备%1").arg(i),
                                              QStringLiteral("10.0.0.%1").arg(i + 1), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 6);   // 未选任何一台，全集仍是 6

        // 搜索框过滤 + 全选 + 清空，三种操作之后都不得改变 allDevices()。
        // 过滤词取 6 台名称的共同前缀「设备」：过滤后 6 台全部可见（规格 §2.3
        // 过滤只改菜单可见项；「全选可见」作用可见集——此处可见=全集）。
        auto* edit = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
        if (edit)
            edit->setText(QStringLiteral("设备"));
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 6);
        QSignalSpy spy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(spy.isValid());
        bus.selectAllVisible();                                   // 菜单底部「全选可见 N 台」同一条路径
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 6);   // 硬线
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 6);
        if (edit)
            edit->setText(QString());
        bus.clearSelection();                                     // 菜单底部「清空选择」
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 6);   // 硬线
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 0);
    }

    // ── v2.16-B 任务4 Step4：「设备 ▾」弹层结构（分组即子菜单，勾选即选中）──
    // 规格 §2.3：每次 aboutToShow 重建；主菜单一行一个分组「组名 命中/总数」；
    // 「未分组」最后；底部两动作在分隔线之后。夹具经 invokeMethod 直发
    // aboutToShow/aboutToHide（与真实开合菜单走同一批接线，无窗口弹层依赖）。
    void pickerMenuGroupsCountedAndUngroupedLast()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile a1 = makeProfile(QStringLiteral("dev-a1"), QStringLiteral("甲一"),
                                       QStringLiteral("10.8.0.1"), 21);
        a1.tags = {"车间A"};
        DeviceProfile a2 = makeProfile(QStringLiteral("dev-a2"), QStringLiteral("甲二"),
                                       QStringLiteral("10.8.0.2"), 21);
        a2.tags = {QStringLiteral("车间A").toUtf8().toStdString()};
        DeviceProfile b1 = makeProfile(QStringLiteral("dev-b1"), QStringLiteral("乙一"),
                                       QStringLiteral("10.8.0.3"), 21);
        b1.tags = {"A线"};
        DeviceProfile c1 = makeProfile(QStringLiteral("dev-c1"), QStringLiteral("丙一"),
                                       QStringLiteral("10.8.0.4"), 21);   // 无标签 → 未分组
        QVERIFY(registry.save(a1));
        QVERIFY(registry.save(a2));
        QVERIFY(registry.save(b1));
        QVERIFY(registry.save(c1));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");

        const auto tops = menu->actions();
        // 3 分组项 + 分隔线 + 全选可见 + 清空选择
        QCOMPARE(tops.size(), 6);
        // 组序：标签升序区分大小写（'A'(U+0041) < 车(U+8F66)），「未分组」永远最后
        QCOMPARE(tops.at(0)->text(), QStringLiteral("A线 1/1"));
        QCOMPARE(tops.at(1)->text(), QStringLiteral("车间A 2/2"));
        QCOMPARE(tops.at(2)->text(), QStringLiteral("未分组 1/1"));
        QVERIFY(tops.at(2)->menu());
        QVERIFY(tops.at(3)->isSeparator());
        QCOMPARE(tops.at(4)->text(), QStringLiteral("全选可见 4 台"));
        QCOMPARE(tops.at(5)->text(), QStringLiteral("清空选择"));

        // 子菜单成员 = registry 顺序，checkable、初始未勾、deviceKey 属性、端点 tooltip
        const auto members = tops.at(1)->menu()->actions();
        QCOMPARE(members.size(), 2);
        QVERIFY(members.at(0)->isCheckable());
        QVERIFY(!members.at(0)->isChecked());
        QCOMPARE(members.at(0)->property("deviceKey").toString(), QStringLiteral("id:dev-a1"));
        QCOMPARE(members.at(1)->property("deviceKey").toString(), QStringLiteral("id:dev-a2"));
        QVERIFY2(members.at(0)->toolTip().contains(QStringLiteral("ftp 10.8.0.1:21")),
                 qPrintable(members.at(0)->toolTip()));
    }

    // 勾选风暴：连勾 5 台只在菜单关闭时广播一次（规格 §3.8）；
    // 重开时初始 checked 态由集合回灌，既不误广播也不把已选勾掉。
    void menuCheckboxStormBroadcastsOnce()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        for (int i = 0; i < 5; ++i) {
            DeviceProfile p = makeProfile(QStringLiteral("dev-s%1").arg(i),
                                          QStringLiteral("闪%1").arg(i),
                                          QStringLiteral("10.8.1.%1").arg(i + 1), 21);
            p.tags = {i % 2 == 0 ? "组X" : "组Y"};   // 两台一组，跨组连勾
            QVERIFY(registry.save(p));
        }
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QSignalSpy spy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(spy.isValid());

        QMetaObject::invokeMethod(menu, "aboutToShow");
        const QList<QAction*> members = menuMemberActions(menu);
        QCOMPARE(members.size(), 5);
        for (QAction* act : members)
            act->toggle();
        QCOMPARE(spy.count(), 0);                        // 勾满也不得逐台广播
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(spy.count(), 1);                        // 整场只广播 1 次
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 5);

        // 重开：checked==集合成员回灌；构建初始 setChecked 不得触发 toggled 广播，
        // 关闭也不得产生伪广播（spy 仍为 1）
        QMetaObject::invokeMethod(menu, "aboutToShow");
        const QList<QAction*> reopened = menuMemberActions(menu);
        QCOMPARE(reopened.size(), 5);
        for (QAction* act : reopened)
            QVERIFY2(act->isChecked(), qPrintable(act->text()));
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(spy.count(), 1);
    }

    // 多标签设备在两个分组各出现一次，只勾一处 ⇒ 选中集合 1 台（去重由显式 id
    // 集合结构保证，规格 §2.4）；重开菜单两处 checked 态一致（都==成员判定）。
    void multiTagDeviceSelectedOnceAcrossGroups()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile dual = makeProfile(QStringLiteral("dev-dual"), QStringLiteral("双子星"),
                                         QStringLiteral("10.8.2.1"), 21);
        dual.tags = {"组A", "组B"};
        QVERIFY(registry.save(dual));
        DeviceProfile onlyB = makeProfile(QStringLiteral("dev-b"), QStringLiteral("独B"),
                                          QStringLiteral("10.8.2.2"), 21);
        onlyB.tags = {"组B"};
        QVERIFY(registry.save(onlyB));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");

        QList<QAction*> dualActs;
        const QList<QAction*> members = menuMemberActions(menu);
        for (QAction* act : members) {
            if (act->property("deviceKey").toString() == QStringLiteral("id:dev-dual"))
                dualActs.append(act);
        }
        QCOMPARE(dualActs.size(), 2);                    // 组A、组B 各出现一次
        dualActs.first()->toggle();                      // 只在一处勾
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 1);   // 集合里仍是 1 台
        QCOMPARE(QString::fromStdString(bus.selectedDeviceProfiles().front().deviceId),
                 QStringLiteral("dev-dual"));

        QMetaObject::invokeMethod(menu, "aboutToShow");  // 重建后两处都反映成员判定
        QList<QAction*> reopened;
        for (QAction* act : menuMemberActions(menu)) {
            if (act->property("deviceKey").toString() == QStringLiteral("id:dev-dual"))
                reopened.append(act);
        }
        QCOMPARE(reopened.size(), 2);
        QVERIFY(reopened.at(0)->isChecked());
        QVERIFY(reopened.at(1)->isChecked());
    }

    // 底部动作只作用当前过滤可见集；全不命中时两条一并撤下（规格 §3.4）；
    // 过滤与开合菜单全程不清空既有选择、不改 allDevices()。
    void menuBottomActionsVisibleOnlyAndWithdrawn()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile jia = makeProfile(QStringLiteral("dev-jia"), QStringLiteral("甲"),
                                        QStringLiteral("10.8.3.1"), 21);
        jia.tags = {"组A"};
        QVERIFY(registry.save(jia));
        DeviceProfile yi = makeProfile(QStringLiteral("dev-yi"), QStringLiteral("乙"),
                                       QStringLiteral("10.8.3.2"), 21);
        yi.tags = {"组B"};
        QVERIFY(registry.save(yi));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        auto* edit = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
        QVERIFY(edit);
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-jia"));   // 先选中甲
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 1);

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(spy.isValid());

        // 全不命中：只留一行不可点说明，底部两动作撤下；既有选择纹丝不动
        edit->setText(QStringLiteral("zzz不存在"));
        QMetaObject::invokeMethod(menu, "aboutToShow");
        const auto tops = menu->actions();
        QCOMPARE(tops.size(), 1);
        QVERIFY(!tops.at(0)->isEnabled());
        QVERIFY2(tops.at(0)->text().contains(QStringLiteral("无匹配设备（2 台）")),
                 qPrintable(tops.at(0)->text()));
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 1);   // 绝不清空
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 2);

        // 只命中乙：菜单只剩组B，「全选可见 1 台」只补乙这一台（甲不被动出集合）
        edit->setText(QStringLiteral("乙"));
        QMetaObject::invokeMethod(menu, "aboutToShow");
        const auto filtered = menu->actions();
        QCOMPARE(filtered.size(), 4);                    // 组B + 分隔 + 全选可见 1 台 + 清空
        QCOMPARE(filtered.at(0)->text(), QStringLiteral("组B 1/1"));
        QCOMPARE(filtered.at(2)->text(), QStringLiteral("全选可见 1 台"));
        filtered.at(2)->trigger();
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 2);   // 甲仍在 + 乙补入
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 2);
        QCOMPARE(spy.count(), 1);                        // 全选动作整场只广播一次
    }

    // ── 修复轮1 finding1：混合菜单会话的单广播规矩 ──
    // 同一场会话里先勾成员（置 pending），再点底部动作（自身即时广播）——
    // aboutToHide 不得因残留 pending 补发第二次广播（规格 §2.3/§3.8：一场一次）。
    void menuBottomActionAfterTickBroadcastsOnce()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        for (int i = 0; i < 3; ++i)
            QVERIFY(registry.save(makeProfile(QStringLiteral("dev-x%1").arg(i),
                                              QStringLiteral("混%1").arg(i),
                                              QStringLiteral("10.8.8.%1").arg(i + 1), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QSignalSpy spy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(spy.isValid());

        auto bottomAction = [menu](const QString& prefix) -> QAction* {
            const auto acts = menu->actions();
            for (QAction* a : acts) {
                if (a->text().startsWith(prefix))
                    return a;
            }
            return nullptr;
        };

        // 场次1：勾一台成员 → 同场点「全选可见」→ 关菜单。整场只许 1 次广播。
        QMetaObject::invokeMethod(menu, "aboutToShow");
        menuMemberActions(menu).first()->toggle();       // toggled → pending=true
        QAction* selectAll = bottomAction(QStringLiteral("全选可见"));
        QVERIFY(selectAll);
        selectAll->trigger();                             // 动作自身即时广播
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(spy.count(), 1);                         // 缺陷形态：pending 未清 → 2
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 3);

        // 场次2：勾掉一台（全勾态下 toggle=取消）→ 同场点「清空选择」→ 关菜单。
        QMetaObject::invokeMethod(menu, "aboutToShow");
        menuMemberActions(menu).first()->toggle();
        QAction* clearAll = bottomAction(QStringLiteral("清空选择"));
        QVERIFY(clearAll);
        clearAll->trigger();
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(spy.count(), 2);                         // 第二场同样只补 1 次
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 0);
    }

    // ── 修复轮1 finding2+3：过滤态「清空选择」清的是全量集合，且文案必须自述范围 ──
    // 规格 §2.3（2026-10-21 订正本）：两条底部动作作用域刻意不对称——全选受可见边界，
    // 清空是逃生门清全部；过滤生效时清空项文案 `清空选择（全部 M 台）`（M=已选总数），
    // 无过滤时保持朴素 `清空选择`。行为与两条文案（两分支）在此同批钉桩。
    void clearUnderFilterDropsWholeSetAndLabelStatesScope()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile jia = makeProfile(QStringLiteral("dev-jia"), QStringLiteral("甲"),
                                        QStringLiteral("10.8.9.1"), 21);
        jia.tags = {"组A"};
        QVERIFY(registry.save(jia));
        DeviceProfile yi = makeProfile(QStringLiteral("dev-yi"), QStringLiteral("乙"),
                                       QStringLiteral("10.8.9.2"), 21);
        yi.tags = {"组B"};
        QVERIFY(registry.save(yi));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        auto* edit = bus.findChild<QLineEdit*>(QStringLiteral("deviceSearchEdit"));
        QVERIFY(edit);
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);

        // 先（无过滤）经菜单勾中甲+乙
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-jia"));
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-yi"));
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 2);

        // 过滤「乙」：甲被排除在菜单可见集之外，但仍是已选
        edit->setText(QStringLiteral("乙"));
        QMetaObject::invokeMethod(menu, "aboutToShow");
        const auto tops = menu->actions();
        QCOMPARE(tops.size(), 4);                         // 组B + 分隔 + 全选可见 1 台 + 清空
        QCOMPARE(tops.at(2)->text(), QStringLiteral("全选可见 1 台"));
        // finding3：过滤态清空项文案必须带范围说明（M=已选总数=2，含被过滤掉的甲）
        QCOMPARE(tops.at(3)->text(), QStringLiteral("清空选择（全部 2 台）"));
        // finding2：经真实菜单项触发清空
        tops.at(3)->trigger();
        QMetaObject::invokeMethod(menu, "aboutToHide");
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 0);   // 全量清空：不可见的甲也被清掉
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 2);        // 硬线：全集纹丝不动

        // finding3 另一分支：无过滤时清空项保持朴素「清空选择」
        edit->setText(QString());
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QAction* plain = nullptr;
        const auto acts = menu->actions();
        for (QAction* a : acts) {
            if (a->text() == QStringLiteral("清空选择"))
                plain = a;
        }
        QVERIFY2(plain, qPrintable(QStringLiteral("无过滤时清空项应保持朴素文案，实际末项: %1")
                                       .arg(acts.isEmpty() ? QStringLiteral("(空)")
                                                           : acts.last()->text())));
        QMetaObject::invokeMethod(menu, "aboutToHide");
    }

    // ── v2.16-B 任务4 Step5：chips 只显已选（视觉态只切可见性）+ 左键移出选择 ──
    void chipsShowOnlySelectedDevices()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-c0"), QStringLiteral("零号"),
                                           QStringLiteral("10.8.4.1"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-c1"), QStringLiteral("壹号"),
                                           QStringLiteral("10.8.4.2"), 21)));

        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);           // 预算=自身宽度−固定控件（隐藏窗口同样成立，与屏宽无关）
        auto* hint = bus.findChild<QLabel*>(QStringLiteral("deviceBusHint"));
        QVERIFY(hint);
        QVERIFY2(hint->text().contains(QStringLiteral("未选择设备")), qPrintable(hint->text()));
        auto* picker = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(picker);
        QCOMPARE(picker->text(), QStringLiteral("设备 ▾ 未选择"));

        // 未勾任何台：第 1 行没有任何可见 chips（胶囊仍在，只是收起——视觉态不删数据）
        QCOMPARE(visibleChips(bus).size(), 0);

        selectDeviceViaMenu(bus, QStringLiteral("id:dev-c1"));
        const auto chips = visibleChips(bus);
        QCOMPARE(chips.size(), 1);
        QCOMPARE(chips.first()->property("deviceId").toString(), QStringLiteral("dev-c1"));
        QVERIFY(chips.first()->property("selected").toBool());
        QVERIFY(hint->isHidden());                       // 已选 → 空态提示让位
        QCOMPARE(picker->text(), QStringLiteral("设备 ▾ 已选 1 台"));

        bus.clearSelection();
        QCOMPARE(visibleChips(bus).size(), 0);
        QVERIFY(!hint->isHidden());
        QCOMPARE(picker->text(), QStringLiteral("设备 ▾ 未选择"));
    }

    // 左键单击 chip＝移出选择（chips 的存在本身即选中，切换语义不再成立，§2.3）；
    // 只移那一条，广播一次，allDevices() 不动。
    void chipClickRemovesOnlyThatDevice()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        for (int i = 0; i < 3; ++i)
            QVERIFY(registry.save(makeProfile(QStringLiteral("dev-m%1").arg(i),
                                              QStringLiteral("中%1").arg(i),
                                              QStringLiteral("10.8.5.%1").arg(i + 1), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);           // 3 枚 chips 的行宽预算充分（与屏宽无关，见上条同款）
        selectAllViaMenu(bus);                           // 勾 3 台
        QCOMPARE(visibleChips(bus).size(), 3);
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 3);

        QSignalSpy spy(&bus, &DeviceBusWidget::deviceSelectionChanged);
        QVERIFY(spy.isValid());
        QPushButton* middle = nullptr;
        for (QPushButton* chip : visibleChips(bus)) {
            if (chip->property("deviceId").toString() == QStringLiteral("dev-m1"))
                middle = chip;
        }
        QVERIFY(middle);
        middle->click();   // 键盘等价路径移出（修复轮1：真鼠标左键移出走尾部「×」区）
        QCOMPARE(spy.count(), 1);                        // 广播恰好一次
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 2);
        QCOMPARE(visibleChips(bus).size(), 2);
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 3);   // 硬线
        for (const auto& d : bus.selectedDevices())
            QVERIFY(QString::fromStdString(d.ip) != QStringLiteral("10.8.5.2"));
    }

    // 溢出折算：80 台全选 → 可见 chips + M == 80（守恒，不判具体枚数，
    // tests/AGENTS.md 约定 6）；「+M 台」与「设备 ▾」是同一个菜单实例。
    // 折叠分支可达性由窗口显式后布局兑现（QTRY 等待 fold 出现，非 sleep 轮询）。
    void chipOverflowPreservesCountAndOpensSameMenu()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        for (int i = 0; i < 80; ++i)
            QVERIFY(registry.save(makeProfile(QStringLiteral("dev-o%1").arg(i),
                                              QStringLiteral("溢%1").arg(i),
                                              QStringLiteral("10.9.%1").arg(i / 250)
                                                  + QStringLiteral(".%2").arg(i % 250 + 1), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.selectAllVisible();
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 80);

        auto* moreBtn = bus.findChild<QPushButton*>(QStringLiteral("devicePillMore"));
        QVERIFY(moreBtn);
        auto* picker = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(picker);
        QVERIFY(moreBtn->menu() == picker->menu());      // 同一个菜单（不是复制品）

        // 行宽预算不足（420 − 固定控件 ⇒ 预算 < 单枚 chips 宽）：折叠分支确定性走到，
        // 不依赖 show()/屏宽/布局瞬态（budget 由自身宽度算，见产品注释）。
        // 若未实现折叠，moreBtn 保持隐藏 → 本用例红（非虚设性由产品侧 qInfo 打点佐证）。
        bus.resize(420, 100);
        bus.selectAllVisible();
        QVERIFY2(!moreBtn->isHidden(),
                 qPrintable(QStringLiteral("+M 台按钮未出现，moreBtn text=%1").arg(moreBtn->text())));
        const int visibleCount = static_cast<int>(visibleChips(bus).size());
        const QString moreText = moreBtn->text();
        QVERIFY2(moreText.startsWith(QStringLiteral("+")) && moreText.endsWith(QStringLiteral(" 台")),
                 qPrintable(moreText));
        const int folded = moreText.mid(1, moreText.size() - 1 - QStringLiteral(" 台").size()).toInt();
        QVERIFY2(folded > 0, qPrintable(QStringLiteral("+M 台 M 必须为正，实际文本: %1").arg(moreText)));
        QCOMPARE(visibleCount + folded, 80);             // 守恒（不断具体枚数）
        qInfo("overflow branch: shown=%d folded=%d", visibleCount, folded);

        // 收起 chips 只是视觉收纳：集合与全集都不动（视觉态只切可见性）
        QCOMPARE(static_cast<int>(bus.selectedDevices().size()), 80);
        QCOMPARE(static_cast<int>(bus.allDevices().size()), 80);

        // 放宽行宽 → 同一算法重算，收敛回可见（resizeEvent 重算路径的确定性形态）
        bus.resize(2400, 100);
        bus.selectAllVisible();
        QCOMPARE(static_cast<int>(visibleChips(bus).size())
                     + moreBtn->text().mid(1, moreBtn->text().size() - 3).toInt(), 80);
    }

    // ── v2.16-B 任务4 Step6：胶囊文案只显名称（去 ip:port）；空名回落地址；仅重名加 ·ip ──
    void chipCaptionNameOnlyWithoutIpPort()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-n1"), QStringLiteral("独名一号"),
                                           QStringLiteral("10.8.6.1"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-n2"), QStringLiteral("独名二号"),
                                           QStringLiteral("10.8.6.2"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-n1"));
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-n2"));

        const auto chips = visibleChips(bus);
        QCOMPARE(chips.size(), 2);
        for (QPushButton* chip : chips) {
            QVERIFY2(chip->text().endsWith(QStringLiteral("  ×")), qPrintable(chip->text()));  // ×保留（裁定①）
            QVERIFY2(!chip->text().contains(QStringLiteral("10.8.6")),   // 不再带 ip:port（§2.3）
                     qPrintable(chip->text()));
            QVERIFY2(!chip->text().contains(QStringLiteral("·")),        // 不重名不加尾标
                     qPrintable(chip->text()));
            // 地址改由悬浮 tooltip 承载（§6 交代：依赖读 IP 的用户看 tooltip）
            QVERIFY2(chip->toolTip().contains(QStringLiteral("10.8.6")), qPrintable(chip->toolTip()));
        }
        const QStringList names{QStringLiteral("独名一号  ×"), QStringLiteral("独名二号  ×")};
        QVERIFY2(names.contains(chips.at(0)->text()), qPrintable(chips.at(0)->text()));
        QVERIFY2(names.contains(chips.at(1)->text()), qPrintable(chips.at(1)->text()));
    }

    // 空名回落 ip:port——现状行为即如此（§2.3"否则栏里出空白胶囊，比长更糟"），
    // 本用例是改造后不得退化的基线守卫（改装配前应同样绿）。
    void chipCaptionEmptyNameFallsBackToAddress()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-anon"), QString(),
                                           QStringLiteral("10.8.7.3"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectDeviceViaMenu(bus, QStringLiteral("id:dev-anon"));
        const auto chips = visibleChips(bus);
        QCOMPARE(chips.size(), 1);
        QCOMPARE(chips.first()->text(), QStringLiteral("10.8.7.3:21  ×"));
    }

    // 重名才加 ·ip 短尾标：判据与 DeviceRegistry::rebuildWarnings 同源（非空名字
    // 出现 >1 次）；chips 与菜单项文本同一真源（规格 §3.6）。
    void duplicateNamesGetShortIpSuffixEverywhere()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-dup1"), QStringLiteral("甲"),
                                           QStringLiteral("10.0.0.7"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-dup2"), QStringLiteral("甲"),
                                           QStringLiteral("10.0.0.9"), 21)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-solo"), QStringLiteral("乙"),
                                           QStringLiteral("10.0.0.8"), 21)));
        DeviceBusWidget bus;
        bus.setRegistry(&registry);
        bus.resize(1400, 100);
        selectAllViaMenu(bus);

        QStringList chipTexts;
        for (QPushButton* chip : visibleChips(bus))
            chipTexts << chip->text();
        QVERIFY2(chipTexts.contains(QStringLiteral("甲 ·10.0.0.7  ×")), qPrintable(chipTexts.join(QStringLiteral(" | "))));
        QVERIFY2(chipTexts.contains(QStringLiteral("甲 ·10.0.0.9  ×")), qPrintable(chipTexts.join(QStringLiteral(" | "))));
        QVERIFY2(chipTexts.contains(QStringLiteral("乙  ×")), qPrintable(chipTexts.join(QStringLiteral(" | "))));

        // 菜单项文本同规则（重名带尾标，非重名干净名称）
        auto* btn = bus.findChild<QPushButton*>(QStringLiteral("devicePickerBtn"));
        QVERIFY(btn);
        QMenu* menu = btn->menu();
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QStringList menuTexts;
        for (QAction* act : menuMemberActions(menu))
            menuTexts << act->text();
        QVERIFY2(menuTexts.contains(QStringLiteral("甲 ·10.0.0.7")), qPrintable(menuTexts.join(QStringLiteral(" | "))));
        QVERIFY2(menuTexts.contains(QStringLiteral("甲 ·10.0.0.9")), qPrintable(menuTexts.join(QStringLiteral(" | "))));
        QVERIFY2(menuTexts.contains(QStringLiteral("乙")), qPrintable(menuTexts.join(QStringLiteral(" | "))));
    }

    // ── ④ 任务中心：模板列表 + 步骤/参数摘要 + 目标设备名称+地址 ──
    void taskCenter_templateSummary()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-1"), QStringLiteral("焊接机"),
            QStringLiteral("10.1.0.1"), 21)));

        TaskTemplateStore templates;
        TaskTemplate template1;
        template1.name = "固件升级";
        template1.description = "部署并重启";
        template1.deviceIds = {"dev-1"};
        QVariantMap deployParams;
        deployParams.insert(QStringLiteral("files"),
                            QStringList{QStringLiteral("C:/fw/a.bin"),
                                        QStringLiteral("C:/fw/b.bin")});
        deployParams.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/tmp"));
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("ps"), QStringLiteral("df"),
                                         QStringLiteral("free")});
        template1.steps = {makeStep(TaskStepType::DeployFiles, deployParams),
                           makeStep(TaskStepType::RunCommands, commandParams),
                           makeStep(TaskStepType::Reboot)};
        QVERIFY(templates.save(template1));

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);

        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
        list->setCurrentRow(0);

        auto* summary = center.findChild<QLabel*>(QStringLiteral("templateSummaryLabel"));
        QVERIFY(summary);
        const QString text = summary->text();
        QVERIFY2(text.contains(QStringLiteral("固件升级")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("目标 1 台")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("文件 2 个")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("命令 3 条")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("重启")), qPrintable(text));

        // 中栏步骤表：三步逐行展示
        auto* steps = center.findChild<QTableWidget*>(QStringLiteral("templateSteps"));
        QVERIFY(steps);
        QCOMPARE(steps->rowCount(), 3);
        QVERIFY(steps->item(0, 1)->text().contains(QStringLiteral("文件部署")));
        QVERIFY(steps->item(2, 1)->text().contains(QStringLiteral("重启")));

        // 右栏目标设备：名称 + 地址
        auto* devices = center.findChild<QListWidget*>(QStringLiteral("taskTargetDevices"));
        QVERIFY(devices);
        QCOMPARE(devices->count(), 1);
        QVERIFY(devices->item(0)->text().contains(QStringLiteral("焊接机")));
        QVERIFY(devices->item(0)->text().contains(QStringLiteral("10.1.0.1")));
    }

    // ── ④b v2.16-B §4：模板 tagSelectors 带首尾空格时，圈选结果不得比规范化前小 ──
    // 本规格唯一的行为变更站点（TaskCenterWidget::resolveTargets）：selector 与档案
    // 标签两侧都先 devicebus::normalizeTag 再比较。改动前 selector " 车间A " 走
    // std::find 精确相等匹配不上 ⇒ 目标 0 台（此用例当场红出）。
    void resolveTargetsSelectorWithSurroundingSpacesStillMatches()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile tagged = makeProfile(
            QStringLiteral("dev-tag-1"), QStringLiteral("车间A机器"),
            QStringLiteral("10.7.0.1"), 21);
        tagged.tags = {"车间A"};                     // registry 存库前已 trim，此处本就干净
        QVERIFY(registry.save(tagged));
        DeviceProfile untagged = makeProfile(
            QStringLiteral("dev-tag-2"), QStringLiteral("无标机器"),
            QStringLiteral("10.7.0.2"), 21);
        QVERIFY(registry.save(untagged));            // 对照组：圈选不得误伤无标签设备

        TaskTemplateStore templates;
        TaskTemplate tmpl;
        tmpl.name = QStringLiteral("带空格圈选").toUtf8().toStdString();
        tmpl.tagSelectors = {" 车间A "};             // 模板 JSON 侧从未 trim——真变更臂
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        tmpl.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        QVERIFY(templates.save(tmpl));

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);

        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
        list->setCurrentRow(0);

        auto* summary = center.findChild<QLabel*>(QStringLiteral("templateSummaryLabel"));
        QVERIFY(summary);
        QVERIFY2(summary->text().contains(QStringLiteral("目标 1 台")),
                 qPrintable(summary->text()));
        auto* devices = center.findChild<QListWidget*>(QStringLiteral("taskTargetDevices"));
        QVERIFY(devices);
        QCOMPARE(devices->count(), 1);               // 只命中 tagged，非恒真也非恒假
        QVERIFY(devices->item(0)->text().contains(QStringLiteral("车间A机器")));
    }

    // ── ④c v2.16-B §4 等值守卫一：内部空格仍是区分手段（规范化只去首尾空白）──
    // 防"顺手改成折叠空格/模糊匹配"：selector "车间 A" 对标签 "车间A" 必须仍不匹配。
    // 该用例在改动前后都应绿——改动前精确相等不匹配，改动后 normalizeTag 保留内部空格。
    void resolveTargetsSelectorInnerSpaceStillMisses()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile tagged = makeProfile(
            QStringLiteral("dev-g1"), QStringLiteral("机器一"),
            QStringLiteral("10.7.1.1"), 21);
        tagged.tags = {"车间A"};
        QVERIFY(registry.save(tagged));

        TaskTemplateStore templates;
        TaskTemplate tmpl;
        tmpl.name = QStringLiteral("内部空格守卫").toUtf8().toStdString();
        tmpl.tagSelectors = {"车间 A"};
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        tmpl.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        QVERIFY(templates.save(tmpl));

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
        list->setCurrentRow(0);

        auto* devices = center.findChild<QListWidget*>(QStringLiteral("taskTargetDevices"));
        QVERIFY(devices);
        QCOMPARE(devices->count(), 0);               // 内部空格不同 ⇒ 不匹配
        auto* runBtn = center.findChild<QPushButton*>(QStringLiteral("btnTaskRun"));
        QVERIFY(runBtn);
        QCOMPARE(runBtn->isEnabled(), false);        // 空目标 ⇒ 执行按钮保持禁用
    }

    // ── ④d v2.16-B §4 等值守卫二：大小写仍是区分手段（规范化不碰大小写）──
    // 防"顺手改成大小写不敏感"：selector "车间a" 对标签 "车间A" 必须仍不匹配。
    void resolveTargetsSelectorCaseDifferenceStillMisses()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        DeviceProfile tagged = makeProfile(
            QStringLiteral("dev-g2"), QStringLiteral("机器二"),
            QStringLiteral("10.7.2.1"), 21);
        tagged.tags = {"车间A"};
        QVERIFY(registry.save(tagged));

        TaskTemplateStore templates;
        TaskTemplate tmpl;
        tmpl.name = QStringLiteral("大小写守卫").toUtf8().toStdString();
        tmpl.tagSelectors = {"车间a"};
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        tmpl.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        QVERIFY(templates.save(tmpl));

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
        list->setCurrentRow(0);

        auto* devices = center.findChild<QListWidget*>(QStringLiteral("taskTargetDevices"));
        QVERIFY(devices);
        QCOMPARE(devices->count(), 0);               // 大小写不同 ⇒ 不匹配
    }

    // ── ⑤ 执行确认：摘要+风险提示；拒绝则不启动、不落记录 ─────────
    void taskCenter_riskConfirmation()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-1"), QStringLiteral("焊接机"),
            QStringLiteral("10.1.0.1"), 21)));

        TaskTemplateStore templates;
        TaskTemplate template1;
        template1.templateId = "tpl-confirm";
        template1.name = "维护重启";
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        template1.steps = {makeStep(TaskStepType::RunCommands, commandParams),
                           makeStep(TaskStepType::Reboot)};
        template1.deviceIds = {"dev-1"};
        QVERIFY(templates.save(template1));
        const std::string templateId = template1.templateId;

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);

        QString capturedSummary;
        bool capturedRisky = false;
        int confirmCalls = 0;
        center.setRunConfirmer([&](const QString& summary, bool risky) {
            ++confirmCalls;
            capturedSummary = summary;
            capturedRisky = risky;
            return false;   // 操作员拒绝
        });

        QSignalSpy started(&center, &TaskCenterWidget::runStarted);
        center.startTemplate(templateId);

        QCOMPARE(confirmCalls, 1);
        // 摘要含设备名称+地址、命令数量与重启动作；重启动作触发风险提示
        QVERIFY2(capturedSummary.contains(QStringLiteral("焊接机")), qPrintable(capturedSummary));
        QVERIFY2(capturedSummary.contains(QStringLiteral("10.1.0.1")), qPrintable(capturedSummary));
        QVERIFY2(capturedSummary.contains(QStringLiteral("命令 1 条")), qPrintable(capturedSummary));
        QVERIFY(capturedRisky);
        QVERIFY2(capturedSummary.contains(QStringLiteral("风险提示")), qPrintable(capturedSummary));

        // 拒绝：不启动执行、不产生任何历史记录
        QCOMPARE(started.count(), 0);
        QCOMPARE(runs.query({}).size(), std::size_t{0});
    }

    // ── ⑥ 校验失败运行 → 历史「失败」行 → 从失败阶段重试动作 ──────
    void taskCenter_failedRunHistoryAndRetryAction()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        // 只有 FTP 端点 → RunCommands 步骤校验必失败（设备未配置 Telnet/SSH 端点），
        // 全程不触网、不需要任何真实适配器
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-ftp"), QStringLiteral("只读FTP盒"),
            QStringLiteral("10.2.0.2"), 21)));

        TaskTemplateStore templates;
        TaskTemplate template1;
        template1.templateId = "tpl-retry";
        template1.name = "远程巡检";
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        template1.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        template1.deviceIds = {"dev-ftp"};
        QVERIFY(templates.save(template1));
        const std::string templateId = template1.templateId;

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);
        center.setRunConfirmer([](const QString&, bool) { return true; });

        QSignalSpy started(&center, &TaskCenterWidget::runStarted);
        QVERIFY(started.isValid());
        center.startTemplate(templateId);
        QCOMPARE(started.count(), 1);

        // 引擎在协调线程收口 → 队列回调写历史表
        auto* history = center.findChild<QTableWidget*>(QStringLiteral("taskRunHistory"));
        QVERIFY(history);
        QTRY_COMPARE_WITH_TIMEOUT(history->rowCount(), 1, 10000);
        const int statusCol = 2;
        QCOMPARE(history->item(0, statusCol)->text(), QStringLiteral("失败"));

        // 选中失败行 → 「从失败阶段重试」点亮
        auto* retry = center.findChild<QPushButton*>(QStringLiteral("taskRetryFailedButton"));
        QVERIFY(retry);
        QCOMPARE(retry->isEnabled(), false);
        history->selectRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(retry->isEnabled(), 5000);

        // 点击重试 → engine.retryFailed 复用回调，产生第二轮记录
        retry->click();
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 5000);

        // 第二轮同样收口为失败（校验必失败）→ 历史中出现两条终态失败记录
        const auto failedCount = [&runs] {
            TaskRunFilter filter;
            filter.status = TaskRunStatus::Failed;
            return runs.query(filter).size();
        };
        QTRY_VERIFY_WITH_TIMEOUT(failedCount() == std::size_t{2}, 10000);
    }

    // ── ⑥b 终审 Important 5：同档案多个不同 credentialRef → 中文告警一次 ──
    void taskCenter_warnsOnMultipleDistinctCredentialRefs()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        // dev-multi：ftp 与 sftp 端点携带不同凭据引用 → 必须告警（仅 1 次）；
        // dev-single：两端点同引用 → 不得告警。两台都缺 Telnet/SSH 命令端点，
        // 运行只会走 Validating 校验失败路径（同用例⑥，不触碰适配器工厂）。
        DeviceProfile multi;
        multi.deviceId = "dev-multi";
        multi.name = QStringLiteral("多凭据设备").toStdString();
        multi.endpoints.push_back({"ftp", "10.5.0.1", 21, "ref-never-echo-A"});
        multi.endpoints.push_back({"sftp", "10.5.0.1", 22, "ref-never-echo-B"});
        QVERIFY(registry.save(multi));
        DeviceProfile single;
        single.deviceId = "dev-single";
        single.name = QStringLiteral("单凭据设备").toStdString();
        single.endpoints.push_back({"ftp", "10.5.0.2", 21, "ref-never-echo-A"});
        single.endpoints.push_back({"sftp", "10.5.0.2", 22, "ref-never-echo-A"});
        QVERIFY(registry.save(single));

        TaskTemplateStore templates;
        TaskTemplate task;
        task.templateId = "tpl-creds";
        task.name = QStringLiteral("凭据巡检").toStdString();
        QVariantMap commandParams;
        commandParams.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("uptime")});
        task.steps = {makeStep(TaskStepType::RunCommands, commandParams)};
        task.deviceIds = {"dev-multi", "dev-single"};
        QVERIFY(templates.save(task));
        const std::string templateId = task.templateId;

        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);
        center.setRunConfirmer([](const QString&, bool) { return true; });

        QSignalSpy logs(&center, &TaskCenterWidget::logMessage);
        QVERIFY(logs.isValid());
        QSignalSpy finished(&center, &TaskCenterWidget::runFinished);
        QVERIFY(finished.isValid());

        center.startTemplate(templateId);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);

        int warnCount = 0;
        for (const QVariantList& args : logs) {
            const QString text = args.at(0).toString();
            if (text.contains(QStringLiteral("不同凭据引用"))) {
                ++warnCount;
                QVERIFY2(text.contains(QStringLiteral("多凭据设备")), qPrintable(text));
                QVERIFY2(text.contains(QStringLiteral("2")), qPrintable(text));
                // 告警绝不泄露引用内容（这里引用键本身也不得出现）
                QVERIFY2(!text.contains(QStringLiteral("ref-never-echo")), qPrintable(text));
            }
        }
        QCOMPARE(warnCount, 1);
    }

    // ── ⑧ v2.13 Task 3：模板导出——公共 API 直调 + 按钮选中启用 + 清洗口径 ──
    void taskCenter_exportTemplate_writesContractFileAndDisabledWithoutSelection()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl; tmpl.name = "导出我"; tmpl.deviceIds = {"device-xxxx"};
        TaskStep step; step.type = TaskStepType::Reboot; tmpl.steps.push_back(step);
        QVERIFY(store.save(tmpl));
        TaskCenterWidget center;
        auto* exportBtn = center.findChild<QPushButton*>(QStringLiteral("taskExportTemplateButton"));
        QVERIFY(exportBtn);
        QVERIFY(!exportBtn->isEnabled());                    // 未选中禁用（尚无 store，亦无选中）
        center.setStores(&store, nullptr);                   // 既有口径：刷新自动选中首行
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        list->setCurrentRow(0);
        QVERIFY(exportBtn->isEnabled());                    // 选中即用
        QCOMPARE(TaskCenterWidget::sanitizedTemplateFileName(QStringLiteral("A/B:*<>|\\?x")),
                 QStringLiteral("A_B_______x.json"));        // 清洗口径钉死（8 个非法字符逐一替换）
        QCOMPARE(TaskCenterWidget::sanitizedTemplateFileName(QStringLiteral("报表.json")),
                 QStringLiteral("报表.json"));                  // #55：endsWith 防重，不再 .json.json
        QCOMPARE(TaskCenterWidget::sanitizedTemplateFileName(QStringLiteral("x.JSON")),
                 QStringLiteral("x.JSON"));                  // 大小写同判
        const QString path = m_tempDir.filePath(QStringLiteral("t.json"));
        QVERIFY(center.exportTemplateToFile(path));
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray content = f.readAll();
        QVERIFY(content.contains("deviceforge.task-template"));
        QVERIFY(content.contains(QString::fromUtf8("导出我").toUtf8()));
        QVERIFY(!content.contains("template-"));             // ID 不外溢（store 生成的前缀不在文件中）

        // 失败路径：指向已存在目录（打开写必失败，跨机器确定，#55 替代 Z: 盘假设）；
        // 错误弹窗布防后自动关闭（沿用本文件布防做法）
        QTimer::singleShot(50, &center, [&center] {
            if (auto* box = center.findChild<QMessageBox*>())
                box->done(QMessageBox::Ok);
        });
        QVERIFY(!center.exportTemplateToFile(m_tempDir.path()));
    }

    // ── ⑧b v2.13 Task 3：模板清空后刷新 → 陈旧选中态复位、导出按钮回禁用 ──
    // 负对照：refreshTemplates 空列表分支若不复位，m_currentTemplateId 残留旧值，
    // 按钮将保持启用（用例变红）。store 无状态（共用 ConfigStore 单例），清空走 freshDb。
    void taskCenter_exportTemplateButtonResetsWhenSelectionCleared()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl; tmpl.name = "会被清库";
        TaskStep step; step.type = TaskStepType::Reboot; tmpl.steps.push_back(step);
        QVERIFY(store.save(tmpl));
        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        auto* exportBtn = center.findChild<QPushButton*>(QStringLiteral("taskExportTemplateButton"));
        QVERIFY(exportBtn);
        QVERIFY(exportBtn->isEnabled());                     // 自动选中首行 → 启用
        freshDb();                                           // 清库 → 模板列表转空
        center.setStores(&store, nullptr);                   // 重绑刷新 → 走复位路
        QVERIFY(!exportBtn->isEnabled());                    // 陈旧选中态必须清除
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QCOMPARE(list->count(), 0);
    }

    // ── ⑨ v2.13 Task 4：模板导入——新建/覆盖/改名/取消/坏文件五路 ──────────
    void taskCenter_import_newTemplate_countsUpAndKeepsDangling()
    {
        TaskTemplateStore store;
        DeviceRegistry registry;                             // 空档案：device-dead 必计悬空
        QVERIFY(registry.load());
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&store, nullptr);

        auto* importBtn = center.findChild<QPushButton*>(QStringLiteral("taskImportTemplateButton"));
        QVERIFY(importBtn);
        QCOMPARE(importBtn->isEnabled(), true);              // 恒可用（规格 §4）

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        const int before = static_cast<int>(store.list().size());
        QSignalSpy logs(&center, &TaskCenterWidget::logMessage);
        QVERIFY(logs.isValid());
        armDialogCapture(center);

        QVERIFY(center.importTemplateFromFile(path));
        QVERIFY2(m_dialogClosed, "成功信息框未弹出（布防未命中）");
        QCOMPARE(static_cast<int>(store.list().size()), before + 1);

        // 悬空引用原样保留（不剥离）
        const auto rows = store.list();
        QCOMPARE(QString::fromStdString(rows.front().name), QStringLiteral("导入体"));
        QCOMPARE(rows.front().deviceIds.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(rows.front().deviceIds.front()),
                 QStringLiteral("device-dead"));

        // 成功提示两行文案：步骤计数 + 悬空统计；logMessage 同文案
        const QString expected = QStringLiteral(
            "已导入模板「导入体」（1 个步骤）\n"
            "引用了 1 个本机不存在的设备档案，建好同端点档案后自动接上");
        QCOMPARE(m_dialogText, expected);
        bool logHit = false;
        for (const QVariantList& args : logs) {
            if (args.at(0).toString() == expected)
                logHit = true;
        }
        QVERIFY2(logHit, qPrintable(expected));

        // 返回 true 即已 refreshTemplates()：列表出现新行
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 5000);
    }

    void taskCenter_import_conflictOverwrite_replacesInPlace()
    {
        TaskTemplateStore store;
        TaskTemplate existing;
        existing.name = QStringLiteral("导入体").toUtf8().toStdString();
        existing.description = QStringLiteral("本机旧版").toUtf8().toStdString();
        existing.version = 3;                                // 高于文件版本(1)：覆盖须继承既有版本，
        TaskStep oldStep; oldStep.type = TaskStepType::DeployFiles;   // 否则被 store 降级拒绝
        existing.steps.push_back(oldStep);
        QVERIFY(store.save(existing));
        const std::string existingId = store.list().front().templateId;

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        QString resolverName;
        center.setImportConflictResolver([&](const QString& name) {
            resolverName = name;
            return 0;                                        // 覆盖
        });

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        armDialogCapture(center);
        QVERIFY(center.importTemplateFromFile(path));

        QCOMPARE(resolverName, QStringLiteral("导入体"));     // resolver 收到导入名
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});               // 数量不变：原位覆写
        QCOMPARE(rows.front().templateId, existingId);       // 继承既有 templateId
        QCOMPARE(rows.front().version, 3);                   // 继承既有 version
        QCOMPARE(QString::fromStdString(rows.front().description),
                 QStringLiteral("导入用例种子"));              // 内容被替换
        QVERIFY2(m_dialogClosed, "成功信息框未弹出");
    }

    void taskCenter_import_conflictRename_addsSuffix2()
    {
        TaskTemplateStore store;
        TaskTemplate existing;
        existing.name = QStringLiteral("导入体").toUtf8().toStdString();
        existing.description = QStringLiteral("本机旧版").toUtf8().toStdString();
        TaskStep step; step.type = TaskStepType::DeployFiles;
        existing.steps.push_back(step);
        QVERIFY(store.save(existing));

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        center.setImportConflictResolver([](const QString&) { return 1; });   // 改名

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        armDialogCapture(center);
        QVERIFY(center.importTemplateFromFile(path));

        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{2});
        const auto byName = [&rows](const QString& name) -> std::optional<TaskTemplate> {
            for (const auto& t : rows) {
                if (QString::fromStdString(t.name) == name)
                    return t;
            }
            return std::nullopt;
        };
        const auto renamed = byName(QStringLiteral("导入体 (2)"));
        QVERIFY2(renamed.has_value(), "未找到自动改名行「导入体 (2)」");
        QCOMPARE(QString::fromStdString(renamed->description), QStringLiteral("导入用例种子"));
        const auto kept = byName(QStringLiteral("导入体"));
        QVERIFY(kept.has_value());
        QCOMPARE(QString::fromStdString(kept->description), QStringLiteral("本机旧版"));   // 既有行未动
        QVERIFY2(m_dialogText.contains(QStringLiteral("导入体 (2)")),
                 qPrintable(m_dialogText));                  // 提示用改名后的名称
    }

    void taskCenter_import_conflictCancel_noop()
    {
        TaskTemplateStore store;
        TaskTemplate existing;
        existing.name = QStringLiteral("导入体").toUtf8().toStdString();
        existing.description = QStringLiteral("本机旧版").toUtf8().toStdString();
        TaskStep step; step.type = TaskStepType::DeployFiles;
        existing.steps.push_back(step);
        QVERIFY(store.save(existing));

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        int resolverCalls = 0;
        center.setImportConflictResolver([&](const QString&) {
            ++resolverCalls;
            return -1;                                       // 取消
        });

        const QString path = writeToTemp(encodeTemplateFile(makeExportableTemplate()));
        QVERIFY(!path.isEmpty());
        // 取消=静默 false 不弹窗：故意不布防——若误弹框，exec 阻塞至测试超时即缺陷证据
        QVERIFY(!center.importTemplateFromFile(path));
        QCOMPARE(resolverCalls, 1);
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});               // 数量不变
        QCOMPARE(QString::fromStdString(rows.front().description),
                 QStringLiteral("本机旧版"));                  // 内容不变
    }

    void taskCenter_import_badFile_rejectedWithoutTouchingStore()
    {
        TaskTemplateStore store;
        TaskTemplate keep;
        keep.name = QStringLiteral("既有模板").toUtf8().toStdString();
        TaskStep step; step.type = TaskStepType::Reboot;
        keep.steps.push_back(step);
        QVERIFY(store.save(keep));

        // 手拼 formatVersion=2 契约文件 → 版本闸拒绝（升级文案），不落库
        const QByteArray bad = QByteArrayLiteral(
            "{\"format\":\"deviceforge.task-template\",\"formatVersion\":2,"
            "\"template\":{\"name\":\"坏文件\",\"steps\":"
            "[{\"type\":\"reboot\",\"parameters\":{}}]}}");
        const QString path = writeToTemp(bad);
        QVERIFY(!path.isEmpty());

        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        int resolverCalls = 0;
        center.setImportConflictResolver([&](const QString&) {
            ++resolverCalls;
            return 0;
        });
        armDialogCapture(center);

        QVERIFY(!center.importTemplateFromFile(path));
        QCOMPARE(resolverCalls, 0);                          // 未触及冲突分支
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});               // store 未被触碰
        QCOMPARE(QString::fromStdString(rows.front().name), QStringLiteral("既有模板"));
        QVERIFY2(m_dialogClosed, "错误框未弹出（布防未命中）");
        QVERIFY2(m_dialogText.contains(QStringLiteral("升级 DeviceForge")),
                 qPrintable(m_dialogText));
    }

    // ── ⑦ MultiProgressWidget：名称 + 地址；无名称保持原样 ────────
    void multiProgress_showsNamePlusAddress()
    {
        MultiProgressWidget progress;
        progress.setDeviceCount(2);
        progress.setDeviceInfo(QStringLiteral("10.0.0.5:21"), QStringLiteral("熔接机"));
        progress.setDeviceInfo(QStringLiteral("10.0.0.6:21"));   // 旧调用：仅地址

        const auto labels = progress.findChildren<QLabel*>();
        bool namedFound = false;
        bool addressOnlyFound = false;
        for (const QLabel* label : labels) {
            if (label->text().contains(QStringLiteral("熔接机"))
                && label->text().contains(QStringLiteral("10.0.0.5:21")))
                namedFound = true;
            if (label->text() == QStringLiteral("10.0.0.6:21"))
                addressOnlyFound = true;
        }
        QVERIFY(namedFound);
        QVERIFY(addressOnlyFound);
    }

    // ── ⑩ v2.13 #54 Task 2：编辑器对话框骨架 ──────────────────────
    void editorDialog_metaAndDanglingRoundTrip()
    {
        TaskTemplate in;
        in.name = "编辑器种子"; in.description = "d"; in.tagSelectors = {"A线"};
        in.deviceIds = {"device-notexist"};                       // 悬空：knownDevices 不含
        TaskStep step; step.type = TaskStepType::Reboot;
        step.parameters.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
        in.steps.push_back(step);
        in.unknownFields.insert(QStringLiteral("x_next"), 7);
        TaskTemplateEditorDialog dlg(in, {});                     // 本机无档案
        auto* devList = dlg.findChild<QListWidget*>(QStringLiteral("teDevices"));
        QVERIFY(devList);
        QVERIFY(devList->item(0)->text().contains(QStringLiteral("本机无档案")));
        QVERIFY(devList->item(0)->checkState() == Qt::Checked);   // 默认保留
        TaskTemplate out;
        QVERIFY(dlg.collect(&out));
        QCOMPARE(out.deviceIds, in.deviceIds);
        QCOMPARE(out.unknownFields, in.unknownFields);
        QCOMPARE(out.steps.front().parameters.value(QStringLiteral("protocol")).toString(),
                 QStringLiteral("telnet"));
        QCOMPARE(out.templateId, in.templateId);
    }
    void editorDialog_validateRejectsEmptyNameAndCommands()
    {
        TaskTemplate in; // 空模板
        TaskTemplateEditorDialog dlg(in, {});
        QVERIFY(!dlg.validate().isEmpty());                        // 缺名称
        TaskTemplate seeded; seeded.name = "n";
        TaskStep cmd; cmd.type = TaskStepType::RunCommands;
        cmd.parameters.insert(QStringLiteral("commands"), QStringList{QStringLiteral("  ")});
        seeded.steps.push_back(cmd);
        TaskTemplateEditorDialog dlg2(seeded, {});
        QVERIFY(dlg2.validate().contains(QStringLiteral("缺少命令列表")));   // v2.14 #49-3 共享口径
    }
    // v2.14 冒烟修复（Critical，自 v2.13 带病发布）：QDialog **不会**自动接线
    // QDialogButtonBox——必须手动 connect(accepted→accept()/rejected→reject())，
    // 否则「保存/取消」点击全无响应，只能点窗口 ✕。原注释误称自动接线。
    void editorDialog_buttonBoxWiringClosesDialog()
    {
        TaskTemplate in;
        in.name = "接线验证";
        QVariantMap deploy;
        deploy.insert(QStringLiteral("items"), QVariantList{
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/a.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/a.bin")}}});
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        step.parameters = deploy;
        in.steps.push_back(step);
        {
            TaskTemplateEditorDialog dlg(in, {});
            dlg.show();
            auto* save = dlg.findChild<QPushButton*>(QStringLiteral("teSave"));
            QVERIFY(save);
            save->click();                                   // 合法输入：应关闭并 Accepted
            QVERIFY2(!dlg.isVisible(), "点保存（合法输入）应关闭对话框");
            QCOMPARE(dlg.result(), int(QDialog::Accepted));
        }
        {
            TaskTemplateEditorDialog dlg(in, {});
            dlg.show();
            auto* cancel = dlg.findChild<QPushButton*>(QStringLiteral("teCancel"));
            QVERIFY(cancel);
            cancel->click();
            QVERIFY(!dlg.isVisible());
            QCOMPARE(dlg.result(), int(QDialog::Rejected));
        }
    }
    // v2.14 冒烟修复：deploy 条目表只有「添加」没有「删除」入口——添加错了只能
    // 整步重配。「移除选中行」按钮：无选中禁用，点击删表行并即时写回模型
    //（removeRow 不发 cellChanged，必须手动 flush）。
    void editorDialog_removeItemRowDeletesEntry()
    {
        TaskTemplate in;
        in.name = "条目移除";
        QVariantMap deploy;
        deploy.insert(QStringLiteral("items"), QVariantList{
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/keep.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/keep.bin")}},
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/drop.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/drop.bin")}}});
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        step.parameters = deploy;
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        auto* remove = dlg.findChild<QPushButton*>(QStringLiteral("teRemoveItem"));
        QVERIFY(remove);
        QVERIFY2(!remove->isEnabled(), "无选中行时移除按钮应禁用");
        table->selectRow(1);
        QVERIFY(remove->isEnabled());
        remove->click();
        QCOMPARE(table->rowCount(), 1);
        TaskTemplate out;
        QVERIFY(dlg.collect(&out));
        const QVariantList items = out.steps[0].parameters.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].toMap().value(QStringLiteral("localPath")).toString(),
                 QStringLiteral("D:/keep.bin"));
    }
    // v2.14 冒烟修复：部署页「保存为模板」等外部落库源不经过任务中心任何操作，
    // 列表只在构造/setStores/本页操作时刷新——用户保存后切到任务中心看不到新
    // 模板。页显（showEvent）刷新收敛一切外部变更源；选中与搜索过滤由
    // refreshTemplates 既有语义保持。
    void taskCenter_showEventRefreshesExternallySavedTemplates()
    {
        freshDb();
        DeviceRegistry registry;
        QVERIFY(registry.load());
        TaskTemplateStore templates;
        TaskRunStore runs;
        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&templates, &runs);              // 此刻空库
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        QVERIFY(list);
        QCOMPARE(list->count(), 0);

        TaskTemplate external;
        external.name = "外部落库";
        external.version = 1;
        external.steps = {makeStep(TaskStepType::Reboot, {})};
        QVERIFY(templates.save(external));                // 模拟部署页保存通道

        center.show();
        QVERIFY2(list->count() == 1,
                 "页显刷新应让外部落库模板立即可见（showEvent→refreshTemplates）");
        QCOMPARE(list->item(0)->text().contains(QStringLiteral("外部落库")), true);
        center.hide();
    }
    void editorDialog_canonicalizeLegacyFilesShorthand()
    {
        QVariantMap legacy;
        legacy.insert(QStringLiteral("files"), QStringList{"/local/a.bin", "/local/b.bin"});
        legacy.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/opt/app/"));
        const auto canon = TaskTemplateEditorDialog::canonicalDeployParameters(legacy);
        QVERIFY(!canon.contains(QStringLiteral("files")));
        QVERIFY(!canon.contains(QStringLiteral("remoteDirectory")));
        const QVariantList items = canon.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].toMap().value(QStringLiteral("remotePath")).toString(),
                 QStringLiteral("/opt/app/a.bin"));               // 去尾斜杠拼接
    }

    // ── ⑪ v2.13 #54 Task 3：deploy 步骤面板（items 表 + 批量添加口径）──
    void editorDialog_deployPanelRoundTripsItems()
    {
        TaskTemplate in; in.name = "n";
        TaskStep step; step.type = TaskStepType::DeployFiles;
        QVariantList items;
        QVariantMap one;
        one.insert(QStringLiteral("localPath"), QStringLiteral("C:/tmp/a.bin"));
        one.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/a.bin"));
        one.insert(QStringLiteral("direction"), QStringLiteral("upload"));
        one.insert(QStringLiteral("overwrite"), QStringLiteral("skip"));
        items.push_back(one);
        step.parameters.insert(QStringLiteral("items"), items);
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);                                            // 此前不存在 → 红
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("/opt/a.bin"));
        table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("C:/tmp/b.bin")));
        // #55：direction 单元格下拉往返断言（程序 setCurrentIndex 不发 activated，
        // 由 assemble() 实时快照读 currentData——与用户 activated 即时写回两路同口径）
        auto* dirCombo = qobject_cast<QComboBox*>(table->cellWidget(0, 2));
        QVERIFY(dirCombo);
        dirCombo->setCurrentIndex(dirCombo->findData(QStringLiteral("download")));
        TaskTemplate out; QVERIFY(dlg.collect(&out));
        const auto row = out.steps.front().parameters.value(QStringLiteral("items"))
                             .toList().front().toMap();
        QCOMPARE(row.value(QStringLiteral("localPath")).toString(), QStringLiteral("C:/tmp/b.bin"));
        QCOMPARE(row.value(QStringLiteral("direction")).toString(), QStringLiteral("download"));
        QCOMPARE(row.value(QStringLiteral("overwrite")).toString(), QStringLiteral("skip"));
    }

    // ── v2.15 任务9：teAddDir 目录展开直调缝（QFileDialog 选人腿仍人工冒烟，#54 先例）──
    // 入口展开保证「条目表所见即部署所得」：目录不再以目录条目形态入表，而是
    // 展开为文件条目批量入表，remotePath=composeRemotePath(teRemoteDir, rel) 镜像预填。
    void editorDialog_addDeployDirectory_expandsIntoMirroredFileRows()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("pkg/bin")));
        {
            QFile f(tmp.filePath(QStringLiteral("pkg/bin/app.conf")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("a");
        }
        {
            QFile f(tmp.filePath(QStringLiteral("pkg/notes.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("b");
        }

        TaskTemplate in;
        in.name = QStringLiteral("目录展开").toStdString();   // TaskTemplate::name 是 std::string
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* remoteDir = dlg.findChild<QLineEdit*>(QStringLiteral("teRemoteDir"));
        QVERIFY(remoteDir);
        remoteDir->setText(QStringLiteral("/opt/app"));

        QString error;
        const int added = dlg.addDeployDirectoryEntries(tmp.filePath(QStringLiteral("pkg")), &error);
        QCOMPARE(added, 2);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);          // 目录以文件条目形态入表（非目录行）
        QStringList remotes;
        for (int row = 0; row < table->rowCount(); ++row) {
            QVERIFY2(!table->item(row, 0)->text().endsWith(QStringLiteral("/pkg")),
                     "本地列必须是展开后的普通文件而非目录");
            remotes << table->item(row, 1)->text();
        }
        QVERIFY(remotes.contains(QStringLiteral("/opt/app/pkg/bin/app.conf")));   // 镜像相对段
        QVERIFY(remotes.contains(QStringLiteral("/opt/app/pkg/notes.txt")));

        TaskTemplate out;
        QVERIFY(dlg.collect(&out));
        const QVariantList items = out.steps[0].parameters.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 2);
        QVERIFY(!out.steps[0].parameters.contains(QStringLiteral("files")));
    }
    // 根目录退化（FolderExpand.h 契约 ⑧）：纯头中文拒绝串逐字直传（三入口同文案，
    // 本用例独立于 tst_folder_expand 钉桩，防入口侧改写文案）。
    void editorDialog_addDeployDirectory_rootRejectionPassesPureHeaderVerbatim()
    {
        TaskTemplate in;
        in.name = QStringLiteral("根拒绝").toStdString();
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        QString error;
        QCOMPARE(dlg.addDeployDirectoryEntries(QDir::rootPath(), &error), -1);
        QCOMPARE(error,
                 QStringLiteral("部署目录不能为文件系统根目录（「%1」无法解析出目录名），"
                                "请在根目录下选择具体子目录")
                     .arg(QDir::rootPath()));
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 0);          // 拒绝时表格零改动，停留编辑不丢内容
    }

    // v2.15 波2 终审 I-1（规格 §3.1 条款 4 远端目标冲突检测）：teAddDir 两次添加
    // **同名不同父**目录（a/bin 与 b/bin）→ 二者展开出同一远端相对首段 "bin/…"，
    // 第二次添加的展开行远端路径与条目表既有行冲突 → 整体拒绝（中文 error、表格零
    // 改动、收集 items 无重复 remotePath、部署不再后写覆盖静默累积）；非冲突目录
    // （c/pkg）仍成功入表（无误拒）。
    void editorDialog_addDeployDirectory_rejectsRemoteTargetCollision()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("a/bin")));
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("b/bin")));
        {
            QFile f(tmp.filePath(QStringLiteral("a/bin/tool.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("1");
        }
        {
            QFile f(tmp.filePath(QStringLiteral("b/bin/tool.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("2");   // 与 a/bin/tool.txt 同名不同父 → 展开远端 rel 同为 "bin/tool.txt"
        }

        TaskTemplate in;
        in.name = QStringLiteral("冲突拒绝").toStdString();
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* remoteDir = dlg.findChild<QLineEdit*>(QStringLiteral("teRemoteDir"));
        QVERIFY(remoteDir);
        remoteDir->setText(QStringLiteral("/opt/app"));
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);

        QString error;
        // 第一次添加 a/bin → 成功 1 行（远端 /opt/app/bin/tool.txt）
        QCOMPARE(dlg.addDeployDirectoryEntries(tmp.filePath(QStringLiteral("a/bin")), &error), 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(table->rowCount(), 1);

        // 第二次添加 b/bin → 展开远端目标与既有行同为 /opt/app/bin/tool.txt → 拒绝 -1、
        // 中文 error、表格零改动（旧实现此处静默追加重复远端行——红）
        QCOMPARE(dlg.addDeployDirectoryEntries(tmp.filePath(QStringLiteral("b/bin")), &error), -1);
        QVERIFY2(!error.isEmpty(), "远端目标冲突必须给出中文拒绝原因");
        QVERIFY2(error.contains(QStringLiteral("冲突")), qPrintable(error));
        QCOMPARE(table->rowCount(), 1);

        // 收集 items 无重复 remotePath（防半入表/后写覆盖静默累积）
        {
            TaskTemplate out;
            QVERIFY(dlg.collect(&out));
            const QVariantList items = out.steps[0].parameters.value(QStringLiteral("items")).toList();
            QCOMPARE(items.size(), 1);
            QStringList remotePaths;
            for (const QVariant& v : items)
                remotePaths << v.toMap().value(QStringLiteral("remotePath")).toString();
            const QSet<QString> unique(remotePaths.begin(), remotePaths.end());
            QCOMPARE(unique.size(), remotePaths.size());
        }

        // 非冲突目录仍成功（无误拒）：c/pkg → rel "pkg/…" 与既有 "bin/…" 无碰撞
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("c/pkg")));
        {
            QFile f(tmp.filePath(QStringLiteral("c/pkg/tool.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("3");
        }
        QCOMPARE(dlg.addDeployDirectoryEntries(tmp.filePath(QStringLiteral("c/pkg")), &error), 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(table->rowCount(), 2);
        TaskTemplate afterNonColliding;
        QVERIFY(dlg.collect(&afterNonColliding));
        const QVariantList fitems = afterNonColliding.steps[0].parameters.value(QStringLiteral("items")).toList();
        QCOMPARE(fitems.size(), 2);
        QStringList fremotes;
        for (const QVariant& v : fitems)
            fremotes << v.toMap().value(QStringLiteral("remotePath")).toString();
        const QSet<QString> funique(fremotes.begin(), fremotes.end());
        QCOMPARE(funique.size(), 2);   // 两条不同远端目标：/opt/app/bin/tool.txt 与 /opt/app/pkg/tool.txt
    }

    // v2.15 波3 任务12：目录展开批量刷表收口——批量插入窗口内必须抑制逐行
    // cellChanged→flushDeployTable 全表重序列化（QSignalSpy 钉桩：批窗口 cellChanged
    // 发射次数为 0；旧实现 3 行入表发 6 次 → 红）。O(N²)→O(N) 结论仅由代码审查与
    // 本行为保持用例背书，不写计时断言（CI 抖动无判据价值）。
    // 行为逐字节等价钉桩：行数不丢不增、种子行恒居首、m_steps 真值写回经
    // 「切步往返→按真值重灌」实证、collect() 条目集与顺序逐字段相符；
    // currentCellChanged 不因尾部插入移动当前单元格（新旧实现同为 0——移除按钮
    // 启用态无副作用的 equivalence 取证，置于 cellChanged 断言之前以便红名仍可见）。
    void editorDialog_addDeployDirectory_batchInsertFlushesOnceAndPreservesRows()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("pkg/bin")));
        {
            QFile f(tmp.filePath(QStringLiteral("pkg/bin/app.conf")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("a");
        }
        {
            QFile f(tmp.filePath(QStringLiteral("pkg/notes.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("b");
        }
        {
            QFile f(tmp.filePath(QStringLiteral("pkg/readme.md")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("c");
        }

        TaskTemplate in;
        in.name = QStringLiteral("批量刷表").toStdString();
        TaskStep deployStep;
        deployStep.type = TaskStepType::DeployFiles;
        deployStep.parameters.insert(QStringLiteral("items"), QVariantList{
            QVariantMap{{QStringLiteral("localPath"), QStringLiteral("D:/seed.bin")},
                        {QStringLiteral("remotePath"), QStringLiteral("/opt/app/seed.bin")},
                        {QStringLiteral("direction"), QStringLiteral("upload")},
                        {QStringLiteral("overwrite"), QStringLiteral("overwrite")}}});
        TaskStep rebootStep;
        rebootStep.type = TaskStepType::Reboot;
        in.steps.push_back(deployStep);
        in.steps.push_back(rebootStep);
        TaskTemplateEditorDialog dlg(in, {});
        auto* remoteDir = dlg.findChild<QLineEdit*>(QStringLiteral("teRemoteDir"));
        QVERIFY(remoteDir);
        remoteDir->setText(QStringLiteral("/opt/app"));
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);
        auto* remove = dlg.findChild<QPushButton*>(QStringLiteral("teRemoveItem"));
        QVERIFY(remove);
        auto* stepList = dlg.findChild<QListWidget*>(QStringLiteral("teStepList"));
        QVERIFY(stepList);
        QCOMPARE(table->rowCount(), 1);                        // 绑定态种子行已在表

        QSignalSpy cellSpy(table, &QTableWidget::cellChanged);
        QSignalSpy currentSpy(table, &QTableWidget::currentCellChanged);

        QString error;
        QCOMPARE(dlg.addDeployDirectoryEntries(tmp.filePath(QStringLiteral("pkg")), &error), 3);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QCOMPARE(currentSpy.count(), 0);
        QVERIFY2(!remove->isEnabled(), "批量添加后移除按钮启用态不变（无当前单元格，与旧实现一致）");
        // 收口核心红：批窗口内零逐行 flush 信号（旧实现 3 行 × 2 次 setItem=6）
        QCOMPARE(cellSpy.count(), 0);

        // 行不丢不增、种子行居首、展开行按表格序追加
        QCOMPARE(table->rowCount(), 4);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("D:/seed.bin"));
        QStringList locals;
        QStringList remotes;
        for (int row = 1; row < 4; ++row) {
            locals << table->item(row, 0)->text();
            remotes << table->item(row, 1)->text();
            QVERIFY2(!table->item(row, 0)->text().endsWith(QStringLiteral("/pkg")),
                     "展开行必须是普通文件而非目录");
        }
        QCOMPARE(QSet<QString>(locals.begin(), locals.end()).size(), 3);   // 无重复行
        QVERIFY(remotes.contains(QStringLiteral("/opt/app/pkg/bin/app.conf")));   // 镜像相对段
        QVERIFY(remotes.contains(QStringLiteral("/opt/app/pkg/notes.txt")));
        QVERIFY(remotes.contains(QStringLiteral("/opt/app/pkg/readme.md")));
        QCOMPARE(QSet<QString>(remotes.begin(), remotes.end()).size(), 3); // 批量化未旁路互异远端

        // m_steps 真值写回实证：切到 reboot 再切回 deploy——绑定重灌按 m_steps
        // 回表；若批末单次 flush 未发生，此处表回到 1 行（种子行）
        stepList->setCurrentRow(1);
        stepList->setCurrentRow(0);
        QCOMPARE(table->rowCount(), 4);

        // collect() 条目集与顺序逐字段相符（种子行在前，展开行保持表格序，
        // 方向/覆盖为引擎规范串）
        TaskTemplate out;
        QVERIFY(dlg.collect(&out));
        const QVariantList items =
            out.steps[0].parameters.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 4);
        const QVariantMap seed = items[0].toMap();
        QCOMPARE(seed.value(QStringLiteral("localPath")).toString(), QStringLiteral("D:/seed.bin"));
        QCOMPARE(seed.value(QStringLiteral("remotePath")).toString(), QStringLiteral("/opt/app/seed.bin"));
        QCOMPARE(seed.value(QStringLiteral("direction")).toString(), QStringLiteral("upload"));
        QCOMPARE(seed.value(QStringLiteral("overwrite")).toString(), QStringLiteral("overwrite"));
        for (int i = 0; i < 3; ++i) {
            const QVariantMap entry = items[i + 1].toMap();
            QCOMPARE(entry.value(QStringLiteral("localPath")).toString(), locals.at(i));
            QCOMPARE(entry.value(QStringLiteral("remotePath")).toString(), remotes.at(i));
            QCOMPARE(entry.value(QStringLiteral("direction")).toString(), QStringLiteral("upload"));
            QCOMPARE(entry.value(QStringLiteral("overwrite")).toString(), QStringLiteral("overwrite"));
        }
    }

    // v2.15 波3 任务12（规格 §3.1 条款 4「空目录跳过不产条目、提示、不算错误」）：
    // 空目录添加仍返回 0、*error 保持空、表格零改动，但用户必须收到中文提示。
    // 提示通道沿用本对话框既有 QMessageBox 形态（validate 闸与波2 冲突拒绝同通道；
    // 本对话框无独立日志 sink，不发明新通道），information 而非 warning——空目录
    // 不算错误。捕获形态照搬 armDialogCapture：布防定时器→exec 嵌套事件循环触发
    // →抓文案+done(Ok)+置标志；未弹框则标志保持 false（负对照）。
    void editorDialog_addDeployDirectory_emptyDirShowsChineseNoticeAndAddsNothing()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("emptydir")));   // 无文件无子目录

        TaskTemplate in;
        in.name = QStringLiteral("空目录提示").toStdString();
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* remoteDir = dlg.findChild<QLineEdit*>(QStringLiteral("teRemoteDir"));
        QVERIFY(remoteDir);
        remoteDir->setText(QStringLiteral("/opt/app"));
        auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("teItemList"));
        QVERIFY(table);

        QString noticeText;
        bool noticeSeen = false;
        QTimer::singleShot(50, &dlg, [&dlg, &noticeText, &noticeSeen] {
            const auto boxes = dlg.findChildren<QMessageBox*>();
            if (boxes.isEmpty())
                return;
            noticeText = boxes.first()->text();
            noticeSeen = true;
            boxes.first()->done(QMessageBox::Ok);
        });

        QString error;
        QCOMPARE(dlg.addDeployDirectoryEntries(tmp.filePath(QStringLiteral("emptydir")), &error), 0);
        QVERIFY2(noticeSeen, "空目录添加必须弹出中文提示（未命中 QMessageBox：当前实现静默）");
        QVERIFY2(noticeText.contains(QStringLiteral("所选目录为空，未添加任何文件条目")),
                 qPrintable(noticeText));
        QVERIFY2(error.isEmpty(), "空目录不算错误：*error 不应携带拒绝串");
        QCOMPARE(table->rowCount(), 0);
    }

    // ── ⑫ v2.13 #54 Task 4：commands/recover 面板（多行命令 + timeoutSec + protocol）──
    void editorDialog_commandsPanelSplitsLinesAndClamps()
    {
        TaskTemplate in; in.name = "n";
        TaskStep step; step.type = TaskStepType::RunCommands;
        step.parameters.insert(QStringLiteral("commands"),
                               QStringList{QStringLiteral("ls"), QStringLiteral("pwd")});
        in.steps.push_back(step);
        TaskTemplateEditorDialog dlg(in, {});
        auto* edit = dlg.findChild<QPlainTextEdit*>(QStringLiteral("teCommands"));
        QVERIFY(edit);                                             // 此前不存在 → 红
        QCOMPARE(edit->toPlainText().split('\n').size(), 2);
        edit->setPlainText(QStringLiteral("reboot\n\n # 注释行会被 trim 去空保留? 否——按行 trim 后仍保留非空"));
        TaskTemplate out; QVERIFY(dlg.collect(&out));
        const auto cmds = out.steps.front().parameters.value(QStringLiteral("commands")).toStringList();
        QCOMPARE(cmds.first(), QStringLiteral("reboot"));
        auto* spin = dlg.findChild<QSpinBox*>(QStringLiteral("teTimeout"));
        QCOMPARE(spin->value(), 10);                               // 默认 10
    }

    // 评审 Critical 1 回归：fillCommandsLike 若在程序化灌值后不解除 blockSignals，
    // 首次绑定后 teCommands 的 textChanged 永久封锁 → 用户编辑永不 flush 到 m_steps，
    // 切步即静默丢改动。用 run_commands(C1)+recover(R1) 两步各自编辑并来回切换验证存活。
    void editorDialog_commandEditsSurviveStepSwitch()
    {
        TaskTemplate in; in.name = "n";
        TaskStep c1; c1.type = TaskStepType::RunCommands;
        c1.parameters.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("ls"), QStringLiteral("pwd")});
        TaskStep r1; r1.type = TaskStepType::Recover;
        r1.parameters.insert(QStringLiteral("commands"),
                             QStringList{QStringLiteral("echo recovered")});
        in.steps.push_back(c1);
        in.steps.push_back(r1);
        TaskTemplateEditorDialog dlg(in, {});

        auto* list = dlg.findChild<QListWidget*>(QStringLiteral("teStepList"));
        auto* edit = dlg.findChild<QPlainTextEdit*>(QStringLiteral("teCommands"));
        QVERIFY(list && edit);

        list->setCurrentRow(0);                                    // 绑定 C1
        edit->setPlainText(QStringLiteral("edited-one\nedited-two")); // 信号应实时 flush 到 m_steps[0]

        list->setCurrentRow(1);                                    // 切到 R1（fillCommandsLike 灌 R1）
        list->setCurrentRow(0);                                    // 切回 C1：编辑器应显示已改内容

        TaskTemplate out; QVERIFY(dlg.collect(&out));
        const auto c1cmds = out.steps[0].parameters.value(QStringLiteral("commands")).toStringList();
        QCOMPARE(c1cmds, (QStringList{QStringLiteral("edited-one"), QStringLiteral("edited-two")}));
        const auto r1cmds = out.steps[1].parameters.value(QStringLiteral("commands")).toStringList();
        QCOMPARE(r1cmds, (QStringList{QStringLiteral("echo recovered")})); // R1 自身内容不受扰动
    }

    // ── ⑬ v2.13 #54 Task 5：任务中心接线（新建/编辑/删除入口 + 删除三态）────
    void taskCenter_newEditDeleteFlows()
    {
        TaskTemplateStore store;
        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        auto* list = center.findChild<QListWidget*>(QStringLiteral("templateList"));
        // 新建：注入对话框太直白——经公共缝：直接构造编辑器+save 已是 Task2 覆盖，
        // 本用例聚焦接线：三按钮存在 + 删除三态
        auto* newBtn = center.findChild<QPushButton*>(QStringLiteral("taskNewTemplateButton"));
        auto* editBtn = center.findChild<QPushButton*>(QStringLiteral("taskEditTemplateButton"));
        auto* delBtn = center.findChild<QPushButton*>(QStringLiteral("taskDeleteTemplateButton"));
        QVERIFY(newBtn && editBtn && delBtn);
        QVERIFY(!editBtn->isEnabled());   // 未选中
        QVERIFY(!delBtn->isEnabled());
        QVERIFY(newBtn->isEnabled());     // 恒可用
        // 种一份模板 → 选中 → 按钮态翻转 → 布防确认框点「否」→ 不删
        TaskTemplate t; t.name = "接线票";
        TaskStep s; s.type = TaskStepType::Reboot; t.steps.push_back(s);
        QVERIFY(store.save(t));
        // brief 顺序缺陷修正（其余逐字）：setStores 早于 save，而 store 直存不通知
        // widget（TaskTemplateStore/ConfigStore 非 QObject 无变更信号），列表仍是空的，
        // 不重绑则 setCurrentRow(0) 无行可选中。按 ⑧b「重绑刷新」既有口径补一刀。
        center.setStores(&store, nullptr);
        list->setCurrentRow(0);
        QVERIFY(editBtn->isEnabled() && delBtn->isEnabled());
        QTimer::singleShot(50, this, [this]{
            for (auto* box : QApplication::topLevelWidgets())
                if (auto* mb = qobject_cast<QMessageBox*>(box)) { mb->done(QMessageBox::No); return; }
        });
        delBtn->click();
        QCOMPARE((int)store.list().size(), 1);                    // 取消 → 不删
        QTimer::singleShot(50, this, [this]{
            for (auto* box : QApplication::topLevelWidgets())
                if (auto* mb = qobject_cast<QMessageBox*>(box)) { mb->done(QMessageBox::Yes); return; }
        });
        delBtn->click();
        QVERIFY(store.list().empty());                            // 确认 → 删
        QVERIFY(!delBtn->isEnabled());                            // 列表空 → 复位禁用
    }

    // ── ⑬ v2.13 #54 Task 5：applyEditorResult 公共编排缝（落库+刷新+回选）────
    void taskCenter_editDialogAccepted_savesInPlaceKeepingId()
    {
        // 经 TaskTemplateEditorDialog 直构 + center 提供的公开编排缝：
        // center.applyEditorResult(template) 返回 store 结果 + 刷新（接线复用 collect 流）
        TaskTemplateStore store;
        TaskCenterWidget center;
        center.setStores(&store, nullptr);
        TaskTemplate t; t.name = "改前";
        TaskStep s; s.type = TaskStepType::Reboot; t.steps.push_back(s);
        QVERIFY(store.save(t));
        TaskTemplate edited = store.list().front();
        edited.name = "改后";
        QVERIFY(center.applyEditorResult(edited));                // 原位覆盖（id 非空）
        QCOMPARE(QString::fromStdString(store.list().front().name), QStringLiteral("改后"));
        QCOMPARE((int)store.list().size(), 1);
        TaskTemplate fresh; fresh.name = "新建";                  // id 空 → 生成
        // brief 缺陷修正（其余逐字）：无步骤模板被 TaskTemplateStore::save 的
        // validateTaskTemplate「至少一个有序步骤」契约拒绝，与接线缝无关，补一个步骤
        TaskStep fs; fs.type = TaskStepType::Reboot; fresh.steps.push_back(fs);
        QVERIFY(center.applyEditorResult(fresh));
        QCOMPARE((int)store.list().size(), 2);
        TaskTemplate invalid;                                     // 空名 → 拒
        QVERIFY(!center.applyEditorResult(invalid));
    }

    // ── ⑭ v2.13 #54 Task 6：e2e「新建→执行」链（编辑器产物必须引擎可跑）────
    // 全链走公共缝：TaskTemplateEditorDialog::collect（编辑器出厂两步 deploy+reboot）
    // → applyEditorResult（落库 + store 生成新 id）→ startTemplate（resolveTargets
    // → confirmer → TaskRunStore::begin → 引擎）。档案只给 FTP 端点（同⑥不触网口径）：
    // 重启步骤必在 Validating 阶段校验失败——既证明模板两步真达引擎逐步校验
    // （错误文本点名「步骤 2（重启）」），又保证收口确定且全程无网络/适配器依赖。
    void taskCenter_editorProducedTemplate_executesFullChain()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(
            QStringLiteral("dev-ed"), QStringLiteral("链式目标机"),
            QStringLiteral("10.7.0.1"), 21)));                   // 仅 FTP 端点

        TaskTemplateStore store;
        TaskRunStore runs;

        // ① 编辑器缝：初值两步模板 → collect（templateId 空 = 新建路）
        TaskTemplate in;
        in.name = "链式出厂";
        in.tagSelectors = {"巡检线"};                             // 顺带补 T2 遗留：标签往返
        in.deviceIds = {"dev-ed"};
        TaskStep deploy; deploy.type = TaskStepType::DeployFiles;
        QVariantList items;
        QVariantMap one;
        one.insert(QStringLiteral("localPath"), QStringLiteral("C:/ed/a.bin"));
        one.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/a.bin"));
        items.push_back(one);
        deploy.parameters.insert(QStringLiteral("items"), items);
        TaskStep reboot; reboot.type = TaskStepType::Reboot;
        in.steps.push_back(deploy);
        in.steps.push_back(reboot);
        TaskTemplateEditorDialog dlg(in, registry.list());
        TaskTemplate produced;
        QVERIFY(dlg.collect(&produced));
        QVERIFY(produced.templateId.empty());                    // 新建：id 留给 store 生成
        QCOMPARE(produced.tagSelectors, in.tagSelectors);
        QCOMPARE(produced.deviceIds, in.deviceIds);
        QCOMPARE(produced.steps.size(), std::size_t{2});

        TaskCenterWidget center;
        center.setRegistry(&registry);
        center.setStores(&store, &runs);
        int confirmCalls = 0;
        center.setRunConfirmer([&](const QString& summary, bool risky) {
            ++confirmCalls;
            // 重启动作风险位与摘要行必须随编辑器产物一路传到确认缝
            return risky && summary.contains(QStringLiteral("含重启动作"));
        });

        // ② 落库：applyEditorResult 生成新 id，两步模板一条不缺
        QVERIFY(center.applyEditorResult(produced));
        const auto rows = store.list();
        QCOMPARE(rows.size(), std::size_t{1});
        const std::string newId = rows.front().templateId;
        QVERIFY2(newId.rfind("template-", 0) == 0,
                 qPrintable(QString::fromStdString(newId)));
        const auto reloaded = store.load(newId);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->steps.size(), std::size_t{2});

        // ③ 执行：startTemplate 全链发起
        QSignalSpy started(&center, &TaskCenterWidget::runStarted);
        QVERIFY(started.isValid());
        QSignalSpy finished(&center, &TaskCenterWidget::runFinished);
        QVERIFY(finished.isValid());
        QSignalSpy logs(&center, &TaskCenterWidget::logMessage);
        QVERIFY(logs.isValid());
        center.startTemplate(newId);
        QCOMPARE(confirmCalls, 1);                               // 确认缝确实过了一遍
        QCOMPARE(started.count(), 1);                            // 执行已发起（同步发射）

        // 收口：重启步骤无 Telnet/SSH 端点 → 校验失败 → 历史落库一条不缺
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        const auto records = runs.query({});
        QCOMPARE(records.size(), std::size_t{1});
        QVERIFY(records.front().status == TaskRunStatus::Failed);
        QCOMPARE(records.front().templateId, newId);             // 记录关联到编辑器产出的模板
        QCOMPARE(records.front().devices.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(records.front().devices.front().deviceId),
                 QStringLiteral("dev-ed"));
        QCOMPARE(QString::fromStdString(records.front().devices.front().address),
                 QStringLiteral("10.7.0.1:21"));                // begin 时快照的地址定格
        QVERIFY(QString::fromStdString(records.front().devices.front().name)
                    .contains(QStringLiteral("链式目标机")));     // 名称快照同理

        // 引擎逐步骤校验证据链：错误日志点名「步骤 2（重启）缺少可用端点」——
        // 该文本只可能来自编辑器产出的第二步被引擎 validateDeviceSteps 检查
        bool stepValidationLog = false;
        for (const QVariantList& args : logs) {
            if (args.at(0).toString().contains(QStringLiteral("步骤 2（重启）缺少可用端点")))
                stepValidationLog = true;
        }
        QVERIFY2(stepValidationLog, "未见引擎对第二步（重启）的校验失败日志");

        // UI 历史表同步收口（同⑥口径：终态行出现即落库刷新完成）
        auto* history = center.findChild<QTableWidget*>(QStringLiteral("taskRunHistory"));
        QVERIFY(history);
        QTRY_COMPARE_WITH_TIMEOUT(history->rowCount(), 1, 10000);
        QCOMPARE(history->item(0, 2)->text(), QStringLiteral("失败"));
    }

    // ── v2.14 #49-4：阶段恢复拒绝文案三分派（纯静态函数直测，无需真跑 33 轮）──
    void retryRejectionMessagesAreDistinguishable()
    {
        // 分支一：lineage 从未登记（本会话外启动的历史运行）
        const QString lineageUnknown =
            TaskCenterWidget::retryRejectionMessage(false, false);
        // 分支二：lineage 已知，但引擎 32 轮 FIFO 已淘汰该运行存档
        const QString evicted =
            TaskCenterWidget::retryRejectionMessage(true, false);
        // 分支三：lineage 已知且存档仍在，retryFailed 仍拒绝（最可能活跃运行占用）
        const QString stillRejected =
            TaskCenterWidget::retryRejectionMessage(true, true);

        QVERIFY2(lineageUnknown.contains(QStringLiteral("本会话")), qPrintable(lineageUnknown));
        // 轮数上限必须单源取自 TaskExecutionEngine::kHistoryLimit（防文案与引擎漂移）
        QVERIFY2(evicted.contains(QStringLiteral("超出 %1 轮上限")
                                      .arg(TaskExecutionEngine::kHistoryLimit)),
                 qPrintable(evicted));
        QVERIFY2(stillRejected.contains(QStringLiteral("活跃运行")), qPrintable(stillRejected));

        // 三条文案两两不同——分派的意义就在于用户能从文案分辨真因（防同形退化）
        QVERIFY(lineageUnknown != evicted);
        QVERIFY(lineageUnknown != stillRejected);
        QVERIFY(evicted != stillRejected);
    }

    // ── v2.15 #49-6：运行终态聚合纯头直测（收敛规则单源 TaskRunAggregate.h）──
    void aggregatePureHeaderSingleSource()
    {
        // #49-6：收敛规则唯一口径在 TaskRunAggregate.h；成员函数与 e2e 都必须是它的转发
        TaskRunOutcome o;
        o.devices = {{}, {}};
        o.devices[0].state = TaskDeviceState::Cancelled;
        o.devices[1].state = TaskDeviceState::Failed;
        QCOMPARE(taskrun::aggregateRunStatus(o), TaskRunStatus::Failed);
        o.devices[1].state = TaskDeviceState::Succeeded;
        QCOMPARE(taskrun::aggregateRunStatus(o), TaskRunStatus::PartiallySucceeded);
        QCOMPARE(taskrun::aggregateRunStatus(TaskRunOutcome{}), TaskRunStatus::Failed); // 空 outcome 非全成功
        o.devices[0].state = TaskDeviceState::Succeeded;
        QCOMPARE(taskrun::aggregateRunStatus(o), TaskRunStatus::Succeeded);

        // v2.15 终审 [T3]：Skipped 归 succeeded 桶的边界钉桩——
        // 单设备全 Skipped → Succeeded；Skipped+Failed 混布 → PartiallySucceeded
        TaskRunOutcome sk;
        sk.devices = {{}};
        sk.devices[0].state = TaskDeviceState::Skipped;
        QCOMPARE(taskrun::aggregateRunStatus(sk), TaskRunStatus::Succeeded);
        sk.devices = {{}, {}};
        sk.devices[0].state = TaskDeviceState::Skipped;
        sk.devices[1].state = TaskDeviceState::Failed;
        QCOMPARE(taskrun::aggregateRunStatus(sk), TaskRunStatus::PartiallySucceeded);
    }
};

QTEST_MAIN(TstTaskCenterWidget)
#include "tst_task_center_widget.moc"
