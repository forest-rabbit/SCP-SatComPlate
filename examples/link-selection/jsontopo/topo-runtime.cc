#include "topo-runtime.h"

#include "topo-json.h"
#include "topo-link-state.h"
#include "topo-node-state.h"

#include "../para.h"
#include "../topo.h"

#include "ns3/simulator.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ns3 {

static std::vector<TopologyTimeSlice> g_topologyTimeSlices;
static uint32_t g_topologyUpdateTotal = 0;
static uint32_t g_topologyUpdateApplied = 0;

// 固定JSON目录。默认情况下不需要命令行传参，甲方只需把JSON文件放到Topodata/json。
static const std::string kDefaultTopoDataDir = "examples/link-selection/Topodata/json/";
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

static void
PrintSampleList(const std::vector<std::string>& samples, const std::string& indent)
{
  if (samples.empty())
  {
    std::cout << indent << "无" << std::endl;
    return;
  }
  for (const auto& sample : samples)
  {
    std::cout << indent << "- " << sample << std::endl;
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
      << "\n  1. 将甲方交付的 nodes_0s.json 和 topology_0s.json 放到 examples/link-selection/Topodata/json/；"
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

static void
LogJsonTopologyPlan()
{
  std::cout << "[TOPO:Init] JsonTopo 初始化完成" << std::endl
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
                  const TopologyLinkUpdateSummary& linkSummary)
{
  std::cout << "[TOPO:Update] " << updateIndex << "/" << totalUpdates
            << " @ " << Simulator::Now().GetSeconds() << "s" << std::endl
            << "  nodes:" << std::endl;
  PrintIndentedBlock(nodeSummary, "    ");
  std::cout << "  links:" << std::endl
            << "    目标链路 : " << linkSummary.desired_links << std::endl
            << "    新增     : " << linkSummary.added_links << std::endl
            << "    恢复     : " << linkSummary.reenabled_links << std::endl
            << "    断开     : " << linkSummary.disabled_links << std::endl;
  if (!linkSummary.note.empty())
  {
    std::cout << "    说明     : " << linkSummary.note << std::endl;
  }
  std::cout << "    变化示例 :" << std::endl;
  PrintSampleList(linkSummary.samples, "      ");
  std::cout << std::endl;
}

static void
ApplyTopologyTimeSlice(TopologyTimeSlice slice)
{
  // 到达时间片时先更新节点簇信息，再应用链路快照，最后重算全局路由。
  ++g_topologyUpdateApplied;

  std::string nodeSummary;
  std::vector<LinkInfo> links;
  TopologyLinkUpdateSummary linkSummary;
  if (slice.is_patch)
  {
    TopologyPatchInfo patch = slice.patch;
    if (!slice.patch_file.empty())
    {
      patch = ReadResolvedTopologyPatchJsonFile(slice.patch_file);
    }
    nodeSummary = ApplyTopologyNodePatches(patch.node_updates);
    links = ApplyTopologyLinkPatchToState(patch);
    linkSummary = ApplyFullTopologyLinks(topoNodes, links, true);
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
      nodeSummary = ApplyTopologyNodeInfos(nodes);
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
                    linkSummary);
}

void
ScheduleTopologyTimeSlices()
{
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
