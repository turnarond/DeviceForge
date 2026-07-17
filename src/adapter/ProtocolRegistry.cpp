#include "ProtocolRegistry.h"
#include <algorithm>

ProtocolRegistry* ProtocolRegistry::instance() {
    static ProtocolRegistry reg;
    return &reg;
}

void ProtocolRegistry::registerFactory(const std::string& protocolId,
                                        AdapterFactory factory) {
    m_factories[protocolId] = factory;
}

std::shared_ptr<IProtocolAdapter> ProtocolRegistry::create(
    const std::string& protocolId) const {

    auto it = m_factories.find(protocolId);
    if (it == m_factories.end()) {
        return nullptr;
    }
    return it->second();
}

bool ProtocolRegistry::isRegistered(const std::string& protocolId) const {
    return m_factories.find(protocolId) != m_factories.end();
}

std::vector<std::string> ProtocolRegistry::registeredProtocols() const {
    std::vector<std::string> keys;
    keys.reserve(m_factories.size());
    for (auto& pair : m_factories) {
        keys.push_back(pair.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}
