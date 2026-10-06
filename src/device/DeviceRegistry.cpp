#include "device/DeviceRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace {

std::string trim(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first >= last ? std::string() : std::string(first, last);
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int defaultPort(const std::string& protocol)
{
    if (protocol == "ftp") return 21;
    if (protocol == "sftp" || protocol == "ssh") return 22;
    if (protocol == "telnet") return 23;
    if (protocol == "modbus") return 502;
    if (protocol == "opcua") return 4840;
    return 0;
}

DeviceEndpoint normalizeEndpoint(DeviceEndpoint endpoint)
{
    endpoint.protocol = lowercase(trim(endpoint.protocol));
    endpoint.ip = lowercase(trim(endpoint.ip));
    endpoint.credentialRef = trim(endpoint.credentialRef);
    if (endpoint.port <= 0) endpoint.port = defaultPort(endpoint.protocol);
    return endpoint;
}

std::string fnv1a64(const std::string& value)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

void appendUnique(std::vector<std::string>& values, const std::string& value)
{
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

} // namespace

std::string endpointKey(const DeviceEndpoint& endpoint)
{
    const auto normalized = normalizeEndpoint(endpoint);
    return normalized.protocol + "://" + normalized.ip + ":" + std::to_string(normalized.port);
}

DeviceInfo toDeviceInfo(const DeviceProfile& profile, const DeviceEndpoint& endpoint)
{
    const auto normalized = normalizeEndpoint(endpoint);
    DeviceInfo device;
    device.ip = normalized.ip;
    device.port = normalized.port;
    device.protocol = normalized.protocol;
    device.alias = profile.name.empty()
        ? normalized.ip + ":" + std::to_string(normalized.port)
        : profile.name;
    device.note = profile.note;
    return device;
}

DeviceProfile fromDeviceInfo(const DeviceInfo& device)
{
    DeviceProfile profile;
    profile.name = device.alias;
    profile.note = device.note;
    profile.endpoints.push_back({device.protocol, device.ip, device.port, {}});
    DeviceRegistry registry;
    return registry.upsert(profile);
}

DeviceProfile DeviceRegistry::normalize(const DeviceProfile& profile)
{
    DeviceProfile normalized = profile;
    normalized.deviceId = trim(normalized.deviceId);
    normalized.name = trim(normalized.name);
    normalized.note = trim(normalized.note);

    std::vector<std::string> tags;
    for (const auto& tag : normalized.tags) appendUnique(tags, trim(tag));
    normalized.tags = std::move(tags);

    std::vector<DeviceEndpoint> endpoints;
    std::unordered_set<std::string> keys;
    for (auto endpoint : normalized.endpoints) {
        endpoint = normalizeEndpoint(std::move(endpoint));
        const auto key = endpointKey(endpoint);
        if (keys.insert(key).second) endpoints.push_back(std::move(endpoint));
    }
    normalized.endpoints = std::move(endpoints);
    return normalized;
}

std::string DeviceRegistry::stableId(const DeviceProfile& profile)
{
    std::vector<std::string> keys;
    keys.reserve(profile.endpoints.size());
    for (const auto& endpoint : profile.endpoints) keys.push_back(endpointKey(endpoint));
    std::sort(keys.begin(), keys.end());
    std::string identity;
    for (const auto& key : keys) identity += key + "\n";
    if (identity.empty()) identity = "name:" + profile.name;
    return "device-" + fnv1a64(identity);
}

DeviceProfile DeviceRegistry::upsert(const DeviceProfile& profile)
{
    auto incoming = normalize(profile);
    std::size_t index = m_profiles.size();

    if (!incoming.deviceId.empty()) {
        const auto it = std::find_if(m_profiles.begin(), m_profiles.end(), [&incoming](const DeviceProfile& item) {
            return item.deviceId == incoming.deviceId;
        });
        if (it != m_profiles.end()) index = static_cast<std::size_t>(std::distance(m_profiles.begin(), it));
    } else {
        for (const auto& endpoint : incoming.endpoints) {
            const auto key = endpointKey(endpoint);
            const auto it = std::find_if(m_profiles.begin(), m_profiles.end(), [&key](const DeviceProfile& item) {
                return std::any_of(item.endpoints.begin(), item.endpoints.end(), [&key](const DeviceEndpoint& existing) {
                    return endpointKey(existing) == key;
                });
            });
            if (it != m_profiles.end()) {
                index = static_cast<std::size_t>(std::distance(m_profiles.begin(), it));
                break;
            }
        }
    }

    if (index == m_profiles.size()) {
        if (incoming.deviceId.empty()) incoming.deviceId = stableId(incoming);
        m_profiles.push_back(std::move(incoming));
        rebuildWarnings();
        return m_profiles.back();
    }

    auto& existing = m_profiles[index];
    if (!incoming.name.empty()) existing.name = incoming.name;
    if (!incoming.note.empty()) existing.note = incoming.note;
    for (const auto& tag : incoming.tags) appendUnique(existing.tags, tag);
    for (const auto& endpoint : incoming.endpoints) {
        const auto key = endpointKey(endpoint);
        const auto same = std::find_if(existing.endpoints.begin(), existing.endpoints.end(), [&key](const DeviceEndpoint& item) {
            return endpointKey(item) == key;
        });
        if (same == existing.endpoints.end()) {
            existing.endpoints.push_back(endpoint);
        } else if (!endpoint.credentialRef.empty()) {
            same->credentialRef = endpoint.credentialRef;
        }
    }
    rebuildWarnings();
    return existing;
}

std::optional<DeviceProfile> DeviceRegistry::find(const std::string& deviceId) const
{
    const auto it = std::find_if(m_profiles.begin(), m_profiles.end(), [&deviceId](const DeviceProfile& profile) {
        return profile.deviceId == deviceId;
    });
    if (it == m_profiles.end()) return std::nullopt;
    return *it;
}

std::vector<DeviceProfile> DeviceRegistry::list() const
{
    return m_profiles;
}

const std::vector<std::string>& DeviceRegistry::warnings() const
{
    return m_warnings;
}

void DeviceRegistry::rebuildWarnings()
{
    m_warnings.clear();
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> warned;
    for (const auto& profile : m_profiles) {
        if (profile.name.empty()) continue;
        if (!seen.insert(profile.name).second && warned.insert(profile.name).second)
            m_warnings.push_back("设备名称重复：" + profile.name);
    }
}
