#ifndef CLUSTER_H
#define CLUSTER_H

#include "ns3/boolean.h"
#include "ns3/integer.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include <cstdint>
#include <ns3/node.h>
#include "ns3/cluster-module.h"
#include "all-node.h"
#include "para.h"
#include "access.h"

namespace ns3{
    extern SatCluster sat;
    extern std::vector<Ptr<LinkUtilizationMonitor>> monitors;
    extern std::vector<NodeContainer> satClusterNodes;          // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首
    void ActiveCluster(NodeContainer node, std::vector<NodeContainer> &nodes);
    void UpdateCluster(NodeContainer node);
    void UseMaxUtilization(const vector<Ptr<LinkUtilizationMonitor>> &monitors, NodeContainer node, const vector<vector<int>> &adj, vector<NodeContainer> &nodes);
    void RelectCluster(uint32_t breakID,  NodeContainer node, vector<NodeContainer> &nodes);
}

#endif