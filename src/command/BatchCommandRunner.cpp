#include "command/BatchCommandRunner.h"

#include "adapter/ProtocolRegistry.h"

#include <algorithm>
#include <chrono>
#include <cctype>

namespace {

std::string normalizedProtocol(std::string protocol)
{
    std::transform(protocol.begin(), protocol.end(), protocol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return protocol == "ssh" ? "ssh" : "telnet";
}

std::string deviceKey(const DeviceInfo& device, int port)
{
    return device.ip + ":" + std::to_string(port);
}

} // namespace

BatchCommandRunner::BatchCommandRunner(AdapterFactory factory)
    : m_factory(std::move(factory))
{
    if (!m_factory) {
        m_factory = [](const std::string& protocol) {
            return ProtocolRegistry::instance()->create(protocol);
        };
    }
}

int BatchCommandRunner::defaultPort(const std::string& protocol)
{
    return normalizedProtocol(protocol) == "ssh" ? 22 : 23;
}

bool BatchCommandRunner::isRetryableError(const std::string& error)
{
    std::string lower = error;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("reset") != std::string::npos
        || lower.find("timeout") != std::string::npos
        || lower.find("timed out") != std::string::npos
        || lower.find("connection closed") != std::string::npos
        || lower.find("couldn't connect") != std::string::npos;
}

CommandBatchResult BatchCommandRunner::run(const CommandRequest& request,
                                           std::atomic_bool& cancelled,
                                           const Callbacks& callbacks) const
{
    CommandBatchResult result;
    const std::string protocol = normalizedProtocol(request.protocol);
    const int timeoutMs = std::max(1, request.timeoutSec) * 1000;

    for (const auto& sourceDevice : request.devices) {
        CommandDeviceResult deviceResult;
        const int port = sourceDevice.port > 0 ? sourceDevice.port : defaultPort(protocol);
        deviceResult.deviceKey = deviceKey(sourceDevice, port);

        if (cancelled.load()) {
            deviceResult.state = CommandResultState::Cancelled;
            result.devices.push_back(deviceResult);
            if (callbacks.onDeviceResult) callbacks.onDeviceResult(deviceResult);
            continue;
        }
        if (request.commands.empty()) {
            deviceResult.state = CommandResultState::Rejected;
            deviceResult.error = "命令列表为空";
            result.devices.push_back(deviceResult);
            if (callbacks.onDeviceResult) callbacks.onDeviceResult(deviceResult);
            continue;
        }

        DeviceInfo device = sourceDevice;
        device.port = port;
        std::string lastError;
        bool completed = false;
        for (int attempt = 0; attempt <= std::max(0, request.retryCount); ++attempt) {
            if (cancelled.load()) {
                deviceResult.state = CommandResultState::Cancelled;
                break;
            }
            auto adapter = m_factory(protocol);
            if (!adapter) {
                lastError = "协议适配器不可用";
                break;
            }
            AuthInfo auth = request.auth;
            if (!adapter->connect(device, auth)) {
                lastError = adapter->lastError();
                auth.clear();
                if (!isRetryableError(lastError) || attempt == request.retryCount)
                    break;
                ++deviceResult.retryCount;
                continue;
            }
            auth.clear();

            bool allOk = true;
            for (const auto& command : request.commands) {
                if (cancelled.load()) {
                    allOk = false;
                    deviceResult.state = CommandResultState::Cancelled;
                    break;
                }
                Request commandRequest;
                commandRequest.path = command;
                commandRequest.timeoutMs = timeoutMs;
                const Response response = adapter->request(commandRequest).get();
                if (!response.success) {
                    allOk = false;
                    lastError = response.errorMessage;
                    break;
                }
                ++deviceResult.commandsExecuted;
                deviceResult.output += response.data;
            }
            adapter->disconnect();
            if (deviceResult.state == CommandResultState::Cancelled)
                break;
            if (allOk) {
                completed = true;
                deviceResult.rebootTriggered = request.rebootMode;
                deviceResult.state = request.rebootMode
                    ? CommandResultState::RebootTriggered
                    : CommandResultState::Succeeded;
                break;
            }
            if (!isRetryableError(lastError) || attempt == request.retryCount)
                break;
            ++deviceResult.retryCount;
            deviceResult.commandsExecuted = 0;
            deviceResult.output.clear();
        }

        if (!completed && deviceResult.state != CommandResultState::Cancelled) {
            deviceResult.state = CommandResultState::Failed;
            deviceResult.error = lastError.empty() ? "命令执行失败" : lastError;
        }
        result.devices.push_back(deviceResult);
        if (callbacks.onDeviceResult) callbacks.onDeviceResult(deviceResult);
    }

    if (callbacks.onFinished) callbacks.onFinished(result);
    return result;
}
