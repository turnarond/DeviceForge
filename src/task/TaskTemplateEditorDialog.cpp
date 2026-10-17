// v2.13 #54 Task 2/3：任务模板编辑器对话框（骨架 + deploy 步骤面板）。
// 设计裁决（控制器拍板，后续 Task 依赖）：
// · teProtocol 为**全类型共享**的协议过滤下拉，置于 QStackedWidget 之外（栈上方），
//   四种步骤类型读写同一 parameters["protocol"] 键；T3/T4 不再各自建 teProtocol。
// · 参数栈页序：0=deploy（T3 已落地 items 表）/1=commands+recover 共用（T4）/2=reboot。
//   空白页步骤的参数在 collect 时**原样透传**（内部 m_steps 保存真值）。
// · teStepType 切换仅在用户主动操作（activated）时把参数替换为该类型的全新空映射；
//   加载绑定绝不改参数。
// · deploy 页（T3）：teRemoteDir 仅是批量添加的 UI 远端前缀，**不写入参数**；
//   参数只携带 items[]（加载已经 canonicalDeployParameters 归一）。表格行=items 行，
//   方向/覆盖列用单元格 QComboBox（itemData=引擎规范串 upload|download / overwrite|skip，
//   缺省 upload/overwrite）；用户编辑即时写回 m_steps，collect 再从可见表格快照兜底。
// · 目标设备列表 = knownDevices ∪ initial.deviceIds：在册条目文案
//   「名称（无名称回退 deviceId） (首个端点 ip:port)」，被引用者默认勾选；
//   不在册者文案「id（本机无档案）」，默认勾选（悬空引用保留，已拍板）。
#include "task/TaskTemplateEditorDialog.h"

#include "task/TaskExecutionTypes.h"
#include "task/TaskStepValidation.h"
#include "task/TaskTemplateStore.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

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

// 参数栈**页**索引（与类型下拉索引解耦）：run_commands 与 recover 引擎参数键完全
// 一致（commands[]/timeoutSec），故共用同一个命令面板页（index1），recover 仅额外
// 显示顶部说明 label；deploy=0 / reboot=2。栈仅 3 页。
int pageIndexForType(TaskStepType type)
{
    switch (type) {
    case TaskStepType::DeployFiles: return 0;
    case TaskStepType::RunCommands:
    case TaskStepType::Recover:     return 1;
    case TaskStepType::Reboot:      return 2;
    }
    return 0;
}

// 步骤参数级校验（deploy 参数形态、commands trim 判空）已收敛至共享纯函数
// taskstep::stepParamsValidationError（v2.14 #49-3，引擎/编辑器文案单源）。
// 端点/设备存在性判断属执行期 validateDeviceSteps 职责，编辑器不做。

// 批量添加远端路径拼接：与 canonicalDeployParameters 同口径（去尾斜杠；
// 空前缀=根目录 → "/"+名称，保证 remotePath 非空、validate 引擎契约不破）
QString joinRemotePath(const QString& prefix, const QString& name)
{
    QString base = prefix.trimmed();
    while (base.size() > 1 && base.endsWith(u'/'))
        base.chop(1);
    if (base.isEmpty() || base == QLatin1String("/"))
        return QStringLiteral("/") + name;
    return base + QLatin1Char('/') + name;
}

// 方向/覆盖单元格下拉：文案中文、itemData=引擎规范串（collect 只读 currentData）。
// 规范值口径同引擎 parseDeployItems：direction 非 "download" 一律 upload，
// overwrite 仅 "skip" 为跳过、其余覆盖。
QComboBox* makeDeployChoiceCombo(const QString& canonicalValue, bool direction, QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    if (direction) {
        combo->addItem(QStringLiteral("上传"), QStringLiteral("upload"));
        combo->addItem(QStringLiteral("下载"), QStringLiteral("download"));
    } else {
        combo->addItem(QStringLiteral("覆盖"), QStringLiteral("overwrite"));
        combo->addItem(QStringLiteral("跳过"), QStringLiteral("skip"));
    }
    const int index = combo->findData(canonicalValue);
    combo->setCurrentIndex(index >= 0 ? index : 0);   // 表外值回退缺省项（引擎同口径）
    return combo;
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
    // #55：上限不设实用界（store 无上限、导入文件 version 任意）——999 封顶会把
    // 高版本模板钳到 999，再被存储降级闸永久拒绝，形成编辑死锁
    m_version->setRange(1, std::numeric_limits<int>::max());
    m_version->setValue(std::max(1, initial.version));
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
    // deploy 页由 setupDeployPage 填充 teRemoteDir/teItemList/teAddFiles/teAddDir（T3）；
    // Task 4 在共用命令页填充 teCommands/teTimeout（run_commands 与 recover 同页，
    // recover 额外显示顶部说明 label）。reboot 页为静态说明。
    auto* deployPage = new QWidget(m_pages);
    deployPage->setObjectName(QStringLiteral("tePageDeploy"));
    setupDeployPage(deployPage);
    auto* commandsPage = new QWidget(m_pages);
    commandsPage->setObjectName(QStringLiteral("tePageCommands"));
    setupCommandsLikePage(commandsPage);
    auto* rebootPage = new QWidget(m_pages);
    rebootPage->setObjectName(QStringLiteral("tePageReboot"));
    auto* rebootLayout = new QVBoxLayout(rebootPage);
    auto* rebootNote = new QLabel(
        QStringLiteral("重启步骤无额外参数：按上方协议过滤选择命令端点（不限=自动）。"),
        rebootPage);
    rebootNote->setWordWrap(true);
    rebootLayout->addWidget(rebootNote);
    rebootLayout->addStretch();
    m_pages->addWidget(deployPage);
    m_pages->addWidget(commandsPage);
    m_pages->addWidget(rebootPage);
    rightLayout->addWidget(m_pages, 1);

    // ── 底部按钮（必须手动接线 accepted→accept()/rejected→reject()——
    //   QDialog 并不会自动连 QDialogButtonBox，v2.13 起此处误信"自动接线"
    //   导致保存/点击无响应，v2.14 冒烟修复；accept 为虚函数覆写，成员指针
    //   调用走 vtable 分派到本类校验闸。仅一个 RejectRole 按钮，吸取
    //   deviceProfileEditor 重复取消按钮教训）──
    auto* buttons = new QDialogButtonBox(this);
    auto* save = buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("teSave"));
    auto* cancel = buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("teCancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

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

void TaskTemplateEditorDialog::setupDeployPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);

    // 远端目录前缀：纯 UI 辅助（批量添加拼接用），不是持久化参数键（裁决）
    auto* dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(QStringLiteral("远端目录前缀"), page));
    m_remoteDir = new QLineEdit(page);
    m_remoteDir->setObjectName(QStringLiteral("teRemoteDir"));
    m_remoteDir->setPlaceholderText(QStringLiteral("/opt/app（留空=根目录；不写入模板参数）"));
    dirRow->addWidget(m_remoteDir, 1);
    layout->addLayout(dirRow);

    // items 表：行=items 条目；列 0/1 文本（本地/远端路径），列 2/3 单元格下拉
    m_itemTable = new QTableWidget(0, 4, page);
    m_itemTable->setObjectName(QStringLiteral("teItemList"));
    m_itemTable->setHorizontalHeaderLabels(
        {QStringLiteral("本地路径"), QStringLiteral("远端路径"),
         QStringLiteral("方向"), QStringLiteral("覆盖")});
    m_itemTable->verticalHeader()->setVisible(false);
    m_itemTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_itemTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_itemTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_itemTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_itemTable, 1);
    // 用户编辑路径单元格（含测试直接 setItem）→ 即时写回当前 deploy 步；
    // 程序化灌表走 blockSignals + m_binding 双守卫，不触发写回（同 teProtocol 纪律）
    connect(m_itemTable, &QTableWidget::cellChanged, this, [this](int, int) {
        flushDeployTable();
    });

    auto* addRow = new QHBoxLayout();
    m_addFiles = new QPushButton(QStringLiteral("添加文件…"), page);
    m_addFiles->setObjectName(QStringLiteral("teAddFiles"));
    m_addDir = new QPushButton(QStringLiteral("添加目录…"), page);
    m_addDir->setObjectName(QStringLiteral("teAddDir"));
    addRow->addWidget(m_addFiles);
    addRow->addWidget(m_addDir);
    // v2.14 冒烟修复：条目此前只加不删（无按钮/无 Del/无右键），添加错了只能
    // 整步重配。移除按钮按选中行启用；removeRow 不发 cellChanged，须手动 flush
    // 写回 m_steps 真值（binding 守卫窗口内忽略，防程序化换步误删）。
    m_removeItem = new QPushButton(QStringLiteral("移除选中行"), page);
    m_removeItem->setObjectName(QStringLiteral("teRemoveItem"));
    m_removeItem->setEnabled(false);
    addRow->addWidget(m_removeItem);
    addRow->addStretch();
    layout->addLayout(addRow);
    connect(m_itemTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { m_removeItem->setEnabled(row >= 0); });
    connect(m_removeItem, &QPushButton::clicked, this, [this] {
        const int row = m_itemTable->currentRow();
        if (row < 0 || m_binding)
            return;
        m_itemTable->removeRow(row);
        flushDeployTable();
    });

    // 批量添加：非原生 QFileDialog（DontUseNativeDialog），每条/每个目录生成行；
    // remotePath = teRemoteDir(去尾斜杠) + '/' + 文件/目录名，空前缀=根目录
    connect(m_addFiles, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("选择要部署的文件"), QString(),
            QStringLiteral("所有文件 (*)"), nullptr,
            QFileDialog::DontUseNativeDialog);
        const QString prefix = m_remoteDir->text();
        for (const QString& file : files) {
            if (file.trimmed().isEmpty())
                continue;
            appendDeployRow(file,
                            joinRemotePath(prefix, QFileInfo(file).fileName()),
                            QStringLiteral("upload"), QStringLiteral("overwrite"));
        }
    });
    connect(m_addDir, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择要部署的目录"), QString(),
            QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly);
        if (dir.trimmed().isEmpty())
            return;
        const QString name = QFileInfo(dir).fileName();
        if (name.isEmpty())
            return;                       // 盘符根等无目录名路径：无法生成远端名，跳过
        appendDeployRow(dir, joinRemotePath(m_remoteDir->text(), name),
                        QStringLiteral("upload"), QStringLiteral("overwrite"));
    });
}

void TaskTemplateEditorDialog::appendDeployRow(const QString& localPath, const QString& remotePath,
                                               const QString& direction, const QString& overwrite)
{
    const int row = m_itemTable->rowCount();
    m_itemTable->insertRow(row);
    // 先挂下拉再写文本单元格：cellChanged 触发的 flush 读到完整行
    auto* directionCombo = makeDeployChoiceCombo(direction, true, m_itemTable);
    connect(directionCombo, &QComboBox::activated, this, [this](int) { flushDeployTable(); });
    m_itemTable->setCellWidget(row, 2, directionCombo);
    auto* overwriteCombo = makeDeployChoiceCombo(overwrite, false, m_itemTable);
    connect(overwriteCombo, &QComboBox::activated, this, [this](int) { flushDeployTable(); });
    m_itemTable->setCellWidget(row, 3, overwriteCombo);
    m_itemTable->setItem(row, 0, new QTableWidgetItem(localPath));
    m_itemTable->setItem(row, 1, new QTableWidgetItem(remotePath));
}

void TaskTemplateEditorDialog::fillDeployTable(const QVariantMap& params)
{
    m_itemTable->blockSignals(true);          // 程序化灌表不触发写回（m_binding 之外的双守卫）
    m_itemTable->setRowCount(0);
    const QVariantList items = params.value(QStringLiteral("items")).toList();
    for (const QVariant& value : items) {
        if (value.typeId() != QMetaType::QVariantMap)
            continue;   // 结构非法条目不入表展示；validate 以引擎同口径文案拦截
        const QVariantMap entry = value.toMap();
        const QString direction =
            entry.value(QStringLiteral("direction")).toString() == QLatin1String("download")
                ? QStringLiteral("download") : QStringLiteral("upload");
        const QString overwrite =
            entry.value(QStringLiteral("overwrite")).toString() == QLatin1String("skip")
                ? QStringLiteral("skip") : QStringLiteral("overwrite");
        appendDeployRow(entry.value(QStringLiteral("localPath")).toString(),
                        entry.value(QStringLiteral("remotePath")).toString(),
                        direction, overwrite);
    }
    m_itemTable->blockSignals(false);
}

QVariantList TaskTemplateEditorDialog::deployItemsFromTable() const
{
    QVariantList items;
    for (int row = 0; row < m_itemTable->rowCount(); ++row) {
        const QTableWidgetItem* localCell = m_itemTable->item(row, 0);
        const QTableWidgetItem* remoteCell = m_itemTable->item(row, 1);
        const QString localPath = localCell ? localCell->text().trimmed() : QString();
        const QString remotePath = remoteCell ? remoteCell->text().trimmed() : QString();
        if (localPath.isEmpty() && remotePath.isEmpty())
            continue;   // 全空占位行不入 items（引擎会拒绝缺路径条目）
        const auto* directionCombo = qobject_cast<QComboBox*>(m_itemTable->cellWidget(row, 2));
        const auto* overwriteCombo = qobject_cast<QComboBox*>(m_itemTable->cellWidget(row, 3));
        QVariantMap entry;
        entry.insert(QStringLiteral("localPath"), localPath);
        entry.insert(QStringLiteral("remotePath"), remotePath);
        entry.insert(QStringLiteral("direction"), directionCombo
                         ? directionCombo->currentData().toString()
                         : QStringLiteral("upload"));
        entry.insert(QStringLiteral("overwrite"), overwriteCombo
                         ? overwriteCombo->currentData().toString()
                         : QStringLiteral("overwrite"));
        items.push_back(entry);
    }
    return items;
}

void TaskTemplateEditorDialog::flushDeployTable()
{
    if (m_binding || m_currentStep < 0 || m_currentStep >= static_cast<int>(m_steps.size()))
        return;
    TaskStep& step = m_steps[static_cast<std::size_t>(m_currentStep)];
    if (step.type != TaskStepType::DeployFiles || m_pages->currentIndex() != 0)
        return;   // 仅 deploy 页展示时写回（teRemoteDir 不入参数；protocol 等键不动）
    const QVariantList items = deployItemsFromTable();
    if (items.isEmpty())
        step.parameters.remove(QStringLiteral("items"));   // 空表回到「缺清单」原始拒绝文案
    else
        step.parameters.insert(QStringLiteral("items"), items);
}

void TaskTemplateEditorDialog::setupCommandsLikePage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    // recover 顶部说明 label：run_commands 与 recover 共用本页，仅 recover 时可见
    //（绑定态由 bindCurrentStep 按步类型 setVisible 切换）
    m_recoverNote = new QLabel(
        QStringLiteral("重启后成功判定检测：命令全部成功返回即视为设备已恢复。"), page);
    m_recoverNote->setObjectName(QStringLiteral("teRecoverNote"));
    m_recoverNote->setWordWrap(true);
    m_recoverNote->setVisible(false);
    layout->addWidget(m_recoverNote);
    layout->addWidget(new QLabel(QStringLiteral("命令（一行一条）"), page));
    m_commandsEditor = new QPlainTextEdit(page);
    m_commandsEditor->setObjectName(QStringLiteral("teCommands"));
    m_commandsEditor->setPlaceholderText(QStringLiteral("ls\npwd\nreboot"));
    layout->addWidget(m_commandsEditor, 1);
    auto* timeoutRow = new QHBoxLayout();
    timeoutRow->addWidget(new QLabel(QStringLiteral("单命令超时（秒）"), page));
    m_commandsTimeout = new QSpinBox(page);
    m_commandsTimeout->setObjectName(QStringLiteral("teTimeout"));
    m_commandsTimeout->setRange(1, 600);   // 引擎 std::max(1, intParam(...,10))：1..600 覆盖
    m_commandsTimeout->setValue(10);        // 缺省 10（引擎默认同值）
    timeoutRow->addWidget(m_commandsTimeout);
    timeoutRow->addStretch();
    layout->addLayout(timeoutRow);

    // 用户编辑即时写回当前步（同 deploy/teProtocol 纪律）：
    // textChanged/valueChanged 仅在非绑定态触发写回；程序化灌值走 blockSignals 不写回。
    connect(m_commandsEditor, &QPlainTextEdit::textChanged, this, [this] {
        flushCommandsLike();
    });
    connect(m_commandsTimeout, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        flushCommandsLike();
    });
}

void TaskTemplateEditorDialog::fillCommandsLike(const QVariantMap& params)
{
    m_commandsEditor->blockSignals(true);   // 程序化灌值不触发写回（m_binding 之外的双守卫）
    m_commandsEditor->setPlainText(params.value(QStringLiteral("commands"))
                                      .toStringList()
                                      .join(QLatin1Char('\n')));
    // 务必解除阻塞：QPlainTextEdit::textChanged 发在控件自身，若此处不 blockSignals(false)，
    // 首次绑定后信号永久封锁 → 用户编辑永不 flush 到 m_steps → 切步即静默丢改动（评审 Critical 1）
    m_commandsEditor->blockSignals(false);
    m_commandsTimeout->blockSignals(true);
    const int seconds = params.contains(QStringLiteral("timeoutSec"))
                            ? params.value(QStringLiteral("timeoutSec")).toInt()
                            : 10;           // 缺省=引擎默认 10
    m_commandsTimeout->setValue(std::clamp(seconds, 1, 600));
    m_commandsTimeout->blockSignals(false);
}

void TaskTemplateEditorDialog::flushCommandsLike()
{
    if (m_binding || m_currentStep < 0 || m_currentStep >= static_cast<int>(m_steps.size()))
        return;
    TaskStep& step = m_steps[static_cast<std::size_t>(m_currentStep)];
    if (step.type != TaskStepType::RunCommands && step.type != TaskStepType::Recover)
        return;   // 仅命令类步骤写回（deploy/reboot 步不触碰本编辑器）
    QStringList lines = m_commandsEditor->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (QString& line : lines)
        line = line.trimmed();
    lines.removeAll(QString());             // 逐行 trim 后去空（引擎执行期同口径取非空集合）
    step.parameters.insert(QStringLiteral("commands"), lines);
    // timeoutSec 恒写为 int（引擎 std::max(1, intParam(...,10)) 容忍其存在，含默认 10）
    step.parameters.insert(QStringLiteral("timeoutSec"), m_commandsTimeout->value());
    // protocol 键由共享 teProtocol 管理（flushProtocolFromCombo），此处不动
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
    // takeItem/insertItem 会瞬态重发 selection→currentRowChanged：blockSignals 压制，
    // 落定后显式绑定（防以旧 m_currentStep 误读互换中的模型——T2 遗留 minor 就此修实）
    m_stepList->blockSignals(true);
    QListWidgetItem* item = m_stepList->takeItem(from);
    m_stepList->insertItem(to, item);
    m_stepList->setCurrentRow(to);
    m_stepList->blockSignals(false);
    m_currentStep = to;                         // 重新绑定移动后的当前步
    bindCurrentStep();
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
        m_pages->setCurrentIndex(pageIndexForType(step.type));
        // deploy 页绑定：items[] → 表格逐行灌入（blockSignals 双守卫，不触发写回）
        if (step.type == TaskStepType::DeployFiles)
            fillDeployTable(step.parameters);
        // commands/recover 共用页绑定：commands[]/timeoutSec → 编辑器/自旋（同双守卫）；
        // recover 额外显示顶部说明 label，run_commands 隐藏
        else if (step.type == TaskStepType::RunCommands || step.type == TaskStepType::Recover)
            fillCommandsLike(step.parameters);
        if (m_recoverNote)
            m_recoverNote->setVisible(step.type == TaskStepType::Recover);
        // 顺带修（T3 遗留 minor）：非 deploy 步绑定即清除批量添加前缀，
        // 防上一个 deploy 步的 teRemoteDir 暗套到别的步的批量添加（该键不持久化，清空无损）
        if (step.type != TaskStepType::DeployFiles && m_remoteDir)
            m_remoteDir->clear();
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
    // deploy 页展示中：以可见表格实时快照兜底写回当前步 items[]
    //（即时 flush 已保 m_steps 常新，这里覆盖未提交编辑的边界；仅当前步，其余步不动）
    if (m_itemTable && m_pages->currentIndex() == 0
        && m_currentStep >= 0 && m_currentStep < static_cast<int>(candidate.steps.size())
        && candidate.steps[static_cast<std::size_t>(m_currentStep)].type
               == TaskStepType::DeployFiles) {
        QVariantMap& params = candidate.steps[static_cast<std::size_t>(m_currentStep)].parameters;
        const QVariantList items = deployItemsFromTable();
        if (items.isEmpty())
            params.remove(QStringLiteral("items"));
        else
            params.insert(QStringLiteral("items"), items);
    }
    // commands/recover 共用页展示中：以可见编辑器快照兜底写回当前步 commands[]/timeoutSec
    //（同 deploy 纪律；页索引经 pageIndexForType 分派，run_commands 与 recover 同为 1；
    //  protocol 由共享 combo 处理，此处不触碰）。逐行 trim 去空 → QStringList，timeoutSec 恒写 int。
    if (m_commandsEditor && m_currentStep >= 0
        && m_currentStep < static_cast<int>(candidate.steps.size())) {
        const TaskStepType type = candidate.steps[static_cast<std::size_t>(m_currentStep)].type;
        if ((type == TaskStepType::RunCommands || type == TaskStepType::Recover)
            && m_pages->currentIndex() == pageIndexForType(type)) {
            QVariantMap& params =
                candidate.steps[static_cast<std::size_t>(m_currentStep)].parameters;
            QStringList lines =
                m_commandsEditor->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            for (QString& line : lines)
                line = line.trimmed();
            lines.removeAll(QString());
            params.insert(QStringLiteral("commands"), lines);
            params.insert(QStringLiteral("timeoutSec"), m_commandsTimeout->value());
        }
    }
    candidate.unknownFields = m_unknownFields;
    return candidate;
}

QString TaskTemplateEditorDialog::validate() const
{
    const TaskTemplate candidate = assemble();
    const TaskTemplateValidationResult base = validateTaskTemplate(candidate);
    if (!base.valid)
        return base.error;
    // 步骤参数级校验走共享纯函数（v2.14 #49-3：与引擎 Validating 文案单源；
    // 端点存在性留执行期，编辑器不校验设备端点）。
    for (std::size_t i = 0; i < candidate.steps.size(); ++i) {
        const QString err = taskstep::stepParamsValidationError(candidate.steps[i], i);
        if (!err.isEmpty())
            return err;
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
    canon.remove(QStringLiteral("remoteDirectory"));   // 契约：items 规范形态去 files/remoteDirectory 两键
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
