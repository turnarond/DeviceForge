#pragma once

#include <string>
#include <atomic>
#include <lwevent/lwevent.h>

namespace lwserverbase {
namespace process {

class ProcessController {
public:
    ProcessController();
    ~ProcessController();
    
    bool EnsureSingleInstance(const std::string& lockFile);
    bool ShouldQuit() const;
    bool ConsoleAvailable() const;
    void SetDaemonMode(bool daemon);
    bool Daemonize();
    void WaitForExitSignal();
    void TriggerExit();

private:
    static void SignalHandler(int signum);
    void InitializeSignalHandling();

public:
    static ProcessController* s_instance;

private:
    std::atomic<bool> m_shouldQuit;
    bool m_daemonMode;
    void* m_lockFile;
    EVENT_HANDLE m_exitEvent;
};

} // namespace process
} // namespace lwserverbase
