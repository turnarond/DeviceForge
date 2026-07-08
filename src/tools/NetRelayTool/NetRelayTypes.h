/*
 * NetRelayTypes.h — NetRelayTool 共享类型与 .nrec 格式常量
 */
#pragma once
#include <cstdint>

// 中继协议类型
enum class RelayProtocol { Tcp, Udp };

// 中继方向：Upstream = 生产者→消费者；Downstream = 消费者→生产者
enum class RelayDirection { Upstream, Downstream };

// .nrec 录制文件格式常量（详见 spec §4）
namespace nrec {
    constexpr char       kMagic[4]   = { 'N', 'R', 'E', 'C' };
    constexpr uint16_t   kVersion    = 1;
    constexpr int        kHeaderSize = 32;   // 固定文件头字节数
}
