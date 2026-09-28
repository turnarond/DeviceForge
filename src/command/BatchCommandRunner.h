#pragma once

#include "command/CommandTypes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

class BatchCommandRunner
{
public:
    using AdapterFactory = std::function<std::shared_ptr<IProtocolAdapter>(const std::string&)>;

    struct Callbacks {
        std::function<void(const CommandDeviceResult&)> onDeviceResult;
        std::function<void(const std::string&)> onLog;
        std::function<void(const CommandBatchResult&)> onFinished;
    };

    explicit BatchCommandRunner(AdapterFactory factory = {});

    CommandBatchResult run(const CommandRequest& request,
                           std::atomic_bool& cancelled,
                           const Callbacks& callbacks) const;

    static int defaultPort(const std::string& protocol);
    static bool isRetryableError(const std::string& error);

private:
    AdapterFactory m_factory;
};
