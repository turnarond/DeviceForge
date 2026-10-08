// v2.11 Task 3：TaskTemplateStore 模板数据模型与持久化测试
// 覆盖：必填字段校验、有序步骤 round-trip、版本化（保存/升级/拒绝降级）、
// 克隆为新模板、未知未来字段前向兼容保留，
// 以及密码/私钥类敏感字段的拒绝（不落盘）与持久化记录不含任何凭据秘密值。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QVariantList>

#include "config/ConfigStore.h"
#include "task/TaskTemplateStore.h"
#include "task/TaskTemplateTypes.h"

namespace {

TaskStep makeStep(TaskStepType type, const QVariantMap& parameters = {})
{
    TaskStep step;
    step.type = type;
    step.parameters = parameters;
    return step;
}

// 四步有序模板：部署 → 命令 → 重启 → 恢复检查
TaskTemplate makeTemplate(const QString& name)
{
    QVariantMap deploy;
    deploy.insert(QStringLiteral("remotePath"), QStringLiteral("/opt/app"));
    deploy.insert(QStringLiteral("cleanFirst"), false);

    QVariantList commands;
    commands << QStringLiteral("systemctl stop app") << QStringLiteral("chmod +x /opt/app/run.sh");
    QVariantMap runCommands;
    runCommands.insert(QStringLiteral("commands"), commands);
    runCommands.insert(QStringLiteral("timeoutMs"), 5000);

    QVariantMap reboot;
    reboot.insert(QStringLiteral("protocol"), QStringLiteral("telnet"));
    reboot.insert(QStringLiteral("waitRecover"), true);

    QVariantMap recover;
    recover.insert(QStringLiteral("checkCommand"), QStringLiteral("uptime"));
    recover.insert(QStringLiteral("retryLimit"), 3);

    TaskTemplate tmpl;
    tmpl.name = name.toStdString();
    tmpl.description = (QStringLiteral("描述-") + name).toStdString();
    tmpl.version = 1;
    tmpl.deviceIds = {std::string("device-aa"), std::string("device-bb")};
    tmpl.tagSelectors = {QStringLiteral("产线A").toStdString()};
    tmpl.steps = {
        makeStep(TaskStepType::DeployFiles, deploy),
        makeStep(TaskStepType::RunCommands, runCommands),
        makeStep(TaskStepType::Reboot, reboot),
        makeStep(TaskStepType::Recover, recover),
    };
    return tmpl;
}

} // namespace

class TstTaskTemplateStore : public QObject
{
    Q_OBJECT

private:
    QString m_dbPath;
    QString m_workDir;

    static QVariantMap minimalRecord(const QString& templateId, const QString& name)
    {
        QVariantMap step;
        step.insert(QStringLiteral("type"), QStringLiteral("deploy_files"));
        step.insert(QStringLiteral("parameters"), QVariantMap{});
        QVariantMap record;
        record.insert(QStringLiteral("templateId"), templateId);
        record.insert(QStringLiteral("name"), name);
        record.insert(QStringLiteral("version"), 1);
        record.insert(QStringLiteral("steps"), QVariantList{step});
        return record;
    }

private slots:
    void initTestCase()
    {
        // 进程专属临时目录，避免 CI 残留/并发污染（同 tst_device_registry_store）
        m_workDir = QDir::temp().filePath(
            QStringLiteral("tst_task_template_store_%1")
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
        ConfigStore::instance().close();
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + QStringLiteral("-wal"));
        QFile::remove(m_dbPath + QStringLiteral("-shm"));
        QVERIFY2(ConfigStore::instance().open(m_dbPath),
                 qPrintable(QStringLiteral("open failed path=%1").arg(m_dbPath)));
    }

    void cleanup()
    {
        ConfigStore::instance().close();
    }

    // 必填字段：名称、版本、至少一个有序步骤
    void validateRejectsMissingRequiredFields()
    {
        TaskTemplate missingName = makeTemplate(QStringLiteral("模板A"));
        missingName.name.clear();
        auto result = validateTaskTemplate(missingName);
        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("名称")));

        TaskTemplate zeroVersion = makeTemplate(QStringLiteral("模板A"));
        zeroVersion.version = 0;
        result = validateTaskTemplate(zeroVersion);
        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("版本")));

        TaskTemplate noSteps = makeTemplate(QStringLiteral("模板A"));
        noSteps.steps.clear();
        result = validateTaskTemplate(noSteps);
        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("步骤")));

        const auto ok = validateTaskTemplate(makeTemplate(QStringLiteral("模板A")));
        QVERIFY(ok.valid);
        QVERIFY(ok.error.isEmpty());
    }

    // 拒绝密码/私钥类字段：顶层、嵌套 map、嵌套 list、未知未来字段
    void validateRejectsCredentialFields()
    {
        const QStringList needles{QStringLiteral("password"), QStringLiteral("Password"),
                                  QStringLiteral("PASSWORD"), QStringLiteral("passwd"),
                                  QStringLiteral("privkey"), QStringLiteral("private_key"),
                                  QStringLiteral("privateKey"), QStringLiteral("key_material"),
                                  QStringLiteral("token"), QStringLiteral("TOKEN"),
                                  QStringLiteral("secret"), QStringLiteral("clientSecret")};
        for (const auto& key : needles) {
            TaskTemplate tmpl = makeTemplate(QStringLiteral("模板A"));
            tmpl.steps.front().parameters.insert(key, QStringLiteral("hunter2"));
            const auto result = validateTaskTemplate(tmpl);
            QVERIFY2(!result.valid, qPrintable(QStringLiteral("未拒绝字段: %1").arg(key)));
            QVERIFY(result.error.contains(QStringLiteral("凭据")));
        }

        // 嵌套 map 内的敏感字段
        QVariantMap auth;
        auth.insert(QStringLiteral("password"), QStringLiteral("s3cret"));
        TaskTemplate nested = makeTemplate(QStringLiteral("模板A"));
        nested.steps.at(1).parameters.insert(QStringLiteral("auth"), auth);
        QVERIFY(!validateTaskTemplate(nested).valid);

        // 嵌套 list 元素 map 内的敏感字段
        QVariantMap keyItem;
        keyItem.insert(QStringLiteral("private_key"), QStringLiteral("x"));
        QVariantList items;
        items << keyItem;
        TaskTemplate inList = makeTemplate(QStringLiteral("模板A"));
        inList.steps.at(1).parameters.insert(QStringLiteral("items"), items);
        QVERIFY(!validateTaskTemplate(inList).valid);

        // 未知未来字段中的敏感字段同样拒绝（前向兼容不得绕过凭据拒绝规则）
        TaskTemplate unknown = makeTemplate(QStringLiteral("模板A"));
        unknown.unknownFields.insert(QStringLiteral("sessionToken"), QStringLiteral("abc"));
        QVERIFY(!validateTaskTemplate(unknown).valid);

        // 凭据引用（非秘密值）必须放行
        TaskTemplate allowed = makeTemplate(QStringLiteral("模板A"));
        allowed.steps.front().parameters.insert(QStringLiteral("credentialRef"),
                                                QStringLiteral("ftp-main"));
        QVERIFY(validateTaskTemplate(allowed).valid);
    }

    // 无效模板：save 返回 false 且不落盘
    void saveRejectsInvalidAndPersistsNothing()
    {
        TaskTemplateStore store;

        TaskTemplate secret = makeTemplate(QStringLiteral("带密码模板"));
        secret.templateId = "tpl-bad";
        secret.steps.front().parameters.insert(QStringLiteral("password"),
                                               QStringLiteral("hunter2"));
        QVERIFY(!store.save(secret));
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("task.template"),
                                               QStringLiteral("tpl-bad")));

        TaskTemplate noname = makeTemplate(QStringLiteral("无名称模板"));
        noname.templateId = "tpl-noname";
        noname.name.clear();
        QVERIFY(!store.save(noname));
        QVERIFY(!ConfigStore::instance().exists(QStringLiteral("task.template"),
                                               QStringLiteral("tpl-noname")));

        QVERIFY(store.list().empty());
    }

    // 有序步骤与全字段 round-trip；空 templateId 保存时自动生成唯一 ID
    void orderedStepsRoundTrip()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl = makeTemplate(QStringLiteral("部署模板"));
        tmpl.version = 3;
        QVERIFY(store.save(tmpl));

        const auto listed = store.list();
        QCOMPARE(listed.size(), size_t(1));
        const std::string id = listed.front().templateId;
        QVERIFY(!id.empty());
        QVERIFY(QString::fromStdString(id).startsWith(QStringLiteral("template-")));

        TaskTemplateStore fresh;
        const auto loaded = fresh.load(id);
        QVERIFY(loaded.has_value());
        QCOMPARE(QString::fromStdString(loaded->templateId), QString::fromStdString(id));
        QCOMPARE(QString::fromStdString(loaded->name), QStringLiteral("部署模板"));
        QCOMPARE(QString::fromStdString(loaded->description), QStringLiteral("描述-部署模板"));
        QCOMPARE(loaded->version, 3);
        QCOMPARE(loaded->deviceIds.size(), size_t(2));
        QCOMPARE(QString::fromStdString(loaded->deviceIds.at(0)), QStringLiteral("device-aa"));
        QCOMPARE(loaded->tagSelectors.size(), size_t(1));
        QCOMPARE(QString::fromStdString(loaded->tagSelectors.at(0)), QStringLiteral("产线A"));

        // 步骤顺序与类型逐位一致
        QCOMPARE(loaded->steps.size(), size_t(4));
        QCOMPARE(int(loaded->steps.at(0).type), int(TaskStepType::DeployFiles));
        QCOMPARE(int(loaded->steps.at(1).type), int(TaskStepType::RunCommands));
        QCOMPARE(int(loaded->steps.at(2).type), int(TaskStepType::Reboot));
        QCOMPARE(int(loaded->steps.at(3).type), int(TaskStepType::Recover));

        // 步骤参数完整保留
        QCOMPARE(loaded->steps.at(0).parameters.value(QStringLiteral("remotePath")).toString(),
                 QStringLiteral("/opt/app"));
        QCOMPARE(loaded->steps.at(0).parameters.value(QStringLiteral("cleanFirst")).toBool(), false);
        const QVariantList commands =
            loaded->steps.at(1).parameters.value(QStringLiteral("commands")).toList();
        QCOMPARE(commands.size(), 2);
        QCOMPARE(commands.at(0).toString(), QStringLiteral("systemctl stop app"));
        QCOMPARE(commands.at(1).toString(), QStringLiteral("chmod +x /opt/app/run.sh"));
        QCOMPARE(loaded->steps.at(1).parameters.value(QStringLiteral("timeoutMs")).toInt(), 5000);
        QCOMPARE(loaded->steps.at(3).parameters.value(QStringLiteral("checkCommand")).toString(),
                 QStringLiteral("uptime"));
    }

    // 版本化：升级允许、降级拒绝、同版本可重复保存
    void versioningGuardsDowngrade()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl = makeTemplate(QStringLiteral("版本模板"));
        tmpl.templateId = "tpl-versioned";
        tmpl.version = 1;
        QVERIFY(store.save(tmpl));

        tmpl.version = 2;
        QVERIFY(store.save(tmpl));
        const auto loaded = store.load("tpl-versioned");
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->version, 2);

        tmpl.version = 1;  // 降级写入必须拒绝
        QVERIFY(!store.save(tmpl));
        QCOMPARE(store.load("tpl-versioned")->version, 2);

        tmpl.version = 2;  // 同版本重复保存（幂等更新）允许
        QVERIFY(store.save(tmpl));
        QCOMPARE(store.load("tpl-versioned")->version, 2);
    }

    // 克隆为新模板：新 ID、版本归 1、内容深拷贝、源模板不受影响
    void cloneCreatesNewTemplate()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl = makeTemplate(QStringLiteral("源模板"));
        tmpl.templateId = "tpl-src";
        tmpl.version = 5;
        QVERIFY(store.save(tmpl));

        const auto cloned = store.clone("tpl-src", std::string(u8"巡检副本"));
        QVERIFY(cloned.has_value());
        QVERIFY(cloned->templateId != std::string("tpl-src"));
        QVERIFY(!cloned->templateId.empty());
        QCOMPARE(QString::fromStdString(cloned->name), QStringLiteral("巡检副本"));
        QCOMPARE(cloned->version, 1);
        QCOMPARE(cloned->steps.size(), size_t(4));
        QCOMPARE(cloned->deviceIds.size(), size_t(2));
        QCOMPARE(int(cloned->steps.at(3).type), int(TaskStepType::Recover));

        // 源模板原样保留
        const auto source = store.load("tpl-src");
        QVERIFY(source.has_value());
        QCOMPARE(QString::fromStdString(source->name), QStringLiteral("源模板"));
        QCOMPARE(source->version, 5);

        // 克隆体已持久化，全新 store 可加载；库内共两条
        TaskTemplateStore fresh;
        QVERIFY(fresh.load(cloned->templateId).has_value());
        QCOMPARE(fresh.list().size(), size_t(2));

        // 源不存在或新名称为空：克隆失败
        QVERIFY(!store.clone("tpl-missing", std::string(u8"副本")).has_value());
        QVERIFY(!store.clone("tpl-src", std::string("  ")).has_value());
        QCOMPARE(fresh.list().size(), size_t(2));
    }

    // 持久化记录不含任何凭据秘密值（合法模板保存后原文扫描）
    void persistedRecordsCarryNoSecretValues()
    {
        TaskTemplateStore store;
        TaskTemplate tmpl = makeTemplate(QStringLiteral("干净模板"));
        tmpl.templateId = "tpl-clean";
        tmpl.steps.front().parameters.insert(QStringLiteral("credentialRef"),
                                             QStringLiteral("ftp-main"));
        QVERIFY(store.save(tmpl));

        const QVariantMap raw = ConfigStore::instance().load(QStringLiteral("task.template"),
                                                             QStringLiteral("tpl-clean"));
        QVERIFY(!raw.isEmpty());
        const QString json = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(raw)).toJson(QJsonDocument::Compact))
            .toLower();
        const QStringList needles{QStringLiteral("password"), QStringLiteral("passwd"),
                                  QStringLiteral("privkey"), QStringLiteral("private_key"),
                                  QStringLiteral("key_material"), QStringLiteral("token"),
                                  QStringLiteral("secret")};
        for (const auto& needle : needles)
            QVERIFY2(!json.contains(needle), qPrintable(QStringLiteral("记录含敏感字段: %1").arg(needle)));
        QVERIFY(json.contains(QStringLiteral("credentialref")));
    }

    // 未知未来字段（顶层与步骤参数）load/save round-trip 原样保留
    void unknownFutureFieldsSurviveRoundTrip()
    {
        QVariantMap step;
        step.insert(QStringLiteral("type"), QStringLiteral("deploy_files"));
        QVariantMap parameters;
        parameters.insert(QStringLiteral("remotePath"), QStringLiteral("/tmp"));
        parameters.insert(QStringLiteral("futureFlag"), true);
        step.insert(QStringLiteral("parameters"), parameters);

        QVariantMap policy;
        policy.insert(QStringLiteral("retryLimit"), 3);

        QVariantMap record = minimalRecord(QStringLiteral("tpl-legacy"), QStringLiteral("旧模板"));
        record.insert(QStringLiteral("version"), 2);
        record.insert(QStringLiteral("steps"), QVariantList{step});
        record.insert(QStringLiteral("executionPolicy"), policy);
        QVERIFY(ConfigStore::instance().save(QStringLiteral("task.template"),
                                             QStringLiteral("tpl-legacy"), record));

        TaskTemplateStore store;
        const auto loaded = store.load("tpl-legacy");
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->version, 2);
        QCOMPARE(loaded->unknownFields.value(QStringLiteral("executionPolicy"))
                     .toMap()
                     .value(QStringLiteral("retryLimit"))
                     .toInt(),
                 3);
        QCOMPARE(loaded->steps.at(0).parameters.value(QStringLiteral("futureFlag")).toBool(), true);

        // 原样再保存：未来字段仍在库中
        QVERIFY(store.save(*loaded));
        const QVariantMap raw = ConfigStore::instance().load(QStringLiteral("task.template"),
                                                             QStringLiteral("tpl-legacy"));
        QCOMPARE(raw.value(QStringLiteral("executionPolicy")).toMap()
                     .value(QStringLiteral("retryLimit"))
                     .toInt(),
                 3);
        QCOMPARE(raw.value(QStringLiteral("steps")).toList().at(0).toMap()
                     .value(QStringLiteral("parameters"))
                     .toMap()
                     .value(QStringLiteral("futureFlag"))
                     .toBool(),
                 true);
    }

    // 损坏或含敏感字段的记录：load 拒绝、list 跳过且不阻断有效记录
    void loadRejectsSensitiveAndMalformedRecords()
    {
        // 未知字段中含敏感键
        QVariantMap secretUnknown = minimalRecord(QStringLiteral("tpl-sec1"), QStringLiteral("模板1"));
        secretUnknown.insert(QStringLiteral("sessionToken"), QStringLiteral("abc"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("task.template"),
                                             QStringLiteral("tpl-sec1"), secretUnknown));

        // 步骤参数深层含敏感键
        QVariantMap passwdItem;
        passwdItem.insert(QStringLiteral("passwd"), QStringLiteral("x"));
        QVariantMap authItem;
        authItem.insert(QStringLiteral("auth"), passwdItem);
        QVariantMap step;
        step.insert(QStringLiteral("type"), QStringLiteral("run_commands"));
        step.insert(QStringLiteral("parameters"), authItem);
        QVariantMap secretStep = minimalRecord(QStringLiteral("tpl-sec2"), QStringLiteral("模板2"));
        secretStep.insert(QStringLiteral("steps"), QVariantList{step});
        QVERIFY(ConfigStore::instance().save(QStringLiteral("task.template"),
                                             QStringLiteral("tpl-sec2"), secretStep));

        // 缺名称
        QVariantMap noName;
        noName.insert(QStringLiteral("templateId"), QStringLiteral("tpl-noname"));
        noName.insert(QStringLiteral("steps"), QVariantList{});
        QVERIFY(ConfigStore::instance().save(QStringLiteral("task.template"),
                                             QStringLiteral("tpl-noname"), noName));

        // 未知步骤类型（未来版本写入，当前代码无法安全解释）
        QVariantMap badTypeStep;
        badTypeStep.insert(QStringLiteral("type"), QStringLiteral("teleport"));
        QVariantMap unknownStepType =
            minimalRecord(QStringLiteral("tpl-teleport"), QStringLiteral("模板3"));
        unknownStepType.insert(QStringLiteral("steps"), QVariantList{badTypeStep});
        QVERIFY(ConfigStore::instance().save(QStringLiteral("task.template"),
                                             QStringLiteral("tpl-teleport"), unknownStepType));

        // steps 不是列表
        QVariantMap badSteps = minimalRecord(QStringLiteral("tpl-badsteps"), QStringLiteral("模板4"));
        badSteps.insert(QStringLiteral("steps"), QStringLiteral("oops"));
        QVERIFY(ConfigStore::instance().save(QStringLiteral("task.template"),
                                             QStringLiteral("tpl-badsteps"), badSteps));

        // 一条有效记录
        TaskTemplateStore store;
        TaskTemplate good = makeTemplate(QStringLiteral("有效模板"));
        good.templateId = "tpl-good";
        QVERIFY(store.save(good));

        QVERIFY(!store.load("tpl-sec1").has_value());
        QVERIFY(!store.load("tpl-sec2").has_value());
        QVERIFY(!store.load("tpl-noname").has_value());
        QVERIFY(!store.load("tpl-teleport").has_value());
        QVERIFY(!store.load("tpl-badsteps").has_value());
        QVERIFY(!store.load("tpl-missing-anywhere").has_value());

        const auto listed = store.list();
        QCOMPARE(listed.size(), size_t(1));
        QCOMPARE(QString::fromStdString(listed.front().templateId), QStringLiteral("tpl-good"));
    }
};

QTEST_MAIN(TstTaskTemplateStore)
#include "tst_task_template_store.moc"
