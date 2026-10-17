// tst_task_step_validation.cpp — 步骤参数级校验共享纯函数守卫测试（v2.14 #49-3）。
//
// 覆盖意图：引擎 Validating（validateDeviceSteps）与编辑器 validate() 此前两处并行
// 实现参数校验且 commands 分支文案分叉（「至少一条命令」vs「缺少命令列表」）。
// 本测试锁定共享头 taskstep::stepParamsValidationError / parseDeployItems 的
// 唯一口径：整句中文文案、trim 后判空（编辑器语义，比引擎旧裸列表判定更严）、
// deploy 简写展开与 items 规范形态、reboot 无参数级校验恒通过。
// 端点/设备存在性校验刻意不在本层（留引擎执行期，模板可跨机），故本测试不涉及。

#include <QtTest>

#include "task/TaskStepValidation.h"

class TstTaskStepValidation : public QObject {
    Q_OBJECT
private slots:
    // ── parseDeployItems：规范 items 形态 ──
    void parseItemsCanonicalFormPasses() {
        QVariantMap one;
        one.insert(QStringLiteral("localPath"), QStringLiteral("D:/a.txt"));
        one.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/app/a.txt"));
        one.insert(QStringLiteral("direction"), QStringLiteral("download"));
        one.insert(QStringLiteral("overwrite"), QStringLiteral("skip"));
        QVariantMap params;
        params.insert(QStringLiteral("items"), QVariantList{one});
        QVector<TransferItemRequest> items;
        QString error;
        QVERIFY(taskstep::parseDeployItems(params, items, error));
        QVERIFY(error.isEmpty());
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].localPath, QStringLiteral("D:/a.txt"));
        QCOMPARE(items[0].remotePath, QStringLiteral("/opt/app/a.txt"));
        QCOMPARE(items[0].direction, TransferDirection::Download);
        QCOMPARE(items[0].overwrite, OverwritePolicy::Skip);
    }
    void parseItemsBadEntryShapeRejected() {
        // 非对象条目 → 「文件部署步骤条目结构非法（应为对象）」
        QVariantMap params;
        params.insert(QStringLiteral("items"), QVariantList{QStringLiteral("not-a-map")});
        QVector<TransferItemRequest> items;
        QString error;
        QVERIFY(!taskstep::parseDeployItems(params, items, error));
        QVERIFY(error.contains(QStringLiteral("条目结构非法（应为对象）")));
        // 缺 remotePath 条目 → 「存在缺少 localPath/remotePath 的条目」
        QVariantMap missing;
        missing.insert(QStringLiteral("localPath"), QStringLiteral("D:/a.txt"));
        params.insert(QStringLiteral("items"), QVariantList{missing});
        error.clear();
        QVERIFY(!taskstep::parseDeployItems(params, items, error));
        QVERIFY(error.contains(QStringLiteral("缺少 localPath/remotePath 的条目")));
    }

    // ── parseDeployItems：files + remoteDirectory 简写展开 ──
    void parseShorthandExpandsRemotePaths() {
        QVariantMap params;
        params.insert(QStringLiteral("files"),
                      QStringList{QStringLiteral("D:/a.txt"), QStringLiteral("/local/b.bin")});
        params.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/opt/app/"));
        QVector<TransferItemRequest> items;
        QString error;
        QVERIFY(taskstep::parseDeployItems(params, items, error));
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].remotePath, QStringLiteral("/opt/app/a.txt"));   // 去尾斜杠拼接
        QCOMPARE(items[1].remotePath, QStringLiteral("/opt/app/b.bin"));
        QCOMPARE(items[0].localPath, QStringLiteral("D:/a.txt"));
        // 根目录特例：base="/" → "/"+文件名（换新 vector，parse 只追加不清空）
        params.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/"));
        QVector<TransferItemRequest> rootItems;
        QVERIFY(taskstep::parseDeployItems(params, rootItems, error));
        QCOMPARE(rootItems[0].remotePath, QStringLiteral("/a.txt"));
    }
    void parseShorthandEmptyPathRejected() {
        QVariantMap params;
        params.insert(QStringLiteral("files"),
                      QStringList{QStringLiteral("D:/a.txt"), QStringLiteral("   ")});
        params.insert(QStringLiteral("remoteDirectory"), QStringLiteral("/opt/app"));
        QVector<TransferItemRequest> items;
        QString error;
        QVERIFY(!taskstep::parseDeployItems(params, items, error));
        QVERIFY(error.contains(QStringLiteral("文件部署清单存在空路径")));
    }
    void parseMissingBothManifestsRejected() {
        QVariantMap params;   // 既无 items 也无 files+remoteDirectory
        QVector<TransferItemRequest> items;
        QString error;
        QVERIFY(!taskstep::parseDeployItems(params, items, error));
        QCOMPARE(error,
                 QStringLiteral("文件部署步骤缺少文件清单（items 数组，或 files + remoteDirectory）"));
    }

    // ── stepParamsValidationError：整句文案单源 ──
    void deployStepFullSentenceWording() {
        TaskStep step;
        step.type = TaskStepType::DeployFiles;   // 空参数 → 缺清单
        QCOMPARE(taskstep::stepParamsValidationError(step, 0),
                 QStringLiteral("步骤 1（文件部署）参数非法："
                                "文件部署步骤缺少文件清单（items 数组，或 files + remoteDirectory）"));
    }
    void deployStepValidPasses() {
        TaskStep step;
        step.type = TaskStepType::DeployFiles;
        QVariantMap one;
        one.insert(QStringLiteral("localPath"), QStringLiteral("D:/a.txt"));
        one.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/a.txt"));
        step.parameters.insert(QStringLiteral("items"), QVariantList{one});
        QVERIFY(taskstep::stepParamsValidationError(step, 2).isEmpty());
    }
    void commandsEmptyAndBlankRejected() {
        // 空列表与纯空白行都判无效（编辑器 trim 语义，引擎随之收紧）
        TaskStep step;
        step.type = TaskStepType::RunCommands;
        QCOMPARE(taskstep::stepParamsValidationError(step, 0),
                 QStringLiteral("步骤 1（批量命令）缺少命令列表"));
        step.parameters.insert(QStringLiteral("commands"), QStringList{QStringLiteral("  ")});
        QCOMPARE(taskstep::stepParamsValidationError(step, 0),
                 QStringLiteral("步骤 1（批量命令）缺少命令列表"));
        step.parameters.insert(QStringLiteral("commands"), QStringList{QStringLiteral("ls -l")});
        QVERIFY(taskstep::stepParamsValidationError(step, 4).isEmpty());
    }
    void recoverSharesWordingWithChineseName() {
        TaskStep step;
        step.type = TaskStepType::Recover;
        step.parameters.insert(QStringLiteral("commands"), QStringList{});
        QCOMPARE(taskstep::stepParamsValidationError(step, 1),
                 QStringLiteral("步骤 2（恢复检查）缺少命令列表"));
    }
    void rebootHasNoParamLevelCheck() {
        // reboot 无参数级校验：端点检查留引擎执行期，此处恒通过
        TaskStep step;
        step.type = TaskStepType::Reboot;
        QVERIFY(taskstep::stepParamsValidationError(step, 0).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstTaskStepValidation)
#include "tst_task_step_validation.moc"
