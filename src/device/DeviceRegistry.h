#pragma once

#include "device/DeviceProfile.h"

#include <optional>
#include <string>
#include <vector>

class DeviceRegistry
{
public:
    DeviceProfile upsert(const DeviceProfile& profile);
    std::optional<DeviceProfile> find(const std::string& deviceId) const;
    std::vector<DeviceProfile> list() const;

    // ConfigStore 持久化（type=device.profile，key=deviceId）。
    // load() 同时回退读取旧 device.list 记录：缺失 deviceId 的旧记录按规范化
    // 端点身份生成确定性 ID（只读迁移，不改写旧记录）；损坏记录跳过并告警。
    bool load();
    // save() 先合并进内存注册表（生成/保持 deviceId），再持久化合并结果。
    // 端点为并集合并语义——保留给协议发现类调用方（addDevice/fromDeviceInfo），
    // 发现新端点只增不减。
    bool save(const DeviceProfile& profile);
    // saveReplacing() 编辑器保存路径（v2.11 终审 Critical 1）：端点表按「整体
    // 替换」语义合并——传入清单未列出的既有端点将从档案中删除（名称/备注/标签
    // 仍与 save 同规则合并）。成功时持久化替换结果并清扫被取代的旧 device.list
    // ip:port 行（被移除的 ip:port 已不再属于任何在册档案时删除，防止 load()
    // 回退读取复活旧地址）。stored 输出替换合并后的档案（供 UI 回写旧行/发信号，
    // 失败时同样给出内存合并结果）。
    bool saveReplacing(const DeviceProfile& profile, DeviceProfile* stored);
    // remove() 从内存与 ConfigStore 删除指定档案；同时删除与该档案端点身份
    // 匹配的旧 device.list 行（key=ip:port），保证对迁移设备删除持久；
    // 不匹配的旧行保留，仍可被 load() 回退读取。
    bool remove(const std::string& deviceId);

    // 重名不阻止保存；UI 可读取此提示并以名称加地址消歧。
    const std::vector<std::string>& warnings() const;

private:
    static DeviceProfile normalize(const DeviceProfile& profile);
    static std::string stableId(const DeviceProfile& profile);
    // 命中既有档案的下标（deviceId 优先，端点身份回退）；未命中返回 size()。
    // incoming 必须已 normalize。upsert 与 saveReplacing 共用同一命中规则。
    std::size_t locate(const DeviceProfile& incoming) const;
    void rebuildWarnings();

    std::vector<DeviceProfile> m_profiles;
    std::vector<std::string> m_warnings;
};
