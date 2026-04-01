#ifndef OPENFLOW_PACKET_H
#define OPENFLOW_PACKET_H

#include <iomanip>
#include "ns3/boolean.h"
#include "ns3/buffer.h"
#include "ns3/core-module.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <ns3/ipv4-address.h>
#include <ns3/mac48-address.h>
#include "ns3/internet-module.h"

namespace ns3{

namespace ofi{

extern uint8_t packetSize;

enum class pktType : uint8_t {
  HEARTBEAT,   // 心跳包
  IDENTITY,    // 身份包
  ACTIVATION,  // 激活包
  Disconnect,  // 失联通告包
  Recovery,    // 恢复通告包
  CheckSatus,  // 状态查询包
  Status,  // 当前节点状态包
  BreakInfo,   // 故障节点信息包
  OTHER
};

class Ipv4Inter{
public:
  Ipv4Address addr;  // ipv4地址
  uint32_t inter;    // 对应的接口
};

// 三种通用packet
class SDNPacket{
public:
  pktType type;
  Mac48Address src;   // 源mac地址，6字节
  Mac48Address dest;  // 目的mac地址，6字节
  uint8_t *packet;
  // size_t packet_size;

  // 构造函数
  // SDNPacket() : type(pktType::OTHER), packet(nullptr), packet_size(0) {}

  // 序列化函数
  void Serialize(uint8_t buffer[], size_t bufferSize) const{
    if (bufferSize < sizeof(type) + packetSize) {
      std::cerr << "Buffer size is too small for serialization" << std::endl;
      return;
    }
    // 将状态信息写入缓冲区
    std::memcpy(buffer, &type, sizeof(pktType));
    std::memcpy(buffer + sizeof(pktType), &src, sizeof(src));
    std::memcpy(buffer + sizeof(pktType) + sizeof(src), &dest, sizeof(dest));
    std::memcpy(buffer + sizeof(pktType) + sizeof(src) + sizeof(dest), packet, packetSize);
  }

  // 解序列化函数
  void Deserialize(const uint8_t buffer[], size_t bufferSize) {
    if (bufferSize < sizeof(type) + sizeof(src) + sizeof(dest)) {
      std::cerr << "Buffer size is too small for deserialization" << std::endl;
      return;
    }
    // 从缓冲区中读取数据
    std::memcpy(&type, buffer, sizeof(type));
    std::memcpy(&src, buffer + sizeof(type), sizeof(src));
    std::memcpy(&dest, buffer + sizeof(type) + sizeof(src), sizeof(dest));

    packetSize = bufferSize - (sizeof(type) + sizeof(src) + sizeof(dest));
    if (packetSize > 0) {
      packet = new uint8_t[packetSize];
      std::memcpy(packet, buffer + sizeof(type) + sizeof(src) + sizeof(dest), packetSize);
    } else {
      packet = nullptr;
    }
  }

  // 析构函数，释放内存
  ~SDNPacket() {
    delete[] packet;
  }

  static std::string GetStatusString(pktType type) {
    switch(type) {
    case pktType::HEARTBEAT:
      return "心跳包";
    case pktType::IDENTITY:
      return "身份包";
    case pktType::ACTIVATION:
      return "激活包";
    case pktType::Disconnect:
      return "失联通告包";
    case pktType::Recovery:
      return "恢复通告包";
    case pktType::CheckSatus:
      return "状态查询包";
    case pktType::Status:
      return "节点信息包";
    case pktType::BreakInfo:
      return "故障节点信息包";
    case pktType::OTHER:
      return "其他";
    }
    return "其他";
  }

};

// 心跳包
class HeartbeatPacket {
public:
  uint8_t controller_id;      // 8位控制器标识符
  uint8_t subcontroller_id;   // 8位目的子控制器ID
  uint8_t timestamp;          // 8位时间戳
  uint8_t sequence_number;    // 8位序列号
  uint8_t status_info;        // 4位状态信息，可选字段，包含控制器的当前状态信息，可选择使用高4位，

  // 序列化函数
  void Serialize(uint8_t buffer[5]) const {
    buffer[0] = this->controller_id;
    buffer[1] = this->subcontroller_id;
    buffer[2] = this->timestamp;
    buffer[3] = this->sequence_number;
    buffer[4] = this->status_info;
  }
};

// 身份包
struct IdentityPacket {
    uint8_t packet_type;               // 4位类型符，明确为身份包
    uint8_t new_master_controller_id;  // 8位新主控制器ID
    uint8_t dest_controller_id; // 8位目的控制器ID
    uint8_t timestamp;                 // 8位时间戳
    uint8_t expiration_period;         // 8位有效期
};

// 激活包
struct ActivationPacket {
    uint8_t packet_type;               // 4位类型符
    uint8_t send_controller_id;     // 8位发送子控制器ID
    uint8_t dest_controller_id; // 8位目的控制器ID
    uint8_t timestamp;                 // 8位时间戳
    uint8_t fau_info;              // 8位故障信息
};

// 失联通告包
struct DisconnectPacket {
    uint8_t packet_type;               // 4位类型符
    uint8_t send_controller_id;        // 8位发送子控制器ID
    uint8_t dest_controller_id;        // 8位目的控制器ID
    uint8_t timestamp;                 // 8位时间戳
    uint8_t fau_info;                  // 8位故障信息
};

// 恢复包
struct RecoveryPacket {
    uint8_t packet_type;               // 4位类型符
    uint8_t send_controller_id;        // 8位发送控制器ID
    uint8_t dest_controller_id;        // 8位目的子控制器ID
    uint8_t timestamp;                 // 8位时间戳
    uint8_t fau_info;                  // 8位故障信息
};


// 状态查询包
struct CheckStatusPacket {
    uint8_t packet_type;               // 4位类型符
    uint8_t send_controller_id;        // 8位发送控制器ID
    uint8_t dest_node_id;        // 8位目的节点ID
    uint8_t timestamp;                 // 8位时间戳
    uint8_t fau_info;                  // 8位故障信息
};

// 当前节点的信息包
struct StatusPacket{
    uint8_t packet_type;    // 4位类型符
    uint8_t src_id;         // 8位源id
    uint8_t dest_id;        // 8位目的ID
    double timestamp;
};

// 故障节点信息包
struct BreakInfoPacket{
    uint8_t packet_type;    // 4位类型符
    uint8_t src_id;         // 8位源id
    uint8_t dest_id;        // 8位目的ID
    uint8_t break_id;       // 8位故障节点ID
    uint8_t timestamp;
};

}

}
# endif