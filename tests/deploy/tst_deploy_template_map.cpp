#include <QtTest>
#include "tools/FtpDeployTool/DeployTemplateMap.h"

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
};
QTEST_APPLESS_MAIN(TstDeployTemplateMap)
#include "tst_deploy_template_map.moc"
