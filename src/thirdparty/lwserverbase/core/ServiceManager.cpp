/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ServiceManager.cpp
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include <core/ServiceManager.h>
#include <core/ServiceBase.h>
#include <config/ConfigManager.h>
#include <logging/Logger.h>
#include <metrics/MetricsCollector.h>
#include <process/ProcessController.h>
#include <signal.h>
#include <lwcomm/lwcomm.h>
#include <cstdio>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winsvc.h>   // StartServiceCtrlDispatcherA, RegisterServiceCtrlHandlerExA
#endif

// SCM service name storage (always available, even on Linux — the header declarations
// of SetScmServiceName/GetScmServiceName are not #ifdef-guarded, so the storage must
// exist on all platforms to satisfy the linker).
namespace { std::string& scm_name_storage() { static std::string n; return n; } }

#if defined(_WIN32)
namespace {
// SCM machinery. ServiceMain runs on an SCM-created thread; these file-scope
// statics hand it the ServiceManager* and argv captured by RunService on the main
// thread. Valid for the whole service lifetime (RunService blocks in the dispatcher
// until ServiceMain force-terminates the process).
SERVICE_STATUS_HANDLE g_status_handle = nullptr;
lwserverbase::core::ServiceManager* g_sm = nullptr;
int                   g_argc          = 0;
char**                g_argv          = nullptr;

void report_status(DWORD state, DWORD wait_hint_ms = 0) {
    if (!g_status_handle) return;
    SERVICE_STATUS st{};
    st.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    st.dwCurrentState     = state;
    // Accept STOP/SHUTDOWN only while RUNNING (not during start/stop-pending).
    st.dwControlsAccepted = (state == SERVICE_RUNNING)
        ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    st.dwWin32ExitCode = NO_ERROR;
    st.dwCheckPoint    = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : 1;
    st.dwWaitHint      = wait_hint_ms;
    ::SetServiceStatus(g_status_handle, &st);
}

DWORD WINAPI scm_ctrl_handler(DWORD control, DWORD /*event_type*/,
                              LPVOID /*event_data*/, LPVOID /*ctx*/) {
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        report_status(SERVICE_STOP_PENDING, 5000);
        // Reuse the existing exit path: TriggerExit sets m_shouldQuit ->
        // WaitForExitSignal's 100ms poll returns -> HandleSignals returns ->
        // RunLifecycle continues to Stop/Cleanup.
        if (g_sm && g_sm->GetProcessController()) {
            g_sm->GetProcessController()->TriggerExit();
        }
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

VOID WINAPI scm_service_main(DWORD argc, LPSTR* argv) {
    // SCM passes the registered service name in argv[0]; register the handler under
    // that exact name (must match the `sc create` name in deploy-helpers).
    const char* svc_name = (argc > 0 && argv && argv[0]) ? argv[0] : "aegis_service";
    g_status_handle = ::RegisterServiceCtrlHandlerExA(svc_name, scm_ctrl_handler, nullptr);
    if (!g_status_handle) return;
    report_status(SERVICE_START_PENDING, 5000);
    // Initialize -> Start -> report RUNNING (accept STOP) -> block in HandleSignals
    // until CtrlHandler triggers exit -> Stop -> Cleanup.
    (void)g_sm->RunLifecycle(g_argc, g_argv, /*report_running=*/true);
    report_status(SERVICE_STOPPED);
    // Bypass VSOA SDK DLL_PROCESS_DETACH which blocks ExitProcess (same pattern as
    // win_service.h). SCM already sees STOPPED; OS reclaims resources.
    ::TerminateProcess(::GetCurrentProcess(), 0);
}
}  // namespace
#endif  // _WIN32

namespace lwserverbase {
namespace core {

ServiceManager::ServiceManager() 
    : m_service(nullptr)
    , m_configManager(nullptr)
    , m_logger(nullptr)
    , m_metricsCollector(nullptr)
    , m_processController(nullptr)
    , m_argc(0)
    , m_argv(nullptr)
    , m_running(false) {
}

ServiceManager::~ServiceManager() {
    Cleanup();
}

int ServiceManager::Start() {
    if (m_running) {
        return 0;
    }
    
    // 启动服务
    int result = m_service->OnStart(m_argc, m_argv);
    if (result != 0) {
        return result;
    }
    
    m_running = true;
    return 0;
}

void ServiceManager::Stop() {
    if (!m_running) {
        if (m_logger) m_logger->Debug("[STOP] ServiceManager::Stop: already stopped");
        return;
    }

    if (m_logger) m_logger->Debug("[STOP] ServiceManager::Stop: calling m_service->OnStop()...");
    // 停止服务
    m_service->OnStop();
    if (m_logger) m_logger->Debug("[STOP] ServiceManager::Stop: OnStop() returned, m_running=false");
    m_running = false;
}

void ServiceManager::ReloadConfig() {
    if (m_configManager) {
        m_configManager->Load(m_configPath);
    }
    m_service->OnReloadConfig();
}

bool ServiceManager::ExecuteCommand(const std::string& cmd, const std::string& args, std::string& resp) {
    return m_service->OnCommand(cmd, args, resp);
}

void ServiceManager::RegisterConfigManager(std::unique_ptr<ConfigManager> configManager) {
    m_configManager = std::move(configManager);
}

void ServiceManager::RegisterLogger(std::unique_ptr<Logger> logger) {
    m_logger = std::move(logger);
}

void ServiceManager::RegisterMetricsCollector(std::unique_ptr<MetricsCollector> metricsCollector) {
    m_metricsCollector = std::move(metricsCollector);
}

void ServiceManager::RegisterProcessController(std::unique_ptr<ProcessController> processController) {
    m_processController = std::move(processController);
}

ConfigManager* ServiceManager::GetConfigManager() const {
    return m_configManager.get();
}

Logger* ServiceManager::GetLogger() const {
    return m_logger.get();
}

MetricsCollector* ServiceManager::GetMetricsCollector() const {
    return m_metricsCollector.get();
}

ProcessController* ServiceManager::GetProcessController() const {
    return m_processController.get();
}

ServiceBase* ServiceManager::GetService() const {
    return m_service;
}

void ServiceManager::SetService(ServiceBase* service) {
    m_service = service;
}

int ServiceManager::Initialize(int argc, char* argv[]) {
    m_argc = argc;
    m_argv = argv;
    
    // 初始化默认组件
    if (!m_configManager) {
        m_configManager = std::make_unique<config::ConfigManager>();
        m_configManager->Load(m_configPath);
    }
    
    if (!m_logger) {
        m_logger = std::make_unique<logging::Logger>();
        std::string logPath = m_configManager->Get("log.path", std::string("logs"));
        std::string logFileName = m_configManager->Get("log.file", std::string("service.log"));
        std::string logFullPath = logPath + "/" + logFileName;
        m_logger->Initialize(logFullPath);
    }
    
    if (!m_metricsCollector) {
        m_metricsCollector = std::make_unique<metrics::MetricsCollector>();
    }
    
    if (!m_processController) {
        m_processController = std::make_unique<process::ProcessController>();
        std::string defaultLock = m_service ? m_service->GetServiceName() + ".lock" : "service.lock";
        std::string lockFile = m_configManager->Get("process.lock_file", defaultLock);
        if (!m_processController->EnsureSingleInstance(lockFile)) {
            return -1;
        }
        
        bool daemonMode = m_configManager->Get("process.daemon", false);
        if (daemonMode) {
            m_processController->SetDaemonMode(true);
            m_processController->Daemonize();
        }
    }
    
    return 0;
}

void ServiceManager::Cleanup() {
    if (m_logger) m_logger->Debug("[CLEANUP] ServiceManager::Cleanup: destroying processController...");
    m_processController.reset();
    if (m_logger) m_logger->Debug("[CLEANUP] ServiceManager::Cleanup: destroying metricsCollector...");
    m_metricsCollector.reset();
    // logger 即将被销毁，之后的诊断信息改用 fprintf(stderr, ...)
    fprintf(stderr, "[CLEANUP] ServiceManager::Cleanup: destroying logger...\n");
    m_logger.reset();
    fprintf(stderr, "[CLEANUP] ServiceManager::Cleanup: destroying configManager...\n");
    m_configManager.reset();
    fprintf(stderr, "[CLEANUP] ServiceManager::Cleanup: done\n");
}

void ServiceManager::HandleSignals() {
    if (m_logger) m_logger->Debug("[SIGNALS] HandleSignals: waiting for exit signal...");
    if (m_processController) {
        m_processController->WaitForExitSignal();
    } else {
        if (m_logger) m_logger->Debug("[SIGNALS] HandleSignals: no processController, using fallback");
        // 如果没有进程控制器，使用默认的信号处理
        while (m_running) {
            LWComm::Sleep(1000);
        }
    }
    if (m_logger) m_logger->Debug("[SIGNALS] HandleSignals: returning, shutdown will proceed");
}

std::string ServiceManager::GetScmServiceName() { return scm_name_storage(); }
void ServiceManager::SetScmServiceName(const std::string& name) { scm_name_storage() = name; }

int ServiceManager::RunLifecycle(int argc, char** argv, bool report_running) {
    if (Initialize(argc, argv) != 0) return -1;
    if (Start() != 0) { Cleanup(); return -1; }
#if defined(_WIN32)
    if (report_running) report_status(SERVICE_RUNNING);
#else
    (void)report_running;
#endif
    HandleSignals();
    Stop();
    Cleanup();
    return 0;
}

#if defined(_WIN32)
/*
 * SEH-safe wrapper for StartServiceCtrlDispatcherA.
 * The RPC call inside it may raise a structured exception on recent Windows
 * instead of returning FALSE, and __try/__except must live in a function
 * with no C++ automatic objects (C2712).  The rest of RunService contains
 * std::string etc. and can't use __try directly.
 */
static void SCM_TRY_DISPATCH(const SERVICE_TABLE_ENTRYA *table, BOOL *out) {
    __try {
        *out = ::StartServiceCtrlDispatcherA(table);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = FALSE;
    }
}
#endif

int ServiceManager::RunService(int argc, char** argv) {
#if defined(_WIN32)
    // SCM services launch with CWD = System32; NSSM used to set AppDirectory for
    // Sense. Force CWD = exe dir so relative config/lock-file paths resolve next to
    // the binary (mirrors the dist/bin layout build-all produces).
    char exe_path[MAX_PATH];
    if (::GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        std::string p(exe_path);
        std::string::size_type slash = p.find_last_of("\\/");
        if (slash != std::string::npos) {
            p.resize(slash);
            ::SetCurrentDirectoryA(p.c_str());
        }
    }

    std::string name = GetScmServiceName();
    if (name.empty()) name = m_service ? m_service->GetServiceName() : "aegis_service";

    g_sm   = this;
    g_argc = argc;
    g_argv = argv;
    SERVICE_TABLE_ENTRYA table[2];
    table[0].lpServiceName = const_cast<LPSTR>(name.c_str());
    table[0].lpServiceProc = scm_service_main;
    table[1].lpServiceName = nullptr;
    table[1].lpServiceProc = nullptr;
    // Service mode: blocks until ServiceMain returns (it TerminateProcess'es the
    // process, so this return is unreachable in service mode).
    // On recent Windows, the RPC call inside StartServiceCtrlDispatcherA may raise
    // a structured exception instead of returning FALSE on error — isolate SEH in
    // a helper to avoid C2712 (cannot __try in a function with C++ unwinding).
    {
        BOOL scm_ok = FALSE;
        SCM_TRY_DISPATCH(table, &scm_ok);
        if (scm_ok) {
            return 0;
        }
    }
    if (::GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        return 1;   // unexpected dispatcher error
    }
    // else: launched from console/dev, not by SCM -> fall through to console path.
#endif
    return RunLifecycle(argc, argv, /*report_running=*/false);
}

} // namespace core
} // namespace lwserverbase
