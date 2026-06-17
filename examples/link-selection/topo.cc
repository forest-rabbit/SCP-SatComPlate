#include "topo.h"
#include "topo-json.h"
#include "ns3/boolean.h"
#include "ns3/integer.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "access.h"
#include "para.h"

// #include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <ns3/ipv4-address.h>
#include <ns3/node.h>
#include <ns3/object.h>
#include <ns3/ptr.h>
#include <set>
#include <sstream>
#include <sys/types.h>
#include <unordered_map>

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample-topo");

namespace ns3{
  NodeContainer sates;       // 所有卫星节点
  std::vector<NodeContainer> sateNodes;  // 所有卫星节点，按照轨道和轨道内的卫星排列
  std::vector<uint32_t>      NodesSlaveID;  // 保存所有卫星节点中子控制器ID，按照node.GetID()来索引元素
  std::vector<uint32_t>      changeClusterID;  // 记录所有卫星节点簇ID变化次数
  NetDeviceContainer p2pDevices; 
  NodeContainer Gnodes;      // 地面网络节点
  NodeContainer topoNodes;   // JSON拓扑中的所有节点，包含卫星和地面站
  std::vector<TopologyNodeInfo> topoNodeInfos;
  std::string nodesJsonFile;
  std::string topologyJsonFile;
  std::string timeSlicesJsonFile;



  // 激光链路分配文件的时间戳和链路配置映射
  typedef std::pair<int, int> Link; // 链路，存储链路的两个节点
  typedef std::vector<Link> LinkSet; // 链路集合，存储链路的节点对
  std::map<int, LinkSet> timeLinksMap; // 时间戳到链路集合的映射
  std::set<Link> currentLinks; // 当前活跃链路集合
  std::map<Link, NetDeviceContainer> linkDevices; // 记录每条链路对应的设备，避免重复安装

  // 跟踪每个节点已经使用的接口索引
  std::map<uint32_t, std::set<uint32_t>> nodeUsedIndices;

  // JSON模式的外部node_id到内部NodeContainer下标的映射。
  // 链路安装必须使用内部下标，日志和JSON仍保留外部ID语义。
  static std::unordered_map<uint32_t, uint32_t> g_topologyNodeIndexByExternalId;
  static std::vector<TopologyTimeSlice> g_topologyTimeSlices;
  static uint32_t g_nextIpv4Network = 0;
  static uint32_t g_topologyUpdateTotal = 0;
  static uint32_t g_topologyUpdateApplied = 0;

  // 固定JSON目录。默认情况下不需要命令行传参，甲方只需把文件放到Topodata。
  static const std::string kDefaultTopoDataDir = "examples/link-selection/Topodata/";
  static const std::string kDefaultNodesJsonFile = kDefaultTopoDataDir + "nodes_0s.json";
  static const std::string kDefaultTopologyJsonFile = kDefaultTopoDataDir + "topology_0s.json";

  static void BuildClusterNodesFromJsonInfo();
  static std::string BuildNodeUpdateSummary(const std::vector<TopologyNodeInfo>& updatedInfos);

  struct TopologyLinkUpdateSummary
  {
    uint32_t desired_links = 0;
    uint32_t added_links = 0;
    uint32_t reenabled_links = 0;
    uint32_t disabled_links = 0;
    uint32_t unchanged_links = 0;
    std::vector<std::string> samples;
  };

  static void
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
  }

  static Link
  MakeLinkKey(uint32_t first, uint32_t second)
  {
    if (first < second)
    {
      return Link(static_cast<int>(first), static_cast<int>(second));
    }
    return Link(static_cast<int>(second), static_cast<int>(first));
  }

  static std::string
  FormatLinkKey(const Link& link)
  {
    return std::to_string(link.first) + "<->" + std::to_string(link.second);
  }

  static void
  AddSample(std::vector<std::string>& samples, const std::string& value)
  {
    static const uint32_t kMaxSamples = 8;
    if (samples.size() < kMaxSamples)
    {
      samples.push_back(value);
    }
  }

  static std::string
  JoinSamples(const std::vector<std::string>& samples)
  {
    if (samples.empty())
    {
      return "无";
    }
    std::ostringstream oss;
    for (uint32_t i = 0; i < samples.size(); ++i)
    {
      if (i > 0)
      {
        oss << ", ";
      }
      oss << samples[i];
    }
    return oss.str();
  }

  static std::string
  FormatTopologyUpdateTimes(const std::vector<TopologyTimeSlice>& slices)
  {
    if (slices.empty())
    {
      return "无后续更新时间点";
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
  FormatTopologySliceFiles(const std::vector<TopologyTimeSlice>& slices)
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
        oss << " | ";
      }
      oss << slices[i].time_s << "s("
          << "node=" << (slices[i].nodes_file.empty() ? "内嵌/无" : slices[i].nodes_file)
          << ", topology=" << (slices[i].links_file.empty() ? "内嵌/无" : slices[i].links_file)
          << ")";
    }
    return oss.str();
  }

  static void
  LogJsonTopologyPlan(const std::string& sliceSource)
  {
    std::cout << "[JSON-TOPO] 初始化完成" << std::endl
              << "  nodes    : " << (nodesJsonFile.empty() ? "未使用" : nodesJsonFile) << std::endl
              << "  topology : " << (topologyJsonFile.empty() ? "未使用" : topologyJsonFile) << std::endl
              << "  time src : " << sliceSource << std::endl
              << "  updates  : " << g_topologyTimeSlices.size()
              << " (" << FormatTopologyUpdateTimes(g_topologyTimeSlices) << ")" << std::endl
              << "  files    : " << FormatTopologySliceFiles(g_topologyTimeSlices) << std::endl;
  }

  static void
  LogTopologyUpdate(uint32_t updateIndex,
                    uint32_t totalUpdates,
                    uint32_t remaining,
                    const std::string& nodeSummary,
                    const TopologyLinkUpdateSummary& linkSummary)
  {
    std::cout << "[JSON-TOPO] 更新 " << updateIndex << "/" << totalUpdates
              << " @ " << Simulator::Now().GetSeconds() << "s" << std::endl
              << "  remaining : " << remaining << std::endl
              << "  nodes     : " << nodeSummary << std::endl
              << "  links     : 目标=" << linkSummary.desired_links
              << ", 新增=" << linkSummary.added_links
              << ", 恢复=" << linkSummary.reenabled_links
              << ", 断开=" << linkSummary.disabled_links
              << ", 未变化=" << linkSummary.unchanged_links << std::endl
              << "  samples   : " << JoinSamples(linkSummary.samples) << std::endl;
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

  static TopologyNodeResolver
  MakeTopologyNodeResolver()
  {
    return [](uint32_t externalNodeId, bool zeroBasedFallback) {
      return ResolveTopologyNodeIndex(externalNodeId, zeroBasedFallback);
    };
  }

  static bool
  HasExplicitTopologyNodeIds()
  {
    return !g_topologyNodeIndexByExternalId.empty();
  }

  static std::vector<LinkInfo>
  ReadResolvedTopologyLinksJsonFile(const std::string& filename)
  {
    return ReadTopologyLinksJsonFile(filename, MakeTopologyNodeResolver(), HasExplicitTopologyNodeIds());
  }

  static Ipv4Address
  NextIpv4Base()
  {
    uint32_t raw = 0x0a000000u + (g_nextIpv4Network * 4u);
    ++g_nextIpv4Network;
    return Ipv4Address(raw);
  }

  static std::string
  LinkDataRateString(const LinkInfo& link)
  {
    if (link.has_link_bandwidth_kbps && link.link_bandwidth_kbps > 0)
    {
      return std::to_string(link.link_bandwidth_kbps) + "kbps";
    }
    if (link.bandwidth_gbps > 0)
    {
      return std::to_string(link.bandwidth_gbps) + "Gbps";
    }
    return std::to_string(linkBandwidth) + "bps";
  }

  static std::string
  LinkDelayString(const LinkInfo& link)
  {
    if (link.has_delay_us)
    {
      return std::to_string(link.delay_us) + "us";
    }
    return std::to_string(link.delay_ms) + "ms";
  }

  static std::string
  FormatInitialTopologyLinkTypes(const std::vector<LinkInfo>& links)
  {
    uint32_t satLinks = 0;
    uint32_t feederLinks = 0;
    uint32_t groundLinks = 0;
    uint32_t otherLinks = 0;

    for (const auto& link : links)
    {
      if (link.type == "sat")
      {
        ++satLinks;
      }
      else if (link.type == "feeder")
      {
        ++feederLinks;
      }
      else if (link.type == "ground")
      {
        ++groundLinks;
      }
      else
      {
        ++otherLinks;
      }
    }

    std::ostringstream oss;
    oss << "sat=" << satLinks
        << ", feeder=" << feederLinks
        << ", ground=" << groundLinks
        << ", other=" << otherLinks;
    return oss.str();
  }

  static std::string
  FormatInitialTopologyLinkSamples(const std::vector<LinkInfo>& links)
  {
    std::vector<std::string> samples;
    for (const auto& link : links)
    {
      AddSample(samples, FormatLinkKey(MakeLinkKey(link.source, link.destination)) + "(" + link.type + ")");
    }
    return JoinSamples(samples);
  }

  static void
  AssignLinkIpv4(NetDeviceContainer devices)
  {
    Ipv4AddressHelper ipv4;
    ipv4.SetBase(NextIpv4Base(), "255.255.255.252");
    ipv4.Assign(devices);
  }

  static std::string
  EnsureLinkInstalledAndUp(NodeContainer& nodes, const LinkInfo& link)
  {
    if (link.source >= nodes.GetN() || link.destination >= nodes.GetN())
    {
      NS_FATAL_ERROR("链路节点下标越界: " << link.source << " -> " << link.destination);
    }

    Link key = MakeLinkKey(link.source, link.destination);
    auto existing = linkDevices.find(key);
    if (existing == linkDevices.end())
    {
      // 第一次出现的链路需要安装NetDevice并分配独立/30网段。
      // 后续时间片如果再次出现同一条链路，只做LinkUp，不重复安装设备。
      PointToPointHelper p2p;
      p2p.SetDeviceAttribute("DataRate", StringValue(LinkDataRateString(link)));
      p2p.SetChannelAttribute("Delay", StringValue(LinkDelayString(link)));
      p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));

      NetDeviceContainer devices = p2p.Install(NodeContainer(nodes.Get(link.source), nodes.Get(link.destination)));
      p2pDevices.Add(devices);
      AssignLinkIpv4(devices);
      linkDevices[key] = devices;
      return "新增";
    }
    else if (currentLinks.find(key) == currentLinks.end())
    {
      LinkUp(nodes.Get(link.source), nodes.Get(link.destination));
      currentLinks.insert(key);
      return "恢复";
    }
    currentLinks.insert(key);
    return "保持";
  }

  static TopologyLinkUpdateSummary
  ApplyFullTopologyLinks(NodeContainer& nodes, const std::vector<LinkInfo>& links, bool recomputeRoutes)
  {
    // JSON时间片被视为“完整拓扑快照”：文件里没有出现的当前链路会被关闭。
    // 这保证运行期状态完全由JSON控制，而不是叠加旧的动态更新逻辑。
    TopologyLinkUpdateSummary summary;
    summary.desired_links = links.size();
    std::set<Link> desiredLinks;
    for (const auto& link : links)
    {
      Link key = MakeLinkKey(link.source, link.destination);
      desiredLinks.insert(key);
      std::string state = EnsureLinkInstalledAndUp(nodes, link);
      if (state == "新增")
      {
        ++summary.added_links;
        AddSample(summary.samples, state + ":" + FormatLinkKey(key) + "(" + link.type + ")");
      }
      else if (state == "恢复")
      {
        ++summary.reenabled_links;
        AddSample(summary.samples, state + ":" + FormatLinkKey(key) + "(" + link.type + ")");
      }
      else
      {
        ++summary.unchanged_links;
      }
    }

    std::vector<Link> linksToDown;
    for (const auto& active : currentLinks)
    {
      if (desiredLinks.find(active) == desiredLinks.end())
      {
        linksToDown.push_back(active);
      }
    }

    for (const auto& link : linksToDown)
    {
      if (linkDevices.find(link) != linkDevices.end())
      {
        LinkDown(nodes.Get(link.first), nodes.Get(link.second));
      }
      currentLinks.erase(link);
      ++summary.disabled_links;
      AddSample(summary.samples, "断开:" + FormatLinkKey(link));
    }

    if (recomputeRoutes)
    {
      Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
    }
    return summary;
  }

  static std::string
  ApplyTopologyNodeInfos(const std::vector<TopologyNodeInfo>& nodeInfos)
  {
    if (nodeInfos.empty())
    {
      return "节点文件为空或本时间片未包含节点更新";
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

    std::string summary = BuildNodeUpdateSummary(updatedInfos);
    topoNodeInfos.assign(updatedInfos.begin(), updatedInfos.end());
    BuildClusterNodesFromJsonInfo();
    return summary;
  }

  static void
  ApplyTopologyTimeSlice(TopologyTimeSlice slice)
  {
    // 到达时间片时先更新节点簇信息，再应用链路快照，最后重算全局路由。
    ++g_topologyUpdateApplied;
    uint32_t remaining = g_topologyUpdateTotal > g_topologyUpdateApplied
                         ? g_topologyUpdateTotal - g_topologyUpdateApplied
                         : 0;
    std::vector<TopologyNodeInfo> nodes = slice.nodes;
    if (!slice.nodes_file.empty())
    {
      nodes = ReadTopologyNodesJsonFile(slice.nodes_file);
    }
    std::string nodeSummary = ApplyTopologyNodeInfos(nodes);

    std::vector<LinkInfo> links = slice.links;
    if (!slice.links_file.empty())
    {
      links = ReadResolvedTopologyLinksJsonFile(slice.links_file);
    }
    TopologyLinkUpdateSummary linkSummary = ApplyFullTopologyLinks(topoNodes, links, true);
    LogTopologyUpdate(g_topologyUpdateApplied,
                      g_topologyUpdateTotal,
                      remaining,
                      nodeSummary,
                      linkSummary);
  }

  static void
  ScheduleTopologyTimeSlices()
  {
    std::string sliceSource = "未使用";
    if (!timeSlicesJsonFile.empty())
    {
      g_topologyTimeSlices = ReadTopologyTimeSlicesJsonFile(timeSlicesJsonFile,
                                                            MakeTopologyNodeResolver(),
                                                            HasExplicitTopologyNodeIds());
      sliceSource = "index file: " + timeSlicesJsonFile;
    }
    else if (_useJsonTopo)
    {
      // 默认不依赖time_slices.json，直接从Topodata目录按文件名发现所有后续时间片。
      g_topologyTimeSlices = ScanTopologyTimeSlicesDirectory(kDefaultTopoDataDir);
      sliceSource = "directory scan: " + kDefaultTopoDataDir;
    }

    g_topologyUpdateApplied = 0;
    g_topologyUpdateTotal = g_topologyTimeSlices.size();
    if (_useJsonTopo)
    {
      LogJsonTopologyPlan(sliceSource);
    }

    for (const auto& slice : g_topologyTimeSlices)
    {
      Simulator::Schedule(Seconds(slice.time_s), &ApplyTopologyTimeSlice, slice);
    }
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
        orbitNodes.Add(sates.Get(i * sate_num + j));
      }
      sateNodes.push_back(orbitNodes);
    }
  }

  static void
  BuildClusterNodesFromJsonInfo()
  {
    // satClusterNodes保持旧代码约定：每个NodeContainer第一个节点为簇首。
    // 甲方确认簇首通常是信关站，因此这里允许簇首来自Gnodes，普通成员只加入卫星节点。
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
  BuildNodeUpdateSummary(const std::vector<TopologyNodeInfo>& updatedInfos)
  {
    // 只打印摘要和少量样例，避免每次更新时间片刷屏。
    std::map<uint32_t, TopologyNodeInfo> oldInfos;
    for (const auto& info : topoNodeInfos)
    {
      oldInfos[info.node_id] = info;
    }

    uint32_t clusterChanged = 0;
    uint32_t headChanged = 0;
    uint32_t addedNodes = 0;
    uint32_t unchangedNodes = 0;
    std::vector<std::string> samples;

    for (const auto& info : updatedInfos)
    {
      auto oldIt = oldInfos.find(info.node_id);
      if (oldIt == oldInfos.end())
      {
        ++addedNodes;
        AddSample(samples, "新增节点:" + std::to_string(info.node_id));
        continue;
      }

      const TopologyNodeInfo& oldInfo = oldIt->second;
      bool changed = false;
      if (oldInfo.is_cluster != info.is_cluster || oldInfo.cluster_id != info.cluster_id)
      {
        ++clusterChanged;
        changed = true;
        AddSample(samples,
                  "节点" + std::to_string(info.node_id) + "簇:"
                  + std::to_string(oldInfo.cluster_id) + "->" + std::to_string(info.cluster_id));
      }
      if (oldInfo.is_cluster_head != info.is_cluster_head)
      {
        ++headChanged;
        changed = true;
        AddSample(samples,
                  "节点" + std::to_string(info.node_id)
                  + (info.is_cluster_head ? "成为簇首" : "取消簇首"));
      }
      if (!changed)
      {
        ++unchangedNodes;
      }
    }

    std::ostringstream oss;
    oss << "节点总数=" << updatedInfos.size()
        << ", 新增节点=" << addedNodes
        << ", 簇归属变化=" << clusterChanged
        << ", 簇首变化=" << headChanged
        << ", 未变化=" << unchangedNodes
        << ", 样例=" << JoinSamples(samples);
    return oss.str();
  }

  static void
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
    std::cout << "[JSON-TOPO] 节点创建完成" << std::endl
              << "  satellites : " << sates.GetN() << std::endl
              << "  ground     : " << Gnodes.GetN() << std::endl
              << "  total      : " << topoNodes.GetN() << std::endl
              << "  clusters   : " << satClusterNodes.size() << std::endl;
  }



  // 函数功能：输出卫星节点信息以及网卡信息
  void print_node_info(){
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("examples/link-selection/output/node-info-tables.txt");
    *stream->GetStream() <<  Simulator::Now().GetSeconds() << "s" << std::endl;
    for(uint32_t i=0; i<sates.GetN(); i++){
      uint32_t size = sates.Get(i)->GetNDevices();
      for(uint32_t j=0; j<size; j++){
        // 获取目的地址
        Ptr<Node> nodep =sates.Get(i);
        Ptr<NetDevice> dev = nodep->GetDevice(j);
        // 获取网卡的 IPv4 接口列表
        Ptr<Ipv4> ipv4 = nodep->GetObject<Ipv4>();
        uint32_t interfaceIndex = dev->GetIfIndex();
        // 获取 IPv4 地址
        Ipv4Address Address = ipv4->GetAddress(interfaceIndex, 0).GetLocal();
        Mac48Address mac = Mac48Address::ConvertFrom(dev->GetAddress());
        *stream->GetStream() <<"node："<< nodep->GetId() << " netdevice：" << j << " interface index：" << interfaceIndex 
                              << " IPv4地址：" << Address 
                              << " mac地址：" << mac
                              << std::endl;

        Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(dev);
        if(p2p_dev != nullptr && p2p_dev->IsLinkUp()){
          Ptr<Channel> channel = p2p_dev->GetChannel();
          for(uint32_t k = 0; k < channel->GetNDevices(); k++){
            Ptr<NetDevice> adj_device = channel->GetDevice(k);
            if( adj_device != dev)
            {
              Ptr<Node> adj_node = adj_device->GetNode();
              *stream->GetStream() <<  "adj_node:" << adj_node->GetId() ;
            }
          }
        }
        *stream->GetStream() <<  endl;
        // ipv4AddrMaps[Address] = nodep->GetId();
      }
    }
  }

  //获取邻接表
  void print_adjacency_list(NodeContainer allnodes){
    std::cout << "\n邻接表输出:" << std::endl;
    for (uint32_t nodeAId = 0; nodeAId < allnodes.GetN(); ++nodeAId)
    {
        Ptr<Node> nodeA = allnodes.Get(nodeAId);
        std::cout << "Node " << nodeA->GetId() << ": ";
        // 遍历节点A的设备
        for (uint32_t devA = 0; devA < nodeA->GetNDevices(); ++devA)
        {
            Ptr<NetDevice> netDeviceA = nodeA->GetDevice(devA);
            Ptr<PointToPointNetDevice> p2pNetDeviceA = DynamicCast<PointToPointNetDevice>(netDeviceA);

            // 检查设备是否为 PointToPointNetDevice
            if (p2pNetDeviceA && p2pNetDeviceA->IsLinkUp())
            {
                Ptr<Channel> channelA = p2pNetDeviceA->GetChannel();
                // 遍历通道上的所有设备，找出与 nodeA 连接的其他节点
                for (uint32_t devB = 0; devB < channelA->GetNDevices(); ++devB)
                {
                    Ptr<NetDevice> netDeviceB = channelA->GetDevice(devB);
                    // 跳过自身的设备
                    if (netDeviceB != netDeviceA)
                    {
                        Ptr<PointToPointNetDevice> p2pNetDeviceB = DynamicCast<PointToPointNetDevice>(netDeviceB);
                        // 确定连接的节点
                        Ptr<Node> nodeB = netDeviceB->GetNode();
                        std::cout << nodeB->GetId() << ",";
                    }
                }
            }
        }

        std::cout << std::endl;
    }
}


  void initTopo(){
    // #ifdef NS3_OPENFLOW
    ConfigureDefaultJsonTopologyFiles();

    if (!nodesJsonFile.empty())
    {
      CreateNodesFromJsonInfo(ReadTopologyNodesJsonFile(nodesJsonFile));
    }
    else
    {
      //创建卫星节点
      for(uint32_t i = 0; i < orbit_num; i++){
        NodeContainer nodes;
        for(uint32_t j=0; j<sate_num; j++){
          Ptr<Node> node = CreateObject<Node> ();
          // node->SetBeginId(5);
          nodes.Add(node);
        }
        sates.Add(nodes);
        topoNodes.Add(nodes);
        sateNodes.push_back(nodes);
      }
    }
    
    // 配置 PointToPoint 信道属性
    PointToPointHelper pointToPoint;
    if(linkBandwidth == 10000000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else pointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    pointToPoint.SetChannelAttribute ("Delay", StringValue("100ms"));  

    pointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("8000p"));//队列容量K=1000

    // 配置 同轨链路 PointToPoint 信道属性
    PointToPointHelper intraPlanePointToPoint;
    if(linkBandwidth == 10000000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Gbps"));
    else if(linkBandwidth == 100000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    if(_isSate == 1)    intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("15.56ms"));        // Sat1 同轨链路时延
    else if(_isSate == 2)   intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("17.16ms"));    // Sat2 同轨链路时延
    else if(_isSate == 3)   intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("7.85ms"));     // Sat3 同轨链路时延
    else if(_isSate == 4)   intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("8.71ms"));     // Sat4 同轨链路时延

    intraPlanePointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("8000p"));                    // 队列容量K=1000

    // 配置 异轨链路 PointToPoint 信道属性
    PointToPointHelper interPlanePointToPoint;
    if(linkBandwidth == 10000000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Gbps"));
    else if(linkBandwidth == 100000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    if(_isSate == 1)    interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("8.93ms"));         // Sat1 异轨链路时延
    else if(_isSate == 2)   interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("9.18ms"));     // Sat2 异轨链路时延
    else if(_isSate == 3)   interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("5.42ms"));     // Sat3 异轨链路时延
    else if(_isSate == 4)   interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("5.61ms"));     // Sat4 异轨链路时延

    interPlanePointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("1000p"));//队列容量K=1000

    InternetStackHelper stack;
    stack.Install(topoNodes);
    if (!_useJsonTopo)
    {
      cout<<sates.GetN()<<" 个卫星节点创建完成！"<<endl;
    }

    //搭建非mesh拓扑 - XW
    if(!_isMesh){
      if (!topologyJsonFile.empty())
      {
        std::vector<LinkInfo> links = ReadResolvedTopologyLinksJsonFile(topologyJsonFile);
        BuildNetworkTopology(topoNodes, links);
      }
      else
      {
        std::vector<LinkInfo> links = ReadTopologyFile("examples/link-selection/topo(324).csv");
        BuildNetworkTopology(sates, links);
      }
    }
    
    //搭建mesh拓扑 - 普通的
    else{

    NetDeviceContainer tmpDevices;

    // 搭建p2p拓扑
		// 轨道内链路连接
		for(uint32_t i=0; i<orbit_num; i++){
			for(uint32_t j=0; j<sate_num; j++){
				Ptr<Node> node = sateNodes[i].Get(j);
				Ptr<Node> next = sateNodes[i].Get((j+1)%sate_num);
        tmpDevices = intraPlanePointToPoint.Install(NodeContainer(node, next));
				p2pDevices.Add(tmpDevices);
			}
		}



    if(!_scenario) //正常场景
    {
      cout<<"搭建mesh拓扑-正常场景！"<<endl;
      // 轨道间链路连接
      for(uint32_t j=0; j<sate_num; j++){
        for(uint32_t i=0; i<orbit_num; i++){
          Ptr<Node> node = sateNodes[i].Get(j);
          Ptr<Node> next = sateNodes[(i+1)%orbit_num].Get(j);
          tmpDevices = interPlanePointToPoint.Install(NodeContainer(node, next));
          p2pDevices.Add(tmpDevices);
        }
      }

      // 普通卫星设置ip
      for(uint32_t i = 0; i < sateNodes.size(); ++i){
        Ipv4AddressHelper sipv4Helper;
        std::string str;
        str = "10." + std::to_string(i+1);
        for(uint32_t j = 0; j < sateNodes[i].GetN(); j++){
          std::string temp = str + "." + std::to_string(j+1) + ".0";
          Ipv4Address addr (temp.c_str ());
          sipv4Helper.SetBase(addr, "255.255.255.0");
          uint32_t size = sateNodes[i].Get(j)->GetNDevices();
          for(uint32_t k=0; k < size; k++){
            sipv4Helper.Assign(sateNodes[i].Get(j)->GetDevice(k));
          }
        }
      }
      //print_node_info();
    }
    else //激光链路分配场景
    {
      // 读取链路配置文件
    }
  } //mesh拓扑结束

    // 基于链路可用度的链路变化
    // if(!_sim) SetLinkAvaAvailability(sates);

    // 调用初始分簇算法
    // 启动动态分簇算法
    // ActiveCluster(sates, satClusterNodes);

    //打印邻接表
    //print_adjacency_list(sates);


    
    // 打印节点信息
    //print_node_info();
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    ScheduleTopologyTimeSlices();

    // 添加簇内簇间路由
    if(_SDNRoute){
      //Ipv4GlobalRoutingHelper::SDNRoutingTables(Gnodes, sates);   //OSPF最短路由
      //experiment.InitialSatRouter(Gnodes, sates, satClusterNodes, monitors, _consType, orbit_num, sate_num); // 初始化卫星路由策略
    }

    // 打印所有节点的路由表
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("examples/link-selection/output/routing-tables-6s.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (6), stream, Time::S);
    Ptr<OutputStreamWrapper> stream2 = ascii.CreateFileStream ("examples/link-selection/output/routing-tables-11s.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (11), stream2, Time::S);
    Ptr<OutputStreamWrapper> stream3 = ascii.CreateFileStream ("examples/link-selection/output/routing-tables-16s.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (16), stream3, Time::S);

    if (!_useJsonTopo)
    {
      Simulator::Schedule(Seconds(1.0), &updateTopo);
    }



    NS_LOG_INFO ("Configure Tracing.");

  }
  // 更新拓扑
  void updateTopo(){
    if (_useJsonTopo)
    {
      return;
    }
    // 获取当前模拟时间（取整到秒）
    int currentTime = Simulator::Now().GetSeconds();
    std::cout << "当前时间: " << currentTime << "s" << std::endl;
   
    if(!_isMesh) {
      Simulator::Schedule(Seconds(1.0), &updateTopo);
      return;
    } // 非mesh拓扑不更新链路

    if(_isMesh && timeLinksMap.empty()) {
      Simulator::Schedule(Seconds(1.0), &updateTopo);
      return; 
    }// mesh拓扑但无链路变化配置不更新链路
    
    // 调度下一次更新
    Simulator::Schedule(Seconds(1.0), &updateTopo);
  } 

// 读取CSV文件并解析链路信息
std::vector<LinkInfo> ReadTopologyFile(const std::string& filename) {
    std::vector<LinkInfo> links;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        NS_FATAL_ERROR("无法打开文件: " << filename);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        LinkInfo link;
        
        // 解析CSV格式: 源节点,目的节点,时延(ms),带宽(Gbps)
        std::getline(ss, token, ',');
        link.source = std::stoi(token) - 1; // 转换为0索引
        
        std::getline(ss, token, ',');
        link.destination = std::stoi(token) - 1;
        
        std::getline(ss, token, ',');
        link.delay_ms = std::stoi(token);
        
        std::getline(ss, token, ',');
        link.bandwidth_gbps = std::stoi(token);
        
        links.push_back(link);
    }
    
    file.close();
    return links;
}

// 搭建非mesh网络
void BuildNetworkTopology(NodeContainer& satellites,  const std::vector<LinkInfo>& links){
    TopologyLinkUpdateSummary summary = ApplyFullTopologyLinks(satellites, links, false);
    std::cout << "[JSON-TOPO] 初始链路安装完成" << std::endl
              << "  total     : " << links.size() << std::endl
              << "  by type   : " << FormatInitialTopologyLinkTypes(links) << std::endl
              << "  installed : " << summary.added_links << std::endl
              << "  reused    : " << summary.unchanged_links + summary.reenabled_links << std::endl
              << "  samples   : " << FormatInitialTopologyLinkSamples(links) << std::endl;
}



}
