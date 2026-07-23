#include "topo-runtime.h"

#include "topo-json.h"
#include "topo-link-state.h"
#include "topo-node-state.h"

#include "../para.h"
#include "../topo.h"

#include "ns3/fatal-error.h"
#include "ns3/simulator.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace ns3 {

static std::vector<TopologyTimeSlice> g_topologyTimeSlices;
static LinkOutputTimeWindow g_linkOutputTimeWindow;
static uint32_t g_topologyUpdateTotal = 0;
static uint32_t g_topologyUpdateApplied = 0;

// 固定JSON目录。默认情况下不需要命令行传参，甲方只需把JSON文件放到input/topology/json。
static const std::string kDefaultTopoDataDir = "examples/link-selection/input/topology/json/";
static const std::string kDefaultNodesJsonFile = kDefaultTopoDataDir + "nodes_0s.json";
static const std::string kDefaultTopologyJsonFile = kDefaultTopoDataDir + "topology_0s.json";

static void
PrintIndentedBlock(const std::string& text, const std::string& indent)
{
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line))
  {
    std::cout << indent << line << std::endl;
  }
}

static std::string
DirectoryName(const std::string& path)
{
  std::string::size_type pos = path.find_last_of('/');
  if (pos == std::string::npos)
  {
    return ".";
  }
  return path.substr(0, pos + 1);
}

static std::string
TopologyDataLocation()
{
  if (IsLinkOutputMode())
  {
    return linkOutputDir;
  }
  if (!timeSlicesJsonFile.empty())
  {
    return DirectoryName(timeSlicesJsonFile);
  }
  if (!nodesJsonFile.empty())
  {
    return DirectoryName(nodesJsonFile);
  }
  return kDefaultTopoDataDir;
}

static bool
FileExists(const std::string& path)
{
  std::ifstream input(path.c_str());
  return input.good();
}

static void
ValidateInitialJsonTopologyFiles()
{
  bool nodesOk = FileExists(nodesJsonFile);
  bool topologyOk = FileExists(topologyJsonFile);
  if (nodesOk && topologyOk)
  {
    return;
  }

  std::ostringstream oss;
  oss << "[TOPO:Error] JsonTopo已启用，但缺少初始化JSON文件。"
      << "\n  nodes    : " << nodesJsonFile << (nodesOk ? " [OK]" : " [缺失]")
      << "\n  topology : " << topologyJsonFile << (topologyOk ? " [OK]" : " [缺失]")
      << "\n处理方式："
      << "\n  1. 将甲方交付的 nodes_0s.json 和 topology_0s.json 放到 "
      << "examples/link-selection/input/topology/json/；"
      << "\n  2. 或用 --nodesJson/--topologyJson 显式指定示例或真实文件；"
      << "\n  3. 如果不使用JsonTopo，运行时传入 --useJsonTopo=false。";
  std::cerr << oss.str() << std::endl;
  std::exit(EXIT_FAILURE);
}

void
ConfigureDefaultJsonTopologyFiles()
{
  // JSON模式下默认读取0s文件作为初始拓扑；后续xs文件由目录扫描负责。
  if (!_useJsonTopo)
  {
    return;
  }
  if (IsLinkOutputMode())
  {
    if (!nodesJsonFile.empty() || !topologyJsonFile.empty() || !timeSlicesJsonFile.empty())
    {
      NS_FATAL_ERROR("link_output模式不能同时指定nodesJson、topologyJson或timeSlicesJson");
    }
    if (_jsonTopoPatchMode)
    {
      NS_FATAL_ERROR("link_output文件是完整快照，不能启用jsonTopoPatchMode");
    }
    g_linkOutputTimeWindow =
      ScanLinkOutputSnapshotsDirectory(linkOutputDir, totalTimeStep);
    return;
  }
  if (nodesJsonFile.empty())
  {
    nodesJsonFile = kDefaultNodesJsonFile;
  }
  if (topologyJsonFile.empty())
  {
    topologyJsonFile = kDefaultTopologyJsonFile;
  }
  ValidateInitialJsonTopologyFiles();
}

bool
IsLinkOutputMode()
{
  return !linkOutputDir.empty();
}

std::string
GetLinkOutputInitialSnapshotFile()
{
  if (!IsLinkOutputMode() || g_linkOutputTimeWindow.initial_file.empty())
  {
    NS_FATAL_ERROR("link_output时间窗口尚未配置");
  }
  return g_linkOutputTimeWindow.initial_file;
}

static std::string
FormatTopologyUpdateTimes(const std::vector<TopologyTimeSlice>& slices)
{
  if (slices.empty())
  {
    return "无";
  }

  std::ostringstream oss;
  for (uint32_t i = 0; i < slices.size(); ++i)
  {
    if (i > 0)
    {
      oss << ", ";
    }
    oss << slices[i].time_s << "s";
  }
  return oss.str();
}

static std::string
FormatLinkOutputUpdateTimes(const std::vector<LinkOutputSnapshotFile>& snapshots)
{
  if (snapshots.empty())
  {
    return "无";
  }

  std::ostringstream oss;
  for (uint32_t i = 0; i < snapshots.size(); ++i)
  {
    if (i > 0)
    {
      oss << ", ";
    }
    oss << snapshots[i].time_s << "s";
  }
  return oss.str();
}

static void
LogJsonTopologyPlan()
{
  if (IsLinkOutputMode())
  {
    std::cout << "[TOPO:Plan] link_output 时间窗口" << std::endl
              << "  data dir   : " << TopologyDataLocation() << std::endl
              << "  initial    : " << g_linkOutputTimeWindow.initial_file << std::endl
              << "  duration   : " << totalTimeStep << "s" << std::endl
              << "  discovered : " << g_linkOutputTimeWindow.discovered_snapshot_count << std::endl
              << "  selected   : " << g_linkOutputTimeWindow.selected_snapshot_count << std::endl
              << "  updates    : " << g_linkOutputTimeWindow.updates.size() << std::endl
              << "  times      : "
              << FormatLinkOutputUpdateTimes(g_linkOutputTimeWindow.updates) << std::endl
              << std::endl;
    return;
  }

  std::cout << "[TOPO:Plan] JsonTopo 时间片计划" << std::endl
            << "  mode     : " << (_jsonTopoPatchMode ? "patch" : "snapshot") << std::endl
            << "  data dir : " << TopologyDataLocation() << std::endl
            << "  updates  : " << g_topologyTimeSlices.size() << std::endl
            << "  times    : " << FormatTopologyUpdateTimes(g_topologyTimeSlices) << std::endl
            << std::endl;
}

static void
LogTopologyUpdate(uint32_t updateIndex,
                  uint32_t totalUpdates,
                  const std::string& nodeSummary,
                  const std::string& clusterSummary,
                  const TopologyLinkUpdateSummary& linkSummary)
{
  std::cout << "[TOPO:Update] " << updateIndex << "/" << totalUpdates
            << " @ " << Simulator::Now().GetSeconds() << "s" << std::endl
            << "  nodes:" << std::endl;
  PrintIndentedBlock(nodeSummary, "    ");
  if (!clusterSummary.empty())
  {
    std::cout << "  clusters:" << std::endl;
    PrintIndentedBlock(clusterSummary, "    ");
  }
  std::cout << "  links:" << std::endl;
  if (!linkSummary.note.empty()
      && linkSummary.added_links == 0
      && linkSummary.reenabled_links == 0
      && linkSummary.disabled_links == 0)
  {
    std::cout << "    " << linkSummary.note << std::endl
              << std::endl;
    return;
  }
  std::cout << "    新增     : " << linkSummary.added_links << std::endl
            << "    恢复     : " << linkSummary.reenabled_links << std::endl
            << "    断开     : " << linkSummary.disabled_links << std::endl;
  if (!linkSummary.note.empty())
  {
    std::cout << "    说明     : " << linkSummary.note << std::endl;
  }
  std::cout << std::endl;
}

static void
ApplyTopologyTimeSlice(TopologyTimeSlice slice)
{
  // 到达时间片时先更新节点簇信息，再应用链路快照，最后重算全局路由。
  ++g_topologyUpdateApplied;

  std::string nodeSummary;
  std::string clusterSummary;
  std::vector<LinkInfo> links;
  TopologyLinkUpdateSummary linkSummary;
  if (slice.is_patch)
  {
    TopologyPatchInfo patch = slice.patch;
    if (!slice.patch_file.empty())
    {
      patch = ReadResolvedTopologyPatchJsonFile(slice.patch_file);
    }
    TopologyNodeUpdateSummary nodeUpdateSummary = ApplyTopologyNodePatches(patch.node_updates);
    nodeSummary = nodeUpdateSummary.node_summary;
    clusterSummary = nodeUpdateSummary.cluster_summary;
    links = ApplyTopologyLinkPatchToState(patch);
    linkSummary = ApplyFullTopologyLinks(topoNodes, links, true);
    if (patch.link_upserts.empty() && patch.link_removes.empty())
    {
      linkSummary.note = "未提供链路更新，保持上一状态";
    }
  }
  else
  {
    if (slice.has_nodes_update)
    {
      std::vector<TopologyNodeInfo> nodes = slice.nodes;
      if (!slice.nodes_file.empty())
      {
        nodes = ReadTopologyNodesJsonFile(slice.nodes_file);
      }
      TopologyNodeUpdateSummary nodeUpdateSummary = ApplyTopologyNodeInfos(nodes);
      nodeSummary = nodeUpdateSummary.node_summary;
      clusterSummary = nodeUpdateSummary.cluster_summary;
    }
    else
    {
      nodeSummary = "未提供节点更新，保持上一状态";
    }

    if (slice.has_links_update)
    {
      links = slice.links;
      if (!slice.links_file.empty())
      {
        links = ReadResolvedTopologyLinksJsonFile(slice.links_file);
      }
      linkSummary = ApplyFullTopologyLinks(topoNodes, links, true);
    }
    else
    {
      linkSummary = KeepCurrentTopologyLinksSummary();
    }
  }

  LogTopologyUpdate(g_topologyUpdateApplied,
                    g_topologyUpdateTotal,
                    nodeSummary,
                    clusterSummary,
                    linkSummary);
}

static bool
IsGroundNodeInfo(const TopologyNodeInfo& info)
{
  return info.node_type == "gs" || info.node_type == "ground";
}

static void
ValidateLinkOutputSnapshot(const LinkOutputSnapshot& snapshot, const std::string& filename)
{
  std::set<uint32_t> expectedSatelliteIds;
  std::set<uint32_t> actualSatelliteIds;
  std::map<uint32_t, bool> groundByNodeIndex;
  for (const auto& info : topoNodeInfos)
  {
    bool isGround = IsGroundNodeInfo(info);
    groundByNodeIndex[info.node_index] = isGround;
    if (!isGround)
    {
      expectedSatelliteIds.insert(info.node_id);
    }
  }
  for (const auto& patch : snapshot.node_updates)
  {
    actualSatelliteIds.insert(patch.node_id);
  }
  if (actualSatelliteIds != expectedSatelliteIds)
  {
    NS_FATAL_ERROR("link_output运行期卫星集合必须与最早快照一致"
                   << "\n  expected: " << expectedSatelliteIds.size()
                   << "\n  actual  : " << actualSatelliteIds.size()
                   << "\n  file    : " << filename);
  }

  std::set<Link> uniqueLinks;
  for (const auto& link : snapshot.links)
  {
    Link key = MakeLinkKey(link.source, link.destination);
    if (!uniqueLinks.insert(key).second)
    {
      NS_FATAL_ERROR("link_output包含重复链路"
                     << "\n  link: " << FormatLinkKey(key)
                     << "\n  file: " << filename);
    }

    if (link.type == "feeder")
    {
      bool sourceIsGround = groundByNodeIndex[link.source];
      bool destinationIsGround = groundByNodeIndex[link.destination];
      if (sourceIsGround == destinationIsGround)
      {
        NS_FATAL_ERROR("link_output feeder必须恰好连接一颗卫星和一个地面站"
                       << "\n  link: " << FormatLinkKey(key)
                       << "\n  file: " << filename);
      }
    }
  }
}

LinkOutputSnapshot
ReadConfiguredLinkOutputInitialSnapshot()
{
  std::string filename = GetLinkOutputInitialSnapshotFile();
  LinkOutputSnapshot snapshot =
    ReadLinkOutputSnapshotJsonFile(filename, MakeTopologyNodeResolver());
  ValidateLinkOutputSnapshot(snapshot, filename);
  return snapshot;
}

static void
ApplyLinkOutputSnapshot(LinkOutputSnapshotFile snapshotFile)
{
  ++g_topologyUpdateApplied;
  LinkOutputSnapshot snapshot =
    ReadLinkOutputSnapshotJsonFile(snapshotFile.snapshot_file, MakeTopologyNodeResolver());
  ValidateLinkOutputSnapshot(snapshot, snapshotFile.snapshot_file);

  TopologyNodeUpdateSummary nodeUpdateSummary =
    ApplyTopologyNodePatches(snapshot.node_updates);
  TopologyLinkUpdateSummary linkSummary =
    ApplyFullTopologyLinks(topoNodes, snapshot.links, true);
  LogTopologyUpdate(g_topologyUpdateApplied,
                    g_topologyUpdateTotal,
                    nodeUpdateSummary.node_summary,
                    nodeUpdateSummary.cluster_summary,
                    linkSummary);
}

void
ScheduleTopologyTimeSlices()
{
  if (IsLinkOutputMode())
  {
    g_topologyUpdateApplied = 0;
    g_topologyUpdateTotal =
      static_cast<uint32_t>(g_linkOutputTimeWindow.updates.size());
    LogJsonTopologyPlan();
    for (const auto& snapshot : g_linkOutputTimeWindow.updates)
    {
      Simulator::Schedule(Seconds(snapshot.time_s), &ApplyLinkOutputSnapshot, snapshot);
    }
    return;
  }

  if (!timeSlicesJsonFile.empty())
  {
    g_topologyTimeSlices = ReadTopologyTimeSlicesJsonFile(timeSlicesJsonFile,
                                                          MakeTopologyNodeResolver(),
                                                          HasExplicitTopologyNodeIds(),
                                                          _jsonTopoPatchMode);
  }
  else if (_useJsonTopo)
  {
    // 默认不依赖time_slices.json，直接从初始文件所在目录按文件名发现后续时间片。
    g_topologyTimeSlices = ScanTopologyTimeSlicesDirectory(TopologyDataLocation(), _jsonTopoPatchMode);
  }

  g_topologyUpdateApplied = 0;
  g_topologyUpdateTotal = g_topologyTimeSlices.size();
  if (_useJsonTopo)
  {
    LogJsonTopologyPlan();
  }

  for (const auto& slice : g_topologyTimeSlices)
  {
    Simulator::Schedule(Seconds(slice.time_s), &ApplyTopologyTimeSlice, slice);
  }
}

} // namespace ns3
