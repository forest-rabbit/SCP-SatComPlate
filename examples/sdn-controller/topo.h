#ifndef TOPO_H
#define TOPO_H

#include "ns3/node-container.h"
#include "all-node.h"
#include "cluster.h"
#include "swtch.h"
#include "satrouting.h"
#include "ns3/log.h"
#include <cstdint>
#include <ns3/ipv4-address.h>
#include <ns3/mac48-address.h>

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
  extern NodeContainer mcs;         // 主控制器节点0， 1 
  extern NodeContainer scs;         // 从控制器节点
  extern NodeContainer gwss;        // 地面站Gateway Station 5个
  extern NodeContainer slavescs;    // 从控制器集合
  extern NodeContainer sates;       // 所有卫星节点
  extern std::vector<NodeContainer> sateNodes;  // 所有卫星节点，按照轨道和轨道内的卫星排列
  extern std::vector<NodeContainer> satClusterNodes;   // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首 
  extern std::vector<uint32_t>      NodesSlaveID;  // 保存所有卫星节点中子控制器ID，按照node.GetID()来索引元素
  extern std::vector<uint32_t>      changeClusterID;  // 记录所有卫星节点簇ID变化次数

  // extern uint32_t sates_num;   // N: LEO卫星总数
  // extern uint32_t orbit_num;   // No: 轨道数
  // extern uint32_t sate_num;  // Ns:每个轨道上卫星数量

  extern int masterID;    // 主控制器ID

  extern Ptr<ns3::ofi::MasterController> MasterController;  // 主控制器节点
  extern Ptr<ns3::ofi::MasterController> BackupMasterController;  // 备份主控制器节点
  extern std::vector<Ptr<ns3::ofi::SlaveController>> SlavesControllers;  // 从控制器节点 

  extern NetDeviceContainer SwitchDevices;      // openflow交换机
  extern NetDeviceContainer allswitchDevices;   // csma交换机

  extern bool toMasterflag;

  extern ofi::node_info master_node_info;
  extern ofi::node_info backup_master_node_info;
  extern std::vector<ofi::node_info> gws_node_info;
  extern std::vector<ofi::node_info> slaves_node_info;

  extern std::unordered_map<Mac48Address, ofi::Ipv4Inter, ofi::Mac48AddressHash, ofi::Mac48AddressEqual> MacMaps;
  extern std::unordered_map<Ipv4Address, uint32_t, Ipv4AddressHash, Ipv4AddressEqual> ipv4AddrMaps;

  void initTopo();
  void initTopoSim(string dataArray);
  void updateTopoSim(string dataArray, string& retArray);
  void updateRouteSim(string dataArray, string& retArray);
  void deleteRouteSim(string dataArray);
  void updateClusterSim(string& retArray);
  void updateTopo();
  void updateRou();
  void rouReconvergence();
  void master_migration();
}

# endif