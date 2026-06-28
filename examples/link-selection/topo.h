#ifndef TOPO_H
#define TOPO_H

#include "ns3/node-container.h"
#include "jsontopo/topo-data.h"
#include "cluster.h"
#include "ns3/log.h"
#include <cstdint>
#include <ns3/ipv4-address.h>
#include <ns3/mac48-address.h>
#include "satrouting.h"  
#include <string>
#include <vector>

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

namespace ns3 {
  extern NodeContainer sates;       // 所有卫星节点
  extern NodeContainer Gnodes;      // 地面网络节点
  extern NodeContainer topoNodes;   // JSON拓扑中的所有节点，包含卫星和地面站
  extern std::vector<NodeContainer> sateNodes;  // 所有卫星节点，按照轨道和轨道内的卫星排列
  extern std::vector<NodeContainer> satClusterNodes;   // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首 
  extern std::vector<uint32_t>      changeClusterID;  // 记录所有卫星节点簇ID变化次数
  extern std::vector<TopologyNodeInfo> topoNodeInfos;
  extern std::string nodesJsonFile;
  extern std::string topologyJsonFile;
  extern std::string timeSlicesJsonFile;


  void initTopo();
  void print_node_info();
  void updateTopo();

  std::vector<LinkInfo> ReadTopologyFile(const std::string& filename);
  void BuildNetworkTopology(NodeContainer& satellites,  const std::vector<LinkInfo>& links);
  void print_adjacency_list(NodeContainer allnodes);
}

# endif
