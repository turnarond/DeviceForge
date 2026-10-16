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
};
QTEST_MAIN(TstTaskTemplateCodec)
#include "tst_task_template_codec.moc"
