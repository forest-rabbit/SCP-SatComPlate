#ifndef TOPO_H
#define TOPO_H

#include "ns3/node-container.h"
#include "cluster.h"
#include "ns3/log.h"
#include <cstdint>
#include <ns3/ipv4-address.h>
#include <ns3/mac48-address.h>
#include "satrouting.h"  

struct Ipv4AddressHash
{
  std::size_t operator()(const Ipv4Address &addr) const
  {
    return addr.Get();
  }
};

struct Ipv4AddressEqual
{
  bool operator()(const Ipv4Address &lhs, const Ipv4Address &rhs) const
  {
    return lhs == rhs;
  }
};

// 链路信息结构体
struct LinkInfo {
    uint32_t source;
    uint32_t destination;
    uint32_t delay_ms;
    uint32_t bandwidth_gbps;
};

namespace ns3 {
  extern NodeContainer sates;       // 所有卫星节点
  extern std::vector<NodeContainer> sateNodes;  // 所有卫星节点，按照轨道和轨道内的卫星排列
  extern std::vector<NodeContainer> satClusterNodes;   // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首 
  extern std::vector<uint32_t>      changeClusterID;  // 记录所有卫星节点簇ID变化次数


  void initTopo();
  void print_node_info();
  void updateTopo();

  std::vector<LinkInfo> ReadTopologyFile(const std::string& filename);
  void BuildNetworkTopology(NodeContainer& satellites,  const std::vector<LinkInfo>& links);
  void print_adjacency_list(NodeContainer allnodes);
}

# endif