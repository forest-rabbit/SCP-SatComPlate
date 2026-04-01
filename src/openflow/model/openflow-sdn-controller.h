/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Blake Hurd  <naimorai@gmail.com>
 */
#ifndef OPENFLOW_SDN_CONTROLLER_H
#define OPENFLOW_SDN_CONTROLLER_H

// #include <assert.h>
// #include <errno.h>

// // Include OFSI code
// #include "ns3/simulator.h"
// #include "ns3/log.h"

#include "ns3/boolean.h"
#include "ns3/mac48-address.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "openflow-interface.h"
#include "openflow-packet.h"
#include "openflow-switch-net-device.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace ns3 {

namespace ofi{
class MasterController;
class SlaveController;

enum struct node_type{
  MasterController,   // 主控制器
  BackupMasterController,   // 备份主控制器
  GateWay,            // 地面站
  SlaveController,    // 子控制器
  Node                // 卫星节点
};

struct node_info{
  uint8_t global_id;      // 节点全局id，比如主控制器ID
  node_type type;         // 节点类型
  Ipv4Address addr;       // 节点的IPv4地址
  Mac48Address macaddr;   // 节点的mac地址

  void PrintInfo() const {
    std::cout << "**********************************" << std::endl;
    std::cout << "Node ID: " << static_cast<unsigned int>(global_id) << std::endl;
    std::string typeStr;
    switch (type) {
      case node_type::MasterController:
        typeStr = "MasterController";
        break;
      case node_type::BackupMasterController:
        typeStr = "BackupMasterController";
        break;
      case node_type::SlaveController:
        typeStr = "SlaveController";
        break;
      case node_type::GateWay:
        typeStr = "GateWay";
        break;
      case node_type::Node:
        typeStr = "Satellite node";
        break;
    }
    std::cout << "Node Type: " << typeStr << std::endl;
    std::cout << "IPv4 Address: " << addr << std::endl;
    std::cout << "MAC Address: " << macaddr << std::endl;
    std::cout << "**********************************" << std::endl;
  }

};

// 自定义 Mac48Address 的哈希函数
struct Mac48AddressHash
{
  std::size_t operator()(const Mac48Address &mac) const
  {
    uint8_t buffer[6];
    mac.CopyTo(buffer);
    std::size_t hash = 0;
    for (int i = 0; i < 6; ++i)
    {
      hash = (hash << 8) ^ buffer[i];
    }
    return hash;
  }
};

// 自定义 Mac48Address 的相等比较函数
struct Mac48AddressEqual
{
  bool operator()(const Mac48Address &lhs, const Mac48Address &rhs) const
  {
    return lhs == rhs;
  }
};

// 主控制器
class MasterController : public Controller
{
public:
  Ptr<Node> m_node;
  void ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer);
  void SetNode(Ptr<Node> node);

protected:
  Time m_expirationTime;                ///< Time it takes for learned MAC state entry/created flow to expire.

};

// 从控制器
class SlaveController : public Controller
{
public:
  Ptr<Node> m_node;
  void ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer);
  void SetNode(Ptr<Node> node);

protected:
  Time m_expirationTime;                ///< Time it takes for learned MAC state entry/created flow to expire.
};

}

}

#endif /* OPENFLOW_SDN_CONTROLLER_H */
