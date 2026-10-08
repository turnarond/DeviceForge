#pragma once

#include "device/DeviceProfile.h"

#include <QVariantMap>
#include <optional>

// v2.11 Task 2：设备档案 ConfigStore 记录编解码（type=device.profile，key=deviceId）。
// 密码只由 ConfigStore/DPAPI 管理，此处仅保存凭据引用 credentialRef，绝不写入密码明文。

// 将设备档案编码为 QVariantMap 记录。
QVariantMap encodeDeviceProfile(const DeviceProfile& profile);

// 解码 ConfigStore 记录为设备档案，兼容两类来源：
// 1) 新格式 device.profile 记录（deviceId/name/note/tags/endpoints）；
// 2) 旧格式 device.list 记录（ip/port/protocol/displayName/note），
//    displayName 即旧 DeviceInfo::alias，映射为设备名称；与 ip 相同视为无别名。
// 缺失 deviceId 的旧记录保持空串，由 DeviceRegistry::upsert 依据规范化端点
// 身份生成确定性 ID。记录结构非法（无有效端点且无可用 ip）返回 std::nullopt，
// 调用方跳过该记录实现回退读取。
std::optional<DeviceProfile> decodeDeviceProfile(const QVariantMap& record);
