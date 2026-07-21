/*
 * tst_opcua_loopback.cpp — 本机环回隔离测试：进程内起一个 open62541 服务端
 * （None 安全 + 匿名），再用与 OpcUaAdapter::connect 完全一致的客户端配置去连它。
 *
 * 目的：把"客户端类是否写对" 与 "远端设备 10.13.104.225 的服务实现" 彻底分离。
 *   - 若本地环回连接成功 → 客户端类/库使用方式正确，问题在那台设备的 OPC UA 服务端；
 *   - 若本地环回也失败 → 客户端代码或库使用方式有问题。
 * 全程 127.0.0.1，无任何外部依赖。
 */
#include <open62541.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>

int main() {
    // ---------- 1) 本机 open62541 服务端（None + 匿名）----------
    UA_Server* server = UA_Server_new();
    UA_ServerConfig* sc = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(sc, 48400, NULL);   // 端口 48400，默认 None + 匿名

    UA_StatusCode s = UA_Server_run_startup(server);
    if (s != UA_STATUSCODE_GOOD) {
        printf("server startup failed: %s\n", UA_StatusCode_name(s));
        UA_Server_delete(server);
        return 2;
    }

    std::atomic<bool> running{true};
    std::thread srv([&]{
        while (running.load())
            UA_Server_run_iterate(server, false);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // ---------- 2) 客户端（完全复刻 OpcUaAdapter::connect 的配置）----------
    UA_ClientConfig config;
    memset(&config, 0, sizeof(config));
    config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_INFO);
    config.timeout = 10000;
    UA_ClientConfig_setDefault(&config);           // None 策略 + 匿名，出厂即完整
    UA_Client* client = UA_Client_newWithConfig(&config);

    UA_StatusCode ret = UA_Client_connect(client, "opc.tcp://127.0.0.1:48400");
    printf("\n==== Client connect: %s (0x%08x) ====\n",
           UA_StatusCode_name(ret), (unsigned)ret);

    if (ret == UA_STATUSCODE_GOOD) {
        UA_Variant val; UA_Variant_init(&val);
        UA_StatusCode r = UA_Client_readValueAttribute(
            client, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_CURRENTTIME), &val);
        printf("read Server CurrentTime: %s\n", UA_StatusCode_name(r));
        UA_Variant_clear(&val);
        UA_Client_disconnect(client);
    }

    UA_Client_delete(client);
    running = false;
    srv.join();
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);

    printf("\n结果：%s\n", ret == UA_STATUSCODE_GOOD
        ? "本地环回连接成功 → 客户端类正确，ActivateSession 失败源于 10.13.104.225 的服务端实现"
        : "本地环回也失败 → 客户端代码/库使用方式存在问题");
    return ret == UA_STATUSCODE_GOOD ? 0 : 1;
}
