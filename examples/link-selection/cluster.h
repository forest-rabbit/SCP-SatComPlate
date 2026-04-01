#ifndef CLUSTER_H
#define CLUSTER_H

#include "ns3/boolean.h"
#include "ns3/integer.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include <cstdint>
#include <ns3/node.h>
#include "ns3/cluster-module.h"
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
    // 基于轨道划分的分簇（按轨道索引分组）
    void OrbitPartitionCluster(NodeContainer node, uint32_t satPerOrbit, std::vector<NodeContainer> &nodes);
    // 基于图连接性的分簇（连通分量），可选簇规模约束
    void ConnectivityPartition(NodeContainer node, std::vector<NodeContainer> &nodes, uint32_t minSize = 0, uint32_t maxSize = 0);
    // 连通性分簇的定时回调
    void ConnectivityClusterTick(NodeContainer node, uint32_t minSize = 0, uint32_t maxSize = 0);
    // 轨道分簇的定时回调
    void OrbitClusterTick(NodeContainer node);
}

#endif