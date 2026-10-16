#include <QtTest>
#include "task/TaskTemplateCodec.h"

class TstTaskTemplateCodec : public QObject {
    Q_OBJECT
private slots:
    // 行编解码往返（外提重构守卫：与 store 行为同源）
    void rowRoundTrip_preservesAllFields()
    {
        TaskTemplate tmpl;
        tmpl.templateId = "template-abc";
        tmpl.name = "A线部署";
        tmpl.description = "含悬空引用";
        tmpl.version = 2;
        tmpl.deviceIds = {"device-deadbeef"};          // 不校验存在性
        tmpl.tagSelectors = {"A线"};
        TaskStep step;
        step.type = TaskStepType::RunCommands;
        step.parameters.insert(QStringLiteral("commands"), QStringList{QStringLiteral("reboot")});
        tmpl.steps.push_back(step);
        tmpl.unknownFields.insert(QStringLiteral("future_note"), QStringLiteral("保留我"));

        const auto decoded = decodeTaskTemplate(encodeTaskTemplate(tmpl), nullptr);
        QVERIFY(decoded.has_value());
        QCOMPARE(QString::fromStdString(decoded->templateId), QStringLiteral("template-abc"));
        QCOMPARE(QString::fromStdString(decoded->name), QStringLiteral("A线部署"));
        QCOMPARE(decoded->version, 2);
        QCOMPARE((int)decoded->deviceIds.size(), 1);
        QCOMPARE((int)decoded->steps.size(), 1);
        QCOMPARE(decoded->steps.front().type, TaskStepType::RunCommands);
        QVERIFY(decoded->unknownFields.contains(QStringLiteral("future_note")));
    }

    // ── Task 2：文件格式契约（encodeTemplateFile / decodeTemplateFile）──

    void fileRoundTrip_dropsTemplateId()
    {
        TaskTemplate tmpl;
        tmpl.templateId = "template-local-only";
        tmpl.name = "巡检";
        TaskStep step; step.type = TaskStepType::Reboot; tmpl.steps.push_back(step);
        const QByteArray json = encodeTemplateFile(tmpl);
        QVERIFY(!json.contains("template-local-only"));      // ID 不进文件
        QVERIFY(json.contains("deviceforge.task-template")); // 魔数
        const auto back = decodeTemplateFile(json, nullptr);
        QVERIFY(back.has_value());
        QVERIFY(back->templateId.empty());                    // 解码恒空 ID
        QCOMPARE(QString::fromStdString(back->name), QStringLiteral("巡检"));
    }
    void fileKeepsDanglingDeviceIdsAndUnknownFields()
    {
        TaskTemplate tmpl; tmpl.name = "n";
        tmpl.deviceIds = {"device-notexist"};
        tmpl.unknownFields.insert(QStringLiteral("x_future"), 1);
        TaskStep step; step.type = TaskStepType::Recover; tmpl.steps.push_back(step);
        const auto back = decodeTemplateFile(encodeTemplateFile(tmpl), nullptr);
        QVERIFY(back.has_value());
        QCOMPARE((int)back->deviceIds.size(), 1);             // 悬空保留不剥离
        QVERIFY(back->unknownFields.contains(QStringLiteral("x_future")));
    }
    void fileRejectsBadInputs_dataDriven()
    {
        // 非法 JSON
        QString reason;
        QVERIFY(!decodeTemplateFile("not json {{", &reason));
        QVERIFY(!reason.isEmpty());
        // 魔数不符
        QVERIFY(!decodeTemplateFile(R"({"format":"other","formatVersion":1,"template":{}})", &reason));
        QVERIFY(reason.contains(QStringLiteral("不是 DeviceForge 模板文件")));
        // formatVersion 过高 → 升级文案
        QVERIFY(!decodeTemplateFile(
            R"({"format":"deviceforge.task-template","formatVersion":2,"template":{}})", &reason));
        QVERIFY(reason.contains(QStringLiteral("升级")));
        // formatVersion 缺失
        QVERIFY(!decodeTemplateFile(R"({"format":"deviceforge.task-template","template":{}})", &reason));
    }
    void fileRejectsSensitiveKeys()
    {
        // 按 Task 2 裁定：steps 填一个合法步骤，确保失败原因落在敏感拒收而非"缺少有序步骤"
        const QByteArray doc = R"({"format":"deviceforge.task-template","formatVersion":1,
            "template":{"name":"n","steps":[{"type":"reboot"}],"evil_password":"x"}})";
        QString reason;
        QVERIFY(!decodeTemplateFile(doc, &reason));
        QVERIFY(reason.contains(QStringLiteral("敏感")));
    }
};
QTEST_MAIN(TstTaskTemplateCodec)
#include "tst_task_template_codec.moc"
