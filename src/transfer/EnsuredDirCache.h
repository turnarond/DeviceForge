#pragma once
// EnsuredDirCache.h — v2.15 波3 任务11（波2 终审 I-3）：运行级「已确保存在的远端
// 目录」集合。TransferExecutor 父目录确保步在三层「已确认存在」路径（该层 stat 报
// 存在 / makeDirectory 成功 / MKD 失败后复检报存在）后记入本缓存；同一运行内后续文
// 件（DeployJob 每条目新建执行器）对这些层直接跳过 stat，消除波2 形态下祖先层的重
// 复控制往返——上限形态 5000 条 × 3-4 层在慢速 SylixOS FTP 上约省上万次往返。
// 线程契约：刻意无锁——缓存由 DeployJob 成员持有，每设备一份、job 内单线程
// （DeploymentRunner 的并发粒度是每 job 一个线程）。未来任何调用方若跨 job/线程
// 共享同一缓存实例，必须自行保证串行访问或另加同步。
// 比较口径：大小写敏感（QSet<QString> 精确匹配）——与远端 POSIX 目录名一致
// （SylixOS/Linux），不引入 caseFold。
#include <QSet>
#include <QString>

namespace xfer {

class EnsuredDirCache
{
public:
    bool contains(const QString& path) const
    {
        return m_paths.contains(path);
    }

    void insert(const QString& path)
    {
        m_paths.insert(path);
    }

private:
    QSet<QString> m_paths;
};

} // namespace xfer
