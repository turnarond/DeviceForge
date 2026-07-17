#pragma once
#include "IProtocolAdapter.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

// 协议适配器工厂注册表（Meyer's Singleton）
// 启动时注册工厂，运行时按 protocolId 创建适配器实例
class ProtocolRegistry {
public:
    static ProtocolRegistry* instance();

    // 注册适配器工厂（启动时调用）
    void registerFactory(const std::string& protocolId, AdapterFactory factory);

    // 创建适配器实例
    std::shared_ptr<IProtocolAdapter> create(const std::string& protocolId) const;

    // 查询已注册协议
    bool isRegistered(const std::string& protocolId) const;
    std::vector<std::string> registeredProtocols() const;

private:
    ProtocolRegistry() = default;
    std::unordered_map<std::string, AdapterFactory> m_factories;
};
