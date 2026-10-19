#include <QtTest>
#include "tools/FtpDeployTool/DeployTemplateMap.h"
// v2.15 任务9：OnceConfig 增 expandedItems（folderexpand::ExpandedFile 清单），
// 本目标随纯头 include 引入展开类型；仍为纯头逻辑，不编产品源码。
#include "task/TaskStepValidation.h"  // parseDeployItems 契约同检（纯头，仅值类型）

class TstDeployTemplateMap : public QObject {
    Q_OBJECT
private slots:
    void deployStepCarriesShorthandParams() {
        deploymap::OnceConfig cfg;
        cfg.protocol = QStringLiteral("ftp");
        cfg.localFiles = {QStringLiteral("D:/a.txt"), QStringLiteral("D:/b.bin")};
        cfg.remoteDirectory = QStringLiteral("/upload");
        cfg.deviceIds = {"dev-1"};
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);
        QCOMPARE(t.steps.size(), std::size_t{1});
        QCOMPARE(t.steps[0].type, TaskStepType::DeployFiles);
        QCOMPARE(t.steps[0].parameters.value(QStringLiteral("protocol")).toString(),
                 QStringLiteral("ftp"));
        QCOMPARE(t.steps[0].parameters.value(QStringLiteral("files")).toStringList(),
                 cfg.localFiles);
        QCOMPARE(t.steps[0].parameters.value(QStringLiteral("remoteDirectory")).toString(),
                 QStringLiteral("/upload"));
        QCOMPARE(t.deviceIds, cfg.deviceIds);
        QVERIFY(t.templateId.empty());          // 新模板由落库层生成
        QCOMPARE(t.version, 1);
        QVERIFY(!t.name.empty());               // 缺省占位名「单次部署模板」，编辑器内用户可改
    }
    void rebootCheckedAppendsRebootStep() {
        deploymap::OnceConfig cfg;
        cfg.localFiles = {QStringLiteral("D:/a.txt")};
        cfg.remoteDirectory = QStringLiteral("/");
        cfg.rebootAfter = true;
        cfg.rebootCommand = QStringLiteral("reboot -f");
        cfg.rebootProtocol = QStringLiteral("ssh");
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);
        QCOMPARE(t.steps.size(), std::size_t{2});
        QCOMPARE(t.steps[1].type, TaskStepType::Reboot);
        QCOMPARE(t.steps[1].parameters.value(QStringLiteral("command")).toString(),
                 QStringLiteral("reboot -f"));
        QCOMPARE(t.steps[1].parameters.value(QStringLiteral("protocol")).toString(),
                 QStringLiteral("ssh"));
    }
    void rebootBlankCommandFallsBackToReboot() {
        deploymap::OnceConfig cfg;
        cfg.localFiles = {QStringLiteral("D:/a.txt")};
        cfg.remoteDirectory = QStringLiteral("/");
        cfg.rebootAfter = true;
        cfg.rebootCommand = QStringLiteral("  ");
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);
        QCOMPARE(t.steps[1].parameters.value(QStringLiteral("command")).toString(),
                 QStringLiteral("reboot"));
    }
    void emptySelectionMeansUnboundTemplate() {
        deploymap::OnceConfig cfg;   // deviceIds 空 = 未绑定（单发"全部"语义不展开，跨机不可移植）
        cfg.localFiles = {QStringLiteral("D:/a.txt")};
        cfg.remoteDirectory = QStringLiteral("/");
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);
        QVERIFY(t.deviceIds.empty());
    }
    void noRebootKeepsSingleStep() {
        deploymap::OnceConfig cfg;
        cfg.localFiles = {QStringLiteral("D:/a.txt")};
        cfg.remoteDirectory = QStringLiteral("/");
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);
        QCOMPARE(t.steps.size(), std::size_t{1});
    }

    // ── v2.15 任务9 三入口同口径（规格 §3.1）：映射层全量承载两条腿 ──

    // 混合选择（含目录）→ parameters 只产 items 全量数组：顶层文件项=扁平
    // remoteDir/文件名（rel 空）、目录镜像项=remoteDir/rel；且**无 files/
    // remoteDirectory 键**（parseDeployItems 契约：items 非空即忽略 files，
    // 两键并写会静默丢顶层文件——见规格 §3.1 第 1 条修正括注）。
    void expandedItemsCarryFullFormWithoutFilesKey() {
        deploymap::OnceConfig cfg;
        cfg.protocol = QStringLiteral("ftp");
        cfg.localFiles = {QStringLiteral("D:/top.txt"), QStringLiteral("D:/pkg")};
        cfg.remoteDirectory = QStringLiteral("/upload/");   // 尾斜杠考验规范化拼接
        cfg.expandedItems = {
            {QStringLiteral("D:/top.txt"), QString()},                       // 顶层文件 rel 空
            {QStringLiteral("D:/pkg/bin/app.conf"), QStringLiteral("pkg/bin/app.conf")},
            {QStringLiteral("D:/pkg/lib/a.so"), QStringLiteral("pkg/lib/a.so")},
        };
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);
        QCOMPARE(t.steps.size(), std::size_t{1});
        const QVariantMap params = t.steps[0].parameters;
        QVERIFY(!params.contains(QStringLiteral("files")));
        QVERIFY(!params.contains(QStringLiteral("remoteDirectory")));
        QCOMPARE(params.value(QStringLiteral("protocol")).toString(), QStringLiteral("ftp"));
        const QVariantList items = params.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].toMap().value(QStringLiteral("localPath")).toString(),
                 QStringLiteral("D:/top.txt"));
        QCOMPARE(items[0].toMap().value(QStringLiteral("remotePath")).toString(),
                 QStringLiteral("/upload/top.txt"));                  // 扁平项（顶层文件）
        QCOMPARE(items[1].toMap().value(QStringLiteral("remotePath")).toString(),
                 QStringLiteral("/upload/pkg/bin/app.conf"));         // 镜像项（尾斜杠不叠 //）
        QCOMPARE(items[2].toMap().value(QStringLiteral("remotePath")).toString(),
                 QStringLiteral("/upload/pkg/lib/a.so"));
        // 契约同检：产物必须能被引擎纯函数原样解码（三入口口径统一的下游保证）
        QVector<TransferItemRequest> parsed;
        QString error;
        QVERIFY(taskstep::parseDeployItems(params, parsed, error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(parsed.size(), 3);
        QCOMPARE(parsed[0].remotePath, QStringLiteral("/upload/top.txt"));
        QCOMPARE(parsed[2].remotePath, QStringLiteral("/upload/pkg/lib/a.so"));
    }

    // 纯文件选择（无目录，expandedItems 空）→ 产物与 v2.14 简写形态逐字段相等
    // （QVariantMap operator== 即深比较，钉死「零漂移」不变式）。
    void pureFileSelectionKeepsV214ShorthandFieldForField() {
        deploymap::OnceConfig cfg;
        cfg.protocol = QStringLiteral("ftp");
        cfg.localFiles = {QStringLiteral("D:/a.txt"), QStringLiteral("D:/b.bin")};
        cfg.remoteDirectory = QStringLiteral("/upload");
        cfg.deviceIds = {"dev-1"};
        cfg.rebootAfter = true;
        cfg.rebootCommand = QStringLiteral("reboot -f");
        cfg.rebootProtocol = QStringLiteral("ssh");
        // cfg.expandedItems 刻意留空 = Widget 无目录口径（原始清单走 files 简写）
        const TaskTemplate t = deploymap::buildTemplateFromOnceConfig(cfg);

        QVariantMap expected;   // v2.14 形态逐字段手搭（与 deployStepCarriesShorthandParams 同输入）
        expected.insert(QStringLiteral("protocol"), QStringLiteral("ftp"));
        expected.insert(QStringLiteral("files"), cfg.localFiles);
        expected.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/upload"));
        QCOMPARE(t.steps[0].parameters, expected);
        QVERIFY(!t.steps[0].parameters.contains(QStringLiteral("items")));
        QCOMPARE(t.steps.size(), std::size_t{2});   // reboot 步组装腿同逐字不动
        QCOMPARE(t.steps[1].parameters.value(QStringLiteral("command")).toString(),
                 QStringLiteral("reboot -f"));
    }
};
QTEST_APPLESS_MAIN(TstDeployTemplateMap)
#include "tst_deploy_template_map.moc"
