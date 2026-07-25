#include "topo-node-state.h"

#include "../para.h"
#include "../topo.h"

#include "ns3/fatal-error.h"
#include "ns3/node.h"

#include <map>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace ns3 {

static std::unordered_map<uint32_t, uint32_t> g_topologyNodeIndexByExternalId;

static bool
IsGroundStation(const TopologyNodeInfo& info)
{
  return info.node_type == "gs" || info.node_type == "ground";
}

static std::string
JoinNodeIds(const std::vector<uint32_t>& ids)
{
  if (ids.empty())
  {
    return "无";
  }
  std::ostringstream oss;
  for (uint32_t i = 0; i < ids.size(); ++i)
  {
    if (i > 0)
    {
      oss << ",";
    }
    oss << ids[i];
  }
  return oss.str();
}

static std::string
BuildClusterSummary(const std::vector<TopologyNodeInfo>& nodeInfos)
{
  struct ClusterPrintInfo
  {
    std::vector<uint32_t> ground_stations;
    std::vector<uint32_t> node_ids;
  };

  std::map<uint32_t, ClusterPrintInfo> clusters;
  for (const auto& info : nodeInfos)
  {
    if (!info.is_cluster)
    {
      continue;
    }
    ClusterPrintInfo& cluster = clusters[info.cluster_id];
    if (IsGroundStation(info))
    {
      cluster.ground_stations.push_back(info.node_id);
    }
    else
    {
      cluster.node_ids.push_back(info.node_id);
    }
  }

  if (clusters.empty())
  {
    return "无";
  }

  std::ostringstream oss;
  uint32_t index = 0;
  for (const auto& item : clusters)
  {
    if (index > 0)
    {
      oss << std::endl;
    }
    oss << "簇" << item.first
        << " 地面站:" << JoinNodeIds(item.second.ground_stations)
        << " 节点:" << JoinNodeIds(item.second.node_ids);
    ++index;
  }
  return oss.str();
}

static void
PrintClusterSummary(const std::string& indent)
{
  std::istringstream input(BuildClusterSummary(topoNodeInfos));
  std::string line;
  while (std::getline(input, line))
  {
    std::cout << indent << line << std::endl;
  }
}

static uint32_t
ResolveTopologyNodeIndex(uint32_t externalNodeId, bool zeroBasedFallback)
{
  // 优先使用nodes_*.json中声明的node_id映射。
  // fallback仅用于没有节点文件的旧格式链路输入。
  auto it = g_topologyNodeIndexByExternalId.find(externalNodeId);
  if (it != g_topologyNodeIndexByExternalId.end())
  {
    return it->second;
  }
  if (zeroBasedFallback && externalNodeId < topoNodes.GetN())
  {
    return externalNodeId;
  }
  if (!zeroBasedFallback && externalNodeId > 0 && externalNodeId <= topoNodes.GetN())
  {
    return externalNodeId - 1;
  }
  NS_FATAL_ERROR("JSON链路引用了不存在的节点ID: " << externalNodeId);
  return 0;
}

TopologyNodeResolver
MakeTopologyNodeResolver()
{
  return [](uint32_t externalNodeId, bool zeroBasedFallback) {
    return ResolveTopologyNodeIndex(externalNodeId, zeroBasedFallback);
  };
}

bool
HasExplicitTopologyNodeIds()
{
  return !g_topologyNodeIndexByExternalId.empty();
}

std::vector<LinkInfo>
ReadResolvedTopologyLinksJsonFile(const std::string& filename)
{
  return ReadTopologyLinksJsonFile(filename, MakeTopologyNodeResolver(), HasExplicitTopologyNodeIds());
}

TopologyPatchInfo
ReadResolvedTopologyPatchJsonFile(const std::string& filename)
{
  return ReadTopologyPatchJsonFile(filename, MakeTopologyNodeResolver(), HasExplicitTopologyNodeIds());
}

static void
RebuildSateNodesFromFlatContainer()
{
  // JSON模式没有真实轨道信息。这里仅为了兼容旧代码中依赖sateNodes/orbit_num/sate_num的路径。
  sateNodes.clear();
  if (sates.GetN() == 0)
  {
    return;
  }
  sates_num = sates.GetN();
  if (orbit_num == 0 || sates_num % orbit_num != 0)
  {
    orbit_num = 1;
  }
  sate_num = sates_num / orbit_num;
  for (uint32_t i = 0; i < orbit_num; ++i)
  {
    NodeContainer orbitNodes;
    for (uint32_t j = 0; j < sate_num; ++j)
    {
      uint32_t index = i * sate_num + j;
      if (index < sates.GetN())
      {
        orbitNodes.Add(sates.Get(index));
      }
    }
    sateNodes.push_back(orbitNodes);
  }
}

static void
BuildClusterNodesFromJsonInfo()
{
  satClusterNodes.clear();
  std::map<uint32_t, NodeContainer> clusters;
  std::map<uint32_t, Ptr<Node>> heads;
  for (const auto& info : topoNodeInfos)
  {
    if (!info.is_cluster)
    {
      continue;
    }
    Ptr<Node> node = topoNodes.Get(info.node_index);
    clusters[info.cluster_id];
    if (info.is_cluster_head)
    {
      heads[info.cluster_id] = node;
    }
    else
    {
      if (info.node_type == "sat")
      {
        clusters[info.cluster_id].Add(node);
      }
    }
  }
  for (auto& cluster : clusters)
  {
    NodeContainer nodes;
    auto head = heads.find(cluster.first);
    if (head != heads.end())
    {
      nodes.Add(head->second);
    }
    nodes.Add(cluster.second);
    satClusterNodes.push_back(nodes);
  }
}

static std::string
BuildClusterUpdateSummary(const std::vector<TopologyNodeInfo>& updatedInfos)
{
  std::map<uint32_t, TopologyNodeInfo> oldInfos;
  for (const auto& info : topoNodeInfos)
  {
    oldInfos[info.node_id] = info;
  }

  uint32_t clusterChanged = 0;
  uint32_t headChanged = 0;

  for (const auto& info : updatedInfos)
  {
    auto oldIt = oldInfos.find(info.node_id);
    if (oldIt == oldInfos.end())
    {
      continue;
    }

    const TopologyNodeInfo& oldInfo = oldIt->second;
    if (oldInfo.is_cluster != info.is_cluster || oldInfo.cluster_id != info.cluster_id)
    {
      ++clusterChanged;
    }
    if (oldInfo.is_cluster_head != info.is_cluster_head)
    {
      ++headChanged;
    }
  }

  std::ostringstream oss;
  oss << "簇归属变化 : " << clusterChanged << std::endl
      << "簇首变化   : " << headChanged << std::endl
      << BuildClusterSummary(updatedInfos);
  return oss.str();
}

void
CreateNodesFromJsonInfo(const std::vector<TopologyNodeInfo>& nodeInfos)
{
  // 初始化阶段一次性创建所有节点。后续时间片只允许引用这里已经创建过的node_id。
  sates = NodeContainer();
  Gnodes = NodeContainer();
  topoNodes = NodeContainer();
  g_topologyNodeIndexByExternalId.clear();
  topoNodeInfos.clear();

  for (auto info : nodeInfos)
  {
    Ptr<Node> node = CreateObject<Node>();
    info.node_index = topoNodes.GetN();
    topoNodes.Add(node);
    if (g_topologyNodeIndexByExternalId.find(info.node_id) != g_topologyNodeIndexByExternalId.end())
    {
      NS_FATAL_ERROR("JSON节点ID重复: " << info.node_id);
    }
    g_topologyNodeIndexByExternalId[info.node_id] = info.node_index;
    if (info.node_type == "gs" || info.node_type == "ground")
    {
      Gnodes.Add(node);
    }
    else
    {
      sates.Add(node);
    }
    topoNodeInfos.push_back(info);
  }

  RebuildSateNodesFromFlatContainer();
  BuildClusterNodesFromJsonInfo();
  std::cout << "[TOPO:Nodes] 节点创建完成" << std::endl
            << "  satellites : " << sates.GetN() << std::endl
            << "  ground     : " << Gnodes.GetN() << std::endl
            << "  total      : " << topoNodes.GetN() << std::endl
            << std::endl
            << "[TOPO:Clusters] 初始簇信息" << std::endl;
  PrintClusterSummary("  ");
  std::cout << std::endl;
}

TopologyNodeUpdateSummary
ApplyTopologyNodeInfos(const std::vector<TopologyNodeInfo>& nodeInfos)
{
  if (nodeInfos.empty())
  {
    return {"节点文件为空或本时间片未包含节点更新", ""};
  }

  std::vector<TopologyNodeInfo> updatedInfos;
  updatedInfos.reserve(nodeInfos.size());
  for (auto info : nodeInfos)
  {
    // 运行期只更新簇归属/簇首等属性，不创建新ns-3节点。
    auto it = g_topologyNodeIndexByExternalId.find(info.node_id);
    if (it == g_topologyNodeIndexByExternalId.end())
    {
      NS_FATAL_ERROR("运行期节点JSON引用了初始化阶段不存在的节点ID: " << info.node_id);
    }
    info.node_index = it->second;
    updatedInfos.push_back(info);
  }

  std::string clusterSummary = BuildClusterUpdateSummary(updatedInfos);
  topoNodeInfos.assign(updatedInfos.begin(), updatedInfos.end());
  BuildClusterNodesFromJsonInfo();
  return {"已应用节点更新", clusterSummary};
}

TopologyNodeUpdateSummary
ApplyTopologyNodePatches(const std::vector<TopologyNodePatch>& patches)
{
  if (patches.empty())
  {
    return {"未提供节点更新，保持上一状态", ""};
  }

  std::map<uint32_t, uint32_t> nodeInfoIndexById;
  for (uint32_t i = 0; i < topoNodeInfos.size(); ++i)
  {
    nodeInfoIndexById[topoNodeInfos[i].node_id] = i;
  }

  std::vector<TopologyNodeInfo> updatedInfos = topoNodeInfos;
  for (const auto& patch : patches)
  {
    auto indexIt = nodeInfoIndexById.find(patch.node_id);
    if (indexIt == nodeInfoIndexById.end()
        || g_topologyNodeIndexByExternalId.find(patch.node_id) == g_topologyNodeIndexByExternalId.end())
    {
      NS_FATAL_ERROR("运行期节点patch引用了初始化阶段不存在的节点ID: " << patch.node_id);
    }

    TopologyNodeInfo& info = updatedInfos[indexIt->second];
    if (patch.has_is_cluster)
    {
      info.is_cluster = patch.is_cluster;
    }
    if (patch.has_cluster_id)
    {
      info.cluster_id = patch.cluster_id;
    }
    if (patch.has_is_cluster_head)
    {
      info.is_cluster_head = patch.is_cluster_head;
    }
  }

  std::string clusterSummary = BuildClusterUpdateSummary(updatedInfos);
  topoNodeInfos.assign(updatedInfos.begin(), updatedInfos.end());
  BuildClusterNodesFromJsonInfo();
  return {"已应用节点更新", clusterSummary};
}

} // namespace ns3
