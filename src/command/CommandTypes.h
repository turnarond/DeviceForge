#pragma once

#include "adapter/IProtocolAdapter.h"

#include <functional>
#include <string>
#include <vector>

enum class CommandResultState {
    Succeeded,
    Failed,
    Rejected,
    Cancelled,
    RebootTriggered
};

struct CommandRequest {
    std::string protocol = "telnet";
    std::vector<DeviceInfo> devices;
    AuthInfo auth;
    std::vector<std::string> commands;
    int timeoutSec = 10;
    int retryCount = 1;
    bool rebootMode = false;
};

struct CommandDeviceResult {
    std::string deviceKey;
    CommandResultState state = CommandResultState::Failed;
    int commandsExecuted = 0;
    int retryCount = 0;
    int elapsedMs = 0;
    bool rebootTriggered = false;
    std::string output;
    std::string error;
};

struct CommandBatchResult {
    std::vector<CommandDeviceResult> devices;
};

struct RebootOptions {
    std::string protocol = "telnet";
    std::string command = "reboot";
    int timeoutSec = 10;
    int retryCount = 1;
};
