#pragma once
// v2.15 #49-6：运行级终态收敛规则单源（原 TaskCenterWidget::aggregateStatus 与
// tst_task_center_e2e 复制实现两处同规则并行——自本头起仅此一版）。
// 函数体逐字搬移自 TaskCenterWidget::aggregateStatus 原函数体，逻辑零改写。
// TaskRunOutcome/TaskDeviceState 来自 TaskExecutionTypes.h；TaskRunStatus 来自 TaskRunTypes.h。
#include "task/TaskExecutionTypes.h"
#include "task/TaskRunTypes.h"

namespace taskrun {

inline TaskRunStatus aggregateRunStatus(const TaskRunOutcome& outcome)
{
    int succeeded = 0;
    int partial = 0;
    int cancelled = 0;
    int failed = 0;
    for (const auto& device : outcome.devices) {
        switch (device.state) {
        case TaskDeviceState::Succeeded:
        case TaskDeviceState::Skipped:
            ++succeeded;
            break;
        case TaskDeviceState::PartiallySucceeded:
            ++partial;
            break;
        case TaskDeviceState::Cancelled:
            ++cancelled;
            break;
        default:
            ++failed;
            break;
        }
    }
    const int total = static_cast<int>(outcome.devices.size());
    if (total > 0 && succeeded == total)
        return TaskRunStatus::Succeeded;
    if (succeeded > 0 || partial > 0)
        return TaskRunStatus::PartiallySucceeded;
    if (cancelled > 0 && failed == 0)
        return TaskRunStatus::Cancelled;
    return TaskRunStatus::Failed;
}

} // namespace taskrun
