// v2.13 #54 Task 2：任务模板编辑器对话框骨架实现。
// 设计裁决（控制器拍板，后续 Task 依赖）：
// · teProtocol 为**全类型共享**的协议过滤下拉，置于 QStackedWidget 之外（栈上方），
//   四种步骤类型读写同一 parameters["protocol"] 键；T3/T4 不再各自建 teProtocol。
// · 参数栈页序：0=deploy（T3 落地前空白）/1=commands（T4）/2=reboot/3=recover（T4）。
//   空白页步骤的参数在 collect 时**原样透传**（内部 m_steps 保存真值）。
// · teStepType 切换仅在用户主动操作（activated）时把参数替换为该类型的全新空映射；
//   加载绑定绝不改参数。
// · 目标设备列表 = knownDevices ∪ initial.deviceIds：在册条目文案
//   「名称（无名称回退 deviceId） (首个端点 ip:port)」，被引用者默认勾选；
//   不在册者文案「id（本机无档案）」，默认勾选（悬空引用保留，已拍板）。
#include "task/TaskTemplateEditorDialog.h"

#include "task/TaskExecutionTypes.h"
#include "task/TaskTemplateStore.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString fromUtf8Std(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

std::string toUtf8Std(const QString& value)
{
    return value.toUtf8().toStdString();
}

// 标签串解析：中英文逗号分隔，去空白项（tagSelectors 口径）
std::vector<std::string> parseTagList(const QString& text)
{
    std::vector<std::string> tags;
    QString normalized = text;
    normalized.replace(QStringLiteral("，"), QStringLiteral(","));
    const QStringList parts = normalized.split(u',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            tags.push_back(toUtf8Std(trimmed));
    }
    return tags;
}

QString joinTagList(const std::vector<std::string>& tags)
{
    QStringList parts;
    for (const std::string& tag : tags)
        parts << fromUtf8Std(tag);
    return parts.join(QStringLiteral(", "));
}

// 步骤类型 ↔ 参数栈页索引/类型下拉索引（deploy/commands/reboot/recover 固定 0..3）
int stackIndexForType(TaskStepType type)
{
    switch (type) {
    case TaskStepType::DeployFiles: return 0;
    case TaskStepType::RunCommands: return 1;
    case TaskStepType::Reboot:      return 2;
    case TaskStepType::Recover:     return 3;
    }
    return 0;
}

TaskStepType typeForStackIndex(int index)
{
    switch (index) {
    case 0: return TaskStepType::DeployFiles;
    case 1: return TaskStepType::RunCommands;
    case 2: return TaskStepType::Reboot;
    case 3: return TaskStepType::Recover;
    default: return TaskStepType::DeployFiles;
    }
}

// deploy 参数预检：复刻引擎 parseDeployItems 的文案口径（编辑器同口径；
// 端点存在性判断属执行期 validateDeviceSteps 职责，此处不做）。
// 返回空串=通过；非空=引擎原文（步骤前缀由调用方拼装）。
QString deployParamsParseError(const QVariantMap& params)
{
    const QVariant itemsValue = params.value(QStringLiteral("items"));
    const QVariantList list = itemsValue.toList();
    if (!list.isEmpty()) {
        for (const QVariant& value : list) {
            if (value.typeId() != QMetaType::QVariantMap)
                return QStringLiteral("文件部署步骤条目结构非法（应为对象）");
            const QVariantMap map = value.toMap();
            if (map.value(QStringLiteral("localPath")).toString().isEmpty()
                || map.value(QStringLiteral("remotePath")).toString().isEmpty())
                return QStringLiteral("文件部署步骤存在缺少 localPath/remotePath 的条目");
        }
        return QString();
    }
    const QStringList files = params.value(QStringLiteral("files")).toStringList();
    const QString remoteDirectory = params.value(QStringLiteral("remoteDirectory")).toString();
    if (!files.isEmpty() && !remoteDirectory.isEmpty()) {
        for (const QString& file : files) {
            if (file.trimmed().isEmpty())
                return QStringLiteral("文件部署清单存在空路径");
        }
        return QString();
    }
    return QStringLiteral("文件部署步骤缺少文件清单（items 数组，或 files + remoteDirectory）");
}

// commands trim 去空后是否一条不剩（编辑器预检；引擎执行期同口径取非空集合）
bool commandsEmptyAfterTrim(const QVariantMap& params)
{
    const QStringList commands = params.value(QStringLiteral("commands")).toStringList();
    for (const QString& command : commands) {
        if (!command.trimmed().isEmpty())
            return false;
    }
    return true;
}

} // namespace

TaskTemplateEditorDialog::TaskTemplateEditorDialog(const TaskTemplate& initial,
                                                   const std::vector<DeviceProfile>& knownDevices,
                                                   QWidget* parent)
    : QDialog(parent)
    , m_unknownFields(initial.unknownFields)
    , m_templateId(initial.templateId)
{
    setObjectName(QStringLiteral("taskTemplateEditor"));
    setWindowTitle(QStringLiteral("编辑任务模板"));
    resize(920, 560);

    // ── 左列：元信息 + 目标设备 + 步骤列表 ─────────────────────────
    auto* metaGroup = new QGroupBox(QStringLiteral("模板信息"), this);
    auto* metaForm = new QFormLayout(metaGroup);
    m_name = new QLineEdit(metaGroup);
    m_name->setObjectName(QStringLiteral("teName"));
    m_name->setText(fromUtf8Std(initial.name));
    metaForm->addRow(QStringLiteral("名称"), m_name);
    m_description = new QLineEdit(metaGroup);
    m_description->setObjectName(QStringLiteral("teDescription"));
    m_description->setText(fromUtf8Std(initial.description));
    metaForm->addRow(QStringLiteral("描述"), m_description);
    m_tags = new QLineEdit(metaGroup);
    m_tags->setObjectName(QStringLiteral("teTags"));
    m_tags->setText(joinTagList(initial.tagSelectors));
    metaForm->addRow(QStringLiteral("标签"), m_tags);
    m_version = new QSpinBox(metaGroup);
    m_version->setObjectName(QStringLiteral("teVersion"));
    m_version->setRange(1, 999);
    m_version->setValue(std::clamp(initial.version, 1, 999));
    metaForm->addRow(QStringLiteral("版本"), m_version);

    auto* deviceGroup = new QGroupBox(QStringLiteral("目标设备"), this);
    auto* deviceLayout = new QVBoxLayout(deviceGroup);
    m_devices = new QListWidget(deviceGroup);
    m_devices->setObjectName(QStringLiteral("teDevices"));
    deviceLayout->addWidget(m_devices);

    const auto referenced = [&initial](const std::string& deviceId) {
        return std::find(initial.deviceIds.begin(), initial.deviceIds.end(), deviceId)
               != initial.deviceIds.end();
    };
    std::vector<std::string> knownIds;
    for (const DeviceProfile& profile : knownDevices) {
        knownIds.push_back(profile.deviceId);
        QString display = profile.name.empty() ? fromUtf8Std(profile.deviceId)
                                                : fromUtf8Std(profile.name);
        if (!profile.endpoints.empty()) {
            const DeviceEndpoint& endpoint = profile.endpoints.front();
            display += QStringLiteral(" (%1:%2)")
                           .arg(fromUtf8Std(endpoint.ip))
                           .arg(endpoint.port);
        }
        auto* item = new QListWidgetItem(display, m_devices);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, QByteArray(profile.deviceId.c_str()));
        item->setCheckState(referenced(profile.deviceId) ? Qt::Checked : Qt::Unchecked);
    }
    // 悬空引用：knownDevices 不含的 initial.deviceIds 条目，默认勾选保留（已拍板）
    for (const std::string& deviceId : initial.deviceIds) {
        if (std::find(knownIds.begin(), knownIds.end(), deviceId) != knownIds.end())
            continue;
        auto* item = new QListWidgetItem(
            fromUtf8Std(deviceId) + QStringLiteral("（本机无档案）"), m_devices);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, QByteArray(deviceId.c_str()));
        item->setCheckState(Qt::Checked);
    }

    auto* stepGroup = new QGroupBox(QStringLiteral("执行步骤"), this);
    auto* stepLayout = new QVBoxLayout(stepGroup);
    m_stepList = new QListWidget(stepGroup);
    m_stepList->setObjectName(QStringLiteral("teStepList"));
    stepLayout->addWidget(m_stepList);
    auto* stepButtonRow = new QHBoxLayout();
    auto* addStep = new QPushButton(QStringLiteral("添加步骤"), stepGroup);
    addStep->setObjectName(QStringLiteral("teAddStep"));
    m_removeStepButton = new QPushButton(QStringLiteral("删除"), stepGroup);
    m_removeStepButton->setObjectName(QStringLiteral("teRemoveStep"));
    m_stepUpButton = new QPushButton(QStringLiteral("上移"), stepGroup);
    m_stepUpButton->setObjectName(QStringLiteral("teStepUp"));
    m_stepDownButton = new QPushButton(QStringLiteral("下移"), stepGroup);
    m_stepDownButton->setObjectName(QStringLiteral("teStepDown"));
    stepButtonRow->addWidget(addStep);
    stepButtonRow->addWidget(m_removeStepButton);
    stepButtonRow->addWidget(m_stepUpButton);
    stepButtonRow->addWidget(m_stepDownButton);
    stepButtonRow->addStretch();

    // ── 右列：步骤类型 + 共享协议过滤（栈外，裁决 1）+ 参数页栈 ─────
    auto* rightLayout = new QVBoxLayout();
    auto* typeForm = new QFormLayout();
    m_stepType = new QComboBox(this);
    m_stepType->setObjectName(QStringLiteral("teStepType"));
    m_stepType->addItem(QStringLiteral("文件部署"));
    m_stepType->addItem(QStringLiteral("批量命令"));
    m_stepType->addItem(QStringLiteral("重启"));
    m_stepType->addItem(QStringLiteral("恢复检查"));
    typeForm->addRow(QStringLiteral("步骤类型"), m_stepType);
    m_protocol = new QComboBox(this);
    m_protocol->setObjectName(QStringLiteral("teProtocol"));
    m_protocol->addItem(QStringLiteral("不限"), QString());
    m_protocol->addItem(QStringLiteral("ftp"), QStringLiteral("ftp"));
    m_protocol->addItem(QStringLiteral("sftp"), QStringLiteral("sftp"));
    m_protocol->addItem(QStringLiteral("telnet"), QStringLiteral("telnet"));
    m_protocol->addItem(QStringLiteral("ssh"), QStringLiteral("ssh"));
    typeForm->addRow(QStringLiteral("协议过滤"), m_protocol);
    rightLayout->addLayout(typeForm);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("teStepPages"));
    // Task 3 在此页填充 teRemoteDir/teItemList/teAddFiles/teAddDir；
    // Task 4 在 commands 页填充 teCommands/teTimeout、recover 页填充恢复检查参数。
    auto* deployPage = new QWidget(m_pages);
    deployPage->setObjectName(QStringLiteral("tePageDeploy"));
    auto* commandsPage = new QWidget(m_pages);
    commandsPage->setObjectName(QStringLiteral("tePageCommands"));
    auto* rebootPage = new QWidget(m_pages);
    rebootPage->setObjectName(QStringLiteral("tePageReboot"));
    auto* rebootLayout = new QVBoxLayout(rebootPage);
    auto* rebootNote = new QLabel(
        QStringLiteral("重启步骤无额外参数：按上方协议过滤选择命令端点（不限=自动）。"),
        rebootPage);
    rebootNote->setWordWrap(true);
    rebootLayout->addWidget(rebootNote);
    rebootLayout->addStretch();
    auto* recoverPage = new QWidget(m_pages);
    recoverPage->setObjectName(QStringLiteral("tePageRecover"));
    m_pages->addWidget(deployPage);
    m_pages->addWidget(commandsPage);
    m_pages->addWidget(rebootPage);
    m_pages->addWidget(recoverPage);
    rightLayout->addWidget(m_pages, 1);

    // ── 底部按钮（QDialog 自动接线 accepted→accept()、rejected→reject()；
    //   仅一个 RejectRole 按钮，吸取 deviceProfileEditor 重复取消按钮教训）──
    auto* buttons = new QDialogButtonBox(this);
    auto* save = buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("teSave"));
    auto* cancel = buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("teCancel"));

    auto* rootLayout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout();
    auto* leftLayout = new QVBoxLayout();
    leftLayout->addWidget(metaGroup);
    leftLayout->addWidget(deviceGroup, 1);
    leftLayout->addWidget(stepGroup, 2);
    columns->addLayout(leftLayout, 1);
    columns->addLayout(rightLayout, 1);
    rootLayout->addLayout(columns, 1);
    rootLayout->addWidget(buttons);

    // ── 步骤模型初始化：打开即对 deploy 步应用简写归一（编辑态只见 items[]）──
    m_steps = initial.steps;
    for (TaskStep& step : m_steps) {
        if (step.type == TaskStepType::DeployFiles)
            step.parameters = canonicalDeployParameters(step.parameters);
    }
    QStringList placeholders;
    for (std::size_t i = 0; i < m_steps.size(); ++i)
        placeholders << QStringLiteral("步骤0");   // 文案由 refreshStepRows 统一重建
    m_stepList->addItems(placeholders);
    refreshStepRows();

    // ── 信号接线 ──────────────────────────────────────────────────
    connect(m_stepList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_currentStep = row;
        bindCurrentStep();
        refreshStepRows();
    });
    // teProtocol：仅用户激活项时写回当前步（加载/切换绑定绝不触碰参数，裁决 4）
    connect(m_protocol, &QComboBox::activated, this, [this](int) {
        flushProtocolFromCombo();
    });
    // teStepType：用户主动切换类型 → 参数替换为该类型全新空映射（裁决 2；加载不走此路）
    connect(m_stepType, &QComboBox::activated, this, [this](int index) {
        if (m_binding || index < 0 || index > 3
            || m_currentStep < 0 || m_currentStep >= static_cast<int>(m_steps.size()))
            return;
        TaskStep& step = m_steps[static_cast<std::size_t>(m_currentStep)];
        step.type = typeForStackIndex(index);
        step.parameters = QVariantMap{};
        refreshStepRows();
        bindCurrentStep();   // 换类型后参数已清空：页索引/协议下拉须同步复位
    });
    // teAddStep：deploy→run_commands→reboot→recover 顺序轮转追加
    connect(addStep, &QPushButton::clicked, this, [this] {
        TaskStep step;
        step.type = m_nextAddType;
        step.parameters = QVariantMap{};
        m_steps.push_back(step);
        m_nextAddType = typeForStackIndex(
            (stackIndexForType(m_nextAddType) + 1) % 4);
        m_stepList->addItem(QStringLiteral("步骤0"));
        m_stepList->setCurrentRow(m_stepList->count() - 1);
        refreshStepRows();
    });
    connect(m_removeStepButton, &QPushButton::clicked, this, [this] {
        if (m_currentStep < 0 || m_currentStep >= static_cast<int>(m_steps.size()))
            return;
        const int row = m_currentStep;
        m_steps.erase(m_steps.begin() + row);      // 先改模型，再动视图行
        delete m_stepList->takeItem(row);          // 选中自动落邻行（空表→-1），触发绑定
        refreshStepRows();
    });
    connect(m_stepUpButton, &QPushButton::clicked, this, [this] { moveStepRow(-1); });
    connect(m_stepDownButton, &QPushButton::clicked, this, [this] { moveStepRow(1); });

    // 首行绑定：reboot 页/协议过滤打开即反映第一个步骤的参数
    if (!m_steps.empty())
        m_stepList->setCurrentRow(0);
    else
        bindCurrentStep();
}

void TaskTemplateEditorDialog::moveStepRow(int delta)
{
    const int from = m_currentStep;
    if (from < 0 || from >= static_cast<int>(m_steps.size()))
        return;
    const int to = from + delta;
    if (to < 0 || to >= static_cast<int>(m_steps.size()))
        return;
    std::swap(m_steps[static_cast<std::size_t>(from)],
              m_steps[static_cast<std::size_t>(to)]);
    QListWidgetItem* item = m_stepList->takeItem(from);
    m_stepList->insertItem(to, item);
    m_stepList->setCurrentRow(to);                // 重新绑定移动后的当前步
    refreshStepRows();
}

void TaskTemplateEditorDialog::refreshStepRows()
{
    const int rows = std::min(m_stepList->count(), static_cast<int>(m_steps.size()));
    for (int i = 0; i < rows; ++i) {
        m_stepList->item(i)->setText(
            QStringLiteral("步骤%1 · %2")
                .arg(i + 1)
                .arg(taskStepTypeToChineseName(m_steps[static_cast<std::size_t>(i)].type)));
    }
    const bool hasStep = m_currentStep >= 0 && m_currentStep < static_cast<int>(m_steps.size());
    if (m_removeStepButton)
        m_removeStepButton->setEnabled(hasStep);
    if (m_stepUpButton)
        m_stepUpButton->setEnabled(hasStep && m_currentStep > 0);
    if (m_stepDownButton)
        m_stepDownButton->setEnabled(hasStep
                                     && m_currentStep + 1 < static_cast<int>(m_steps.size()));
}

void TaskTemplateEditorDialog::bindCurrentStep()
{
    m_binding = true;
    const bool hasStep = m_currentStep >= 0 && m_currentStep < static_cast<int>(m_steps.size());
    m_stepType->setEnabled(hasStep);
    m_protocol->setEnabled(hasStep);
    m_pages->setEnabled(hasStep);
    if (hasStep) {
        const TaskStep& step = m_steps[static_cast<std::size_t>(m_currentStep)];
        m_stepType->setCurrentIndex(stackIndexForType(step.type));
        const QString protocol =
            step.parameters.value(QStringLiteral("protocol")).toString().trimmed().toLower();
        const int protocolIndex = m_protocol->findData(protocol);
        // 表外协议值（如未来新增）→ 显示"不限"但不回写，避免静默改值
        m_protocol->setCurrentIndex(protocolIndex >= 0 ? protocolIndex : 0);
        m_pages->setCurrentIndex(stackIndexForType(step.type));
    } else {
        m_stepType->setCurrentIndex(-1);
        m_protocol->setCurrentIndex(0);
        m_pages->setCurrentIndex(-1);
    }
    m_binding = false;
}

void TaskTemplateEditorDialog::flushProtocolFromCombo()
{
    if (m_binding || m_currentStep < 0 || m_currentStep >= static_cast<int>(m_steps.size()))
        return;
    const QString protocol = m_protocol->currentData().toString();
    QVariantMap& params = m_steps[static_cast<std::size_t>(m_currentStep)].parameters;
    if (protocol.isEmpty())
        params.remove(QStringLiteral("protocol"));   // 不限=不写 protocol 键（引擎空过滤同义）
    else
        params.insert(QStringLiteral("protocol"), protocol);
}

TaskTemplate TaskTemplateEditorDialog::assemble() const
{
    TaskTemplate candidate;
    candidate.templateId = m_templateId;
    candidate.name = toUtf8Std(m_name->text().trimmed());
    candidate.description = toUtf8Std(m_description->text());
    candidate.tagSelectors = parseTagList(m_tags->text());
    candidate.version = m_version->value();
    for (int i = 0; i < m_devices->count(); ++i) {
        QListWidgetItem* item = m_devices->item(i);
        if (item->checkState() == Qt::Checked) {
            candidate.deviceIds.push_back(item->data(Qt::UserRole).toByteArray().toStdString());
        }
    }
    candidate.steps = m_steps;                       // 空白页步骤参数原样透传
    candidate.unknownFields = m_unknownFields;
    return candidate;
}

QString TaskTemplateEditorDialog::validate() const
{
    const TaskTemplate candidate = assemble();
    const TaskTemplateValidationResult base = validateTaskTemplate(candidate);
    if (!base.valid)
        return base.error;
    for (std::size_t i = 0; i < candidate.steps.size(); ++i) {
        const TaskStep& step = candidate.steps[i];
        switch (step.type) {
        case TaskStepType::RunCommands:
        case TaskStepType::Recover:
            if (commandsEmptyAfterTrim(step.parameters))
                return QStringLiteral("步骤 %1 至少一条命令").arg(i + 1);
            break;
        case TaskStepType::DeployFiles: {
            const QString parseError = deployParamsParseError(step.parameters);
            if (!parseError.isEmpty()) {
                return QStringLiteral("步骤 %1（文件部署）参数非法：%2")
                    .arg(i + 1)
                    .arg(parseError);
            }
            break;
        }
        case TaskStepType::Reboot:
            break;   // 无额外参数；协议过滤可选
        }
    }
    return QString();
}

bool TaskTemplateEditorDialog::collect(TaskTemplate* out) const
{
    if (!out)
        return false;
    if (!validate().isEmpty())
        return false;
    *out = assemble();
    return true;
}

QVariantMap TaskTemplateEditorDialog::canonicalDeployParameters(const QVariantMap& params)
{
    const QVariantList existing = params.value(QStringLiteral("items")).toList();
    if (!existing.isEmpty())
        return params;                              // 已是规范形态：不动
    const QStringList files = params.value(QStringLiteral("files")).toStringList();
    const QString remoteDirectory =
        params.value(QStringLiteral("remoteDirectory")).toString();
    if (files.isEmpty() || remoteDirectory.isEmpty())
        return params;                              // 无简写可归一
    QString base = remoteDirectory;
    while (base.size() > 1 && base.endsWith(u'/'))
        base.chop(1);                               // 去尾斜杠拼接（引擎同口径）
    QVariantList items;
    for (const QString& file : files) {
        if (file.trimmed().isEmpty())
            return params;                          // 空路径留给 validate 出引擎同口径文案
        QVariantMap entry;
        entry.insert(QStringLiteral("localPath"), file);
        const QString name = QFileInfo(file).fileName();
        entry.insert(QStringLiteral("remotePath"),
                     base == QLatin1String("/")
                         ? QStringLiteral("/") + name
                         : base + QLatin1Char('/') + name);
        items.push_back(entry);
    }
    QVariantMap canon = params;
    canon.remove(QStringLiteral("files"));
    canon.insert(QStringLiteral("items"), items);
    return canon;
}

void TaskTemplateEditorDialog::accept()
{
    const QString error = validate();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法保存模板"), error);
        return;                                     // 校验不过停留编辑，不关闭窗口
    }
    QDialog::accept();
}
