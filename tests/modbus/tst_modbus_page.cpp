// v2.12 MODBUS 重构 Task 3：双栏连接清单与逐寄存器视图回归。
// 覆盖：无档案回退 ip:port、添加目标下拉（仅 modbus 端点设备 + 其它设备…）、
// 逐寄存器 4xxxx 编址、十/十六进制切换、变化标记仅增量、两表列宽 Interactive、
// 写确认默认取消（含 shouldConfirmWrite 纯函数）、搜索过滤纯函数、
// 端口编辑写回档案（saveReplacing 同步全行）、异常保留末值置灰、2000 行宽容限性能、
// 后端结果入 store 不受搜索过滤影响（轮1 发现 1）、逐寄存器可视区抑制与滚入补发（轮1 发现 2）。
#include <QtTest>

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QDir>
#include <QUuid>

#include "config/ConfigStore.h"
#include "device/DeviceProfile.h"
#include "device/DeviceRegistry.h"
#include "tools/ModbusTool/ModbusDataStore.h"
#include "tools/ModbusTool/ModbusPointModel.h"
#include "tools/ModbusTool/ModbusTargetListModel.h"
#include "tools/ModbusTool/ModbusTargetStore.h"
#include "tools/ModbusTool/ModbusWidget.h"

using modbus::ModbusDataStore;
using modbus::ModbusPointModel;
using modbus::ModbusTargetListModel;
using modbus::ModbusTargetStore;
using modbus::PollSpec;
using modbus::SlaveTarget;
using modbus::DeviceTargets;
using modbus::TargetRow;

namespace {

DeviceProfile makeProfile(const QString& deviceId, const QString& name,
                          const QString& protocol, const QString& ip, int port)
{
    DeviceProfile profile;
    profile.deviceId = deviceId.toStdString();
    profile.name = name.toStdString();
    profile.endpoints.push_back({protocol.toStdString(), ip.toStdString(), port, std::string()});
    return profile;
}

QStringList listTexts(QListWidget* list)
{
    QStringList texts;
    for (int i = 0; i < list->count(); ++i) texts << list->item(i)->text();
    return texts;
}

bool anyContains(const QStringList& texts, const QString& needle)
{
    for (const QString& text : texts) {
        if (text.contains(needle)) return true;
    }
    return false;
}

void clickListRow(QWidget* host, QListWidget* list, const QString& needle)
{
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->text().contains(needle)) {
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(list->item(i)).center());
            return;
        }
    }
    QFAIL(qPrintable(QStringLiteral("列表中未找到: ").append(needle)));
}

QTableView* tableByObjectName(QWidget* w, const char* name)
{
    return w->findChild<QTableView*>(QString::fromLatin1(name));
}

void assertColumnsResizable(QTableView* table, int columnCount)
{
    QVERIFY(table);
    QVERIFY2(table->model(), "模型未装配");
    QCOMPARE(table->model()->columnCount(), columnCount);
    for (int col = 0; col < columnCount - 1; ++col) {
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(col),
                 static_cast<QHeaderView::ResizeMode>(QHeaderView::Interactive));
        // 隐藏列（十六进制列默认隐藏）宽度 0 属 Qt 语义，只约束当前可见列
        if (table->isColumnHidden(col))
            continue;
        QVERIFY2(table->columnWidth(col) >= 40,
                 qPrintable(QStringLiteral("%1 列 %2 缺少初始宽度(宽=%3)")
                                .arg(table->objectName()).arg(col).arg(table->columnWidth(col))));
    }
    QCOMPARE(table->horizontalHeader()->sectionResizeMode(columnCount - 1),
             static_cast<QHeaderView::ResizeMode>(QHeaderView::Stretch));
}

QVariant cellData(const QAbstractItemModel* model, int row, int col, int role = Qt::DisplayRole)
{
    return model->data(model->index(row, col), role);
}

} // namespace

class TstModbusPage : public QObject {
    Q_OBJECT

private:
    QString m_workDir;
    QString m_dbPath;
    // writeConfirmDefaultsCancel 布防取证
    bool m_writeBoxSeen = false;
    bool m_writeBoxDefaultIsCancel = false;

private slots:
    void initTestCase()
    {
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_modbus_page_%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
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
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
        m_writeBoxSeen = false;
        m_writeBoxDefaultIsCancel = false;
    }

    void cleanup()
    {
        ConfigStore::instance().close();
    }

    // ── 裁决 3：registry 缺席回退 ip:port，不得空指针 ──────────────
    void nullRegistryFallsBackToAddress()
    {
        QVariantMap row;
        row.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.1"));
        row.insert(QStringLiteral("port"), 502);
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.1:502"), row));
        ModbusWidget w;
        w.setRegistry(nullptr);  // 显式空指针不得崩
        auto* tm = w.targetModel();
        QVERIFY(tm);
        QVERIFY2(tm->refresh() >= 1, "device.list 回退模式必须产出一行清单");
        QCOMPARE(cellData(tm, 0, ModbusTargetListModel::ColName).toString(),
                 QStringLiteral("10.0.0.1:502"));
        // 纯函数：名称优先、回退端点
        QCOMPARE(ModbusTargetListModel::resolveName(QString(), QStringLiteral("10.0.0.1:502")),
                 QStringLiteral("10.0.0.1:502"));
        QCOMPARE(ModbusTargetListModel::resolveName(QStringLiteral("PLC-A"),
                                                    QStringLiteral("10.0.0.1:502")),
                 QStringLiteral("PLC-A"));
    }

    // ── 添加目标下拉：只列 modbus 端点设备 + 其它设备…入口 ─────────
    void addTargetListsOnlyModbusDevices()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-plc-a"), QStringLiteral("PLC-A"),
                                          QStringLiteral("modbus"), QStringLiteral("10.0.0.1"), 502)));
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-ftp-b"), QStringLiteral("FTP-B"),
                                          QStringLiteral("ftp"), QStringLiteral("10.0.0.2"), 21)));

        ModbusWidget w;
        w.setRegistry(&registry);
        w.show();
        QCoreApplication::processEvents();

        auto* add = w.findChild<QPushButton*>(QStringLiteral("mbAddTarget"));
        QVERIFY(add);
        add->click();
        QCoreApplication::processEvents();  // 对话框布局
        auto* dlg = w.findChild<QDialog*>(QStringLiteral("mbAddTargetDialog"));
        QVERIFY2(dlg, "添加目标对话框未出现");
        auto* list = dlg->findChild<QListWidget*>(QStringLiteral("mbAddTargetList"));
        QVERIFY(list);
        const QStringList texts = listTexts(list);
        QVERIFY2(texts.size() == 2, qPrintable(QStringLiteral("期望恰 2 项，实际: %1").arg(texts.join("|"))));
        QVERIFY2(anyContains(texts, QStringLiteral("PLC-A")), qPrintable(texts.join("|")));
        QVERIFY2(!anyContains(texts, QStringLiteral("FTP-B")), "非 modbus 端点设备不得直列");
        QVERIFY2(anyContains(texts, QStringLiteral("其它设备…")), qPrintable(texts.join("|")));

        // 选 PLC-A → 生成默认目标记录并出现在清单
        clickListRow(&w, list, QStringLiteral("PLC-A"));
        QCoreApplication::processEvents();  // WA_DeleteOnClose 析构，避免第二开窗被防重复拦截
        auto* tm = w.targetModel();
        QVERIFY2(tm->rowByTargetId(QStringLiteral("dev-plc-a#1")),
                 "添加后应生成 dev-plc-a#1 行");

        // 其它设备…：二级选择 FTP-B → 建默认 502 modbus 端点写回档案 + 生成目标行
        add->click();
        QCoreApplication::processEvents();
        auto* dlg2 = w.findChild<QDialog*>(QStringLiteral("mbAddTargetDialog"));
        QVERIFY(dlg2);
        auto* list2 = dlg2->findChild<QListWidget*>(QStringLiteral("mbAddTargetList"));
        QVERIFY(list2);
        clickListRow(&w, list2, QStringLiteral("其它设备…"));
        QVERIFY(anyContains(listTexts(list2), QStringLiteral("FTP-B")));
        clickListRow(&w, list2, QStringLiteral("FTP-B"));
        QCoreApplication::processEvents();
        const auto refreshed = registry.find("dev-ftp-b");
        QVERIFY(refreshed);
        bool hasModbus502 = false;
        for (const auto& ep : refreshed->endpoints) {
            if (ep.protocol == "modbus" && ep.port == 502 && ep.ip == "10.0.0.2") hasModbus502 = true;
        }
        QVERIFY2(hasModbus502, "其它设备须建默认 502 modbus 端点写回档案");
        QVERIFY2(tm->rowByTargetId(QStringLiteral("dev-ftp-b#1")), "端点建立后目标行应入清单");
        w.hide();
    }

    // ── 逐寄存器视图：4xxxx 编址 + 行数 = count ────────────────────
    void pointModelAddressAndCount()
    {
        PollSpec spec;
        spec.regType = QModbusDataUnit::HoldingRegisters;
        spec.startAddr = 0;
        spec.count = 3;
        ModbusPointModel m;
        m.setTarget(QStringLiteral("dev#1"), spec, {}, {});
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(cellData(&m, 0, ModbusPointModel::ColAddress).toString(), QStringLiteral("40001"));
        QCOMPARE(cellData(&m, 1, ModbusPointModel::ColAddress).toString(), QStringLiteral("40002"));
        QCOMPARE(cellData(&m, 2, ModbusPointModel::ColAddress).toString(), QStringLiteral("40003"));
    }

    // ── 十/十六进制切换：关显示 1234、开显示 0x04D2 ────────────────
    void hexToggleChangesDisplay()
    {
        PollSpec spec; // Holding start 0 count 1（默认）
        ModbusPointModel m;
        m.setTarget(QStringLiteral("dev#1"), spec, {1234}, {1234});
        QCOMPARE(cellData(&m, 0, ModbusPointModel::ColDecimal).toString(), QStringLiteral("1234"));
        QCOMPARE(cellData(&m, 0, ModbusPointModel::ColHex).toString(), QStringLiteral("0x04D2"));

        ModbusWidget w;
        w.show();
        QCoreApplication::processEvents();
        auto* pt = tableByObjectName(&w, "mbPointTable");
        QVERIFY(pt);
        auto* toggle = w.findChild<QPushButton*>(QStringLiteral("mbHexToggle"));
        QVERIFY(toggle);
        QVERIFY2(toggle->isCheckable(), "mbHexToggle 必须可勾选");
        auto* pm = w.pointModel();
        pm->setTarget(QStringLiteral("dev#1"), spec, {1234}, {1234});
        // 默认十进制可见、十六进制隐藏
        QVERIFY(!pt->isColumnHidden(ModbusPointModel::ColDecimal));
        QVERIFY(pt->isColumnHidden(ModbusPointModel::ColHex));
        toggle->click();
        QVERIFY(pt->isColumnHidden(ModbusPointModel::ColDecimal));
        QVERIFY(!pt->isColumnHidden(ModbusPointModel::ColHex));
        QCOMPARE(cellData(pt->model(), 0, ModbusPointModel::ColHex).toString(), QStringLiteral("0x04D2"));
        w.hide();
    }

    // ── 变化标记：仅增量行有标记 ───────────────────────────────────
    void changedFlagOnlyOnDelta()
    {
        PollSpec spec;
        spec.count = 2;
        ModbusPointModel m;
        m.setTarget(QStringLiteral("dev#1"), spec, {1, 2}, {1, 2});
        QCOMPARE(cellData(&m, 0, ModbusPointModel::ColChanged).toString(), QString());

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(spy.isValid());
        m.applyUpdate({1, 2}, {1, 3});
        // 增量刷新：只对被改动的行发 dataChanged（非整表风暴）
        QCOMPARE(spy.count(), 1);
        const QModelIndex tl = spy.at(0).at(0).toModelIndex();
        const QModelIndex br = spy.at(0).at(1).toModelIndex();
        QVERIFY(tl.isValid() && tl.row() == 1);
        QVERIFY(br.isValid() && br.row() == 1);
        QCOMPARE(cellData(&m, 0, ModbusPointModel::ColChanged).toString(), QString());
        QCOMPARE(cellData(&m, 1, ModbusPointModel::ColChanged).toString(), QStringLiteral("↑"));
        QCOMPARE(cellData(&m, 1, ModbusPointModel::ColDecimal).toString(), QStringLiteral("3"));
    }

    // ── 两表列宽 Interactive + 初始宽（v2.11.1 ③教训入族规） ──────
    void pointColumnsAreResizable()
    {
        ModbusWidget w;
        w.show();
        QCoreApplication::processEvents();
        assertColumnsResizable(tableByObjectName(&w, "mbTargetTable"),
                               ModbusTargetListModel::ColumnCount);
        assertColumnsResizable(tableByObjectName(&w, "mbPointTable"),
                               ModbusPointModel::ColumnCount);
        w.hide();
    }

    // ── 写确认：shouldConfirmWrite 恒需确认 + QMessageBox 默认取消 ─
    void writeConfirmDefaultsCancel()
    {
        QVERIFY(ModbusWidget::shouldConfirmWrite(QStringLiteral("dev#1"), 0, 1));

        ModbusWidget w;
        QTimer::singleShot(50, this, [this] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* top : widgets) {
                auto* box = qobject_cast<QMessageBox*>(top);
                if (!box || box->objectName() != QStringLiteral("mbWriteConfirm")) continue;
                m_writeBoxSeen = true;
                m_writeBoxDefaultIsCancel =
                    box->standardButton(box->defaultButton()) == QMessageBox::Cancel;
                box->reject();
                return;
            }
        });
        QTimer::singleShot(3000, this, [] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* top : widgets) {
                if (auto* box = qobject_cast<QMessageBox*>(top)) {
                    if (box->objectName() == QStringLiteral("mbWriteConfirm")) box->close();
                }
            }
        });
        const bool proceed = w.confirmWrite(QStringLiteral("dev#1"), 0, 1);
        QVERIFY2(m_writeBoxSeen, "写确认对话框未出现（objectName=mbWriteConfirm）");
        QVERIFY2(m_writeBoxDefaultIsCancel, "写确认必须默认取消（保守方向）");
        QVERIFY(!proceed);
    }

    // ── filterBySearch 纯函数 + 模型级搜索过滤 ────────────────────
    void filterBySearchIsCaseInsensitiveOnNameAndEndpoint()
    {
        TargetRow r;
        r.name = QStringLiteral("PLC一号");
        r.endpoint = QStringLiteral("10.0.0.7:502");
        QVERIFY(ModbusTargetListModel::filterBySearch(r, QString()));
        QVERIFY(ModbusTargetListModel::filterBySearch(r, QStringLiteral("plc")));
        QVERIFY(ModbusTargetListModel::filterBySearch(r, QStringLiteral("PLC")));
        QVERIFY(ModbusTargetListModel::filterBySearch(r, QStringLiteral("一号")));
        QVERIFY(ModbusTargetListModel::filterBySearch(r, QStringLiteral("10.0.0.7")));
        QVERIFY(ModbusTargetListModel::filterBySearch(r, QStringLiteral("502")));
        QVERIFY(!ModbusTargetListModel::filterBySearch(r, QStringLiteral("99.99")));

        // 模型级：device.list 回退两台，搜索过滤只剩匹配行
        QVariantMap a;
        a.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.1"));
        a.insert(QStringLiteral("port"), 502);
        QVariantMap b;
        b.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.2"));
        b.insert(QStringLiteral("port"), 502);
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.1:502"), a));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.2:502"), b));
        ModbusTargetStore store;
        ModbusDataStore data;
        ModbusTargetListModel m(&store, &data);
        QCOMPARE(m.refresh(), 2);
        m.setSearchText(QStringLiteral("10.0.0.2"));
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(cellData(&m, 0, ModbusTargetListModel::ColEndpoint).toString(),
                 QStringLiteral("10.0.0.2:502"));
    }

    // ── 裁决 4：端口编辑写回档案（saveReplacing），同设备其余行同步 ─
    void portEditWritesBackToProfile()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-plc-a"), QStringLiteral("PLC-A"),
                                          QStringLiteral("modbus"), QStringLiteral("10.0.0.1"), 502)));
        ModbusTargetStore store;
        DeviceTargets dt;
        dt.deviceId = "dev-plc-a";
        SlaveTarget s1; s1.slaveId = 1;
        SlaveTarget s3; s3.slaveId = 3;
        dt.targets = {s1, s3};
        QVERIFY(store.save(dt));

        ModbusWidget w;
        w.setRegistry(&registry);
        w.show();
        QCoreApplication::processEvents();
        auto* tm = w.targetModel();
        QCOMPARE(tm->refresh(), 2);
        QCOMPARE(cellData(tm, 0, ModbusTargetListModel::ColEndpoint).toString(),
                 QStringLiteral("10.0.0.1:502"));

        auto* tv = tableByObjectName(&w, "mbTargetTable");
        QVERIFY(tv);
        tv->edit(tm->index(0, ModbusTargetListModel::ColEndpoint));
        auto* editor = w.findChild<QLineEdit*>(QStringLiteral("mbPortEdit"));
        QVERIFY2(editor, "端点列 delegate 必须暴露 mbPortEdit 编辑入口");
        editor->setText(QStringLiteral("5025"));
        QTest::keyClick(editor, Qt::Key_Return);
        QCoreApplication::processEvents();  // 写回后 refresh 经事件队列执行

        const auto after = registry.find("dev-plc-a");
        QVERIFY(after);
        bool portApplied = false;
        for (const auto& ep : after->endpoints) {
            if (ep.protocol == "modbus" && ep.port == 5025) portApplied = true;
        }
        QVERIFY2(portApplied, "端口编辑须经 saveReplacing 写回档案");
        QCOMPARE(tm->rowCount(), 2);
        QCOMPARE(cellData(tm, 0, ModbusTargetListModel::ColEndpoint).toString(),
                 QStringLiteral("10.0.0.1:5025"));
        QCOMPARE(cellData(tm, 1, ModbusTargetListModel::ColEndpoint).toString(),
                 QStringLiteral("10.0.0.1:5025"));
        w.hide();
    }

    // ── 裁决 1：error 非空 = 异常，值列照常显示旧 values 但置灰 ────
    void errorKeepsValuesButMarksAbnormal()
    {
        QVariantMap row;
        row.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.1"));
        row.insert(QStringLiteral("port"), 502);
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.1:502"), row));
        const QString targetId = QStringLiteral("10.0.0.1:502#1");
        ModbusTargetStore store;
        ModbusDataStore data;
        data.updatePoll(targetId, {1234}, 100);
        data.updateError(targetId, QStringLiteral("从站忙拒收，超时重试"), 200);
        QVERIFY2(data.sample(targetId)->ok, "updateError 保留旧 ok 的既定语义");
        QVERIFY(!data.sample(targetId)->error.isEmpty());

        ModbusTargetListModel m(&store, &data);
        QCOMPARE(m.refresh(), 1);
        const TargetRow* r = m.rowAt(0);
        QVERIFY(r);
        QVERIFY2(r->hasError, "健康判定须以 Sample.error 非空为准，不得单信 ok");
        QCOMPARE(cellData(&m, 0, ModbusTargetListModel::ColStatus).toString(),
                 QStringLiteral("异常"));
        QVERIFY(cellData(&m, 0, ModbusTargetListModel::ColDetail).toString()
                    .contains(QStringLiteral("从站忙")));
        QVERIFY(cellData(&m, 0, ModbusTargetListModel::ColDetail, Qt::ToolTipRole).isValid());

        PollSpec spec;
        ModbusPointModel pm;
        pm.setTarget(targetId, spec, {1234}, {1234});
        pm.setSampleMeta(0, QStringLiteral("从站忙拒收，超时重试"));
        QCOMPARE(cellData(&pm, 0, ModbusPointModel::ColDecimal).toString(), QStringLiteral("1234"));
        QVERIFY2(cellData(&pm, 0, ModbusPointModel::ColDecimal, Qt::ForegroundRole).isValid(),
                 "异常时值列须置灰（前景色）");
        ModbusPointModel clean;
        clean.setTarget(targetId, spec, {}, {7});
        clean.setSampleMeta(0, QString());
        QVERIFY(!cellData(&clean, 0, ModbusPointModel::ColDecimal, Qt::ForegroundRole).isValid());
    }

    // ── 规格 §5.5 宽容限性能：2000 行 reset + 单行增量不烂 ─────────
    void largePointResetAndUpdateIsFast()
    {
        PollSpec spec;
        spec.regType = QModbusDataUnit::Coils;
        spec.count = 2000;
        QVector<quint16> values(2000, 0);
        ModbusPointModel m;
        QElapsedTimer timer;
        timer.start();
        m.setTarget(QStringLiteral("dev#1"), spec, values, values);
        QVector<quint16> next = values;
        next[1999] = 1;
        m.applyUpdate(values, next);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < 500, qPrintable(QStringLiteral("2000 行 reset+单行增量耗时 %1ms").arg(elapsed)));
        QCOMPARE(cellData(&m, 1999, ModbusPointModel::ColChanged).toString(), QStringLiteral("↑"));
    }

    // ── 审查发现 1（轮1）+ 任务 4 裁决 1：搜索过滤激活时，未命中行的后端结果仍须入
    // dataStore；交付身份升级为精确 targetId（deviceId#slaveId），不再 ip 前缀归位 ──
    // 绑定约束：不可见行只是不发视图信号，store 侧必须照常归位（updatePoll/updateError
    // 与 m_wallMsByTarget 全集覆盖，按 allIndexOfTarget 走过滤前全集）
    void backendResultReachesStoreForFilteredOutRows()
    {
        QVariantMap a;
        a.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.1"));
        a.insert(QStringLiteral("port"), 502);
        QVariantMap b;
        b.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.2"));
        b.insert(QStringLiteral("port"), 502);
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.1:502"), a));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.2:502"), b));

        ModbusWidget w;
        w.setRegistry(nullptr);  // 回退模式：设备身份 = ip:port
        auto* tm = w.targetModel();
        QVERIFY(tm);
        QCOMPARE(tm->refresh(), 2);
        const QString hiddenId = QStringLiteral("10.0.0.1:502#1");
        const QString shownId = QStringLiteral("10.0.0.2:502#1");

        auto* search = w.findChild<QLineEdit*>(QStringLiteral("mbSearch"));
        QVERIFY(search);
        search->setText(QStringLiteral("10.0.0.2"));  // 过滤激活：10.0.0.1 行被隐藏
        QCOMPARE(tm->rowCount(), 1);
        QCOMPARE(tm->rowIndexOfTarget(hiddenId), -1);

        QSignalSpy spy(tm, &QAbstractItemModel::dataChanged);
        QVERIFY(spy.isValid());
        w.handleBackendResult(hiddenId, {7, 8, 9}, 42, QString());

        // store-only：被过滤的目标必须收到样本
        const auto stored = w.dataStore()->sample(hiddenId);
        QVERIFY2(stored.has_value(), "被搜索过滤隐藏的目标也必须收到读结果（store 更新不受过滤影响）");
        QCOMPARE(stored->values.size(), 3);
        QCOMPARE(stored->values.at(0), quint16(7));
        QCOMPARE(stored->values.at(2), quint16(9));
        QCOMPARE(stored->elapsedMs, qint64(42));
        QVERIFY(stored->ok);
        // 隐藏行不得有视图信号（只有可见行才发 dataChanged）
        QCOMPARE(spy.count(), 0);
        // 可见行未被本次回调误触及
        QVERIFY(!w.dataStore()->sample(shownId).has_value());

        // 错误样本同样不受过滤影响（updateError 保留末值）；targetId 精确归位不误伤他行
        w.handleBackendResult(hiddenId, {}, 55, QStringLiteral("超时重试"));
        const auto errored = w.dataStore()->sample(hiddenId);
        QVERIFY(errored.has_value());
        QCOMPARE(errored->error, QStringLiteral("超时重试"));
        QCOMPARE(errored->values.size(), 3);
        QCOMPARE(errored->values.at(0), quint16(7));

        // m_wallMsByTarget 归位：清过滤后选中该行，UpdatedAt 取自隐藏期间的真实时钟
        search->clear();
        QCOMPARE(tm->rowCount(), 2);
        const int hiddenRow = tm->rowIndexOfTarget(hiddenId);
        QVERIFY2(hiddenRow >= 0, "清除过滤后隐藏行须回到视图");
        auto* tv = tableByObjectName(&w, "mbTargetTable");
        QVERIFY(tv);
        tv->selectRow(hiddenRow);
        auto* pm = w.pointModel();
        QVERIFY(pm);
        QCOMPARE(pm->targetId(), hiddenId);
        const QString updated = cellData(pm, 0, ModbusPointModel::ColUpdatedAt).toString();
        QVERIFY2(updated != QStringLiteral("—") && !updated.isEmpty(),
                 qPrintable(QStringLiteral("隐藏期间的结果须记录真实时钟，UpdatedAt 实际为 '%1'")
                                .arg(updated)));
        w.hide();
    }

    // ── 审查发现 2（轮1）：可视区跟踪——不可见行不发 dataChanged、滚入补发 ──
    void invisibleRowDefersDataChangedUntilScrolledIn()
    {
        PollSpec spec;
        spec.count = 3;
        ModbusPointModel m;
        m.setTarget(QStringLiteral("dev#1"), spec, {1, 1, 1}, {1, 1, 1});
        m.setVisibleRange(0, 0);  // 仅首行在视口内

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(spy.isValid());
        m.applyUpdate({1, 1, 1}, {1, 1, 2});  // 变化落在不可见末行
        QCOMPARE(spy.count(), 0);             // 虚拟化：不可见行不发视图信号
        // 模型数据本身照常更新（可视区只影响信号，不影响数据）
        QCOMPARE(cellData(&m, 2, ModbusPointModel::ColDecimal).toString(), QStringLiteral("2"));
        QCOMPARE(cellData(&m, 2, ModbusPointModel::ColChanged).toString(), QStringLiteral("↑"));

        m.setVisibleRange(0, 1);  // 滚入但未覆盖脏行 → 仍不补发
        QCOMPARE(spy.count(), 0);

        m.setVisibleRange(0, 2);  // 脏行滚入 → 补发
        QCOMPARE(spy.count(), 1);
        const QModelIndex tl = spy.at(0).at(0).toModelIndex();
        const QModelIndex br = spy.at(0).at(1).toModelIndex();
        QVERIFY2(tl.isValid() && br.isValid() && tl.row() == 2 && br.row() == 2,
                 "滚入补发的 dataChanged 必须覆盖该行");
        QCOMPARE(tl.column(), int(ModbusPointModel::ColDecimal));
        QCOMPARE(br.column(), int(ModbusPointModel::ColChanged));

        m.setVisibleRange(2, 4);  // 已补发行不得重复发（dirty 已清）
        QCOMPARE(spy.count(), 1);

        // 可视区内行变化 → 立即发信号（虚拟化不得吞掉可见更新）
        ModbusPointModel visible;
        visible.setTarget(QStringLiteral("dev#1"), spec, {1, 1, 1}, {1, 1, 1});
        visible.setVisibleRange(0, 2);
        QSignalSpy visibleSpy(&visible, &QAbstractItemModel::dataChanged);
        QVERIFY(visibleSpy.isValid());
        visible.applyUpdate({1, 1, 1}, {5, 1, 1});
        QCOMPARE(visibleSpy.count(), 1);
        QCOMPARE(visibleSpy.at(0).at(0).toModelIndex().row(), 0);
        QCOMPARE(cellData(&visible, 0, ModbusPointModel::ColDecimal).toString(),
                 QStringLiteral("5"));
    }

    // ── 裁决 3（任务 4）：setSampleMeta 收敛为可视区纪律——只对可视行发信号、滚入补发 ──
    void sampleMetaSignalsOnlyVisibleRowsUntilScrolledIn()
    {
        PollSpec spec;
        spec.count = 3;
        ModbusPointModel m;
        m.setTarget(QStringLiteral("dev#1"), spec, {1, 1, 1}, {1, 1, 1});
        m.setVisibleRange(0, 0);   // 仅首行在视口内

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(spy.isValid());
        m.setSampleMeta(1700000000000LL, QStringLiteral("通讯异常"));
        QCOMPARE(spy.count(), 1);  // 旧实现对全表发信号，裁决 3 后收敛到可视行
        QCOMPARE(spy.at(0).at(0).toModelIndex().row(), 0);
        QCOMPARE(spy.at(0).at(1).toModelIndex().row(), 0);
        // 越界行数据照常就位（值可读），只是不发视图信号
        QVERIFY2(cellData(&m, 2, ModbusPointModel::ColUpdatedAt).toString()
                     != QStringLiteral("—"),
                 "不可见行的 UpdatedAt 数据也应立即就位");

        m.setVisibleRange(0, 2);   // meta 脏行滚入 → 一段补发
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toModelIndex().row(), 1);
        QCOMPARE(spy.at(1).at(1).toModelIndex().row(), 2);
    }

    // ── 裁决 5（任务 4）：sample error 非空 + 调度器退避态 → 清单"退避黄"（既有 token）──
    void backoffStatusShowsYellow()
    {
        QVariantMap a;
        a.insert(QStringLiteral("ip"), QStringLiteral("10.0.0.1"));
        a.insert(QStringLiteral("port"), 502);
        QVERIFY(ConfigStore::instance().save(QStringLiteral("device.list"),
                                             QStringLiteral("10.0.0.1:502"), a));
        const QString targetId = QStringLiteral("10.0.0.1:502#1");
        ModbusTargetStore store;
        ModbusDataStore data;
        data.updatePoll(targetId, {1}, 10);
        data.updateError(targetId, QStringLiteral("通讯错误: 从站不可达"), 20);

        ModbusTargetListModel m(&store, &data);
        bool inBackoff = true;
        m.setBackoffProbe([&inBackoff, targetId](const QString& id) {
            return id == targetId && inBackoff;
        });
        QCOMPARE(m.refresh(), 1);
        QCOMPARE(cellData(&m, 0, ModbusTargetListModel::ColStatus).toString(),
                 QStringLiteral("退避"));
        // 复用既有琴色 token #F0A030（"琴色是动词"信号态专用，双主题同值，不新增色值）
        QCOMPARE(cellData(&m, 0, ModbusTargetListModel::ColStatus, Qt::ForegroundRole)
                     .value<QColor>(), QColor(QStringLiteral("#F0A030")));

        inBackoff = false;   // 非退避的同一异常回落红（任务 3 语义保持）
        m.syncSamples();
        QCOMPARE(cellData(&m, 0, ModbusTargetListModel::ColStatus).toString(),
                 QStringLiteral("异常"));
        QVERIFY(cellData(&m, 0, ModbusTargetListModel::ColStatus, Qt::ForegroundRole)
                    .value<QColor>() != QColor(QStringLiteral("#F0A030")));
    }

    // ── 裁决 6（任务 4）：删除设备全部目标 = 存空 targets 记录；清单不合成默认行复活 ──
    void emptyTargetRecordShowsNoRows()
    {
        DeviceRegistry registry;
        QVERIFY(registry.load());
        QVERIFY(registry.save(makeProfile(QStringLiteral("dev-plc-e"), QStringLiteral("PLC-E"),
                                          QStringLiteral("modbus"), QStringLiteral("10.0.0.5"), 502)));
        ModbusTargetStore store;
        DeviceTargets filled;
        filled.deviceId = "dev-plc-e";
        SlaveTarget t1;
        t1.slaveId = 1;
        filled.targets.push_back(t1);
        QVERIFY(store.save(filled));
        // 删除全部目标的落库形态 = save 空记录（裁决 6，非 remove()——load 不回退旧行）
        DeviceTargets emptied;
        emptied.deviceId = "dev-plc-e";
        QVERIFY(store.save(emptied));

        ModbusDataStore data;
        ModbusTargetListModel m(&store, &data);
        m.setRegistry(&registry);          // 注入即触发 refresh
        QCOMPARE(m.rowCount(), 0);         // 空记录不合成"默认从站 1"展示行
    }
};

QTEST_MAIN(TstModbusPage)
#include "tst_modbus_page.moc"
