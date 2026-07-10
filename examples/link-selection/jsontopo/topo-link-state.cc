#include "topo-link-state.h"

#include "../access.h"
#include "../para.h"
#include "../topo.h"

#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/net-device-container.h"
#include "ns3/point-to-point-module.h"

#include <map>
#include <set>
#include <sstream>

namespace ns3 {

extern NetDeviceContainer p2pDevices;

static std::set<Link> currentLinks;
static std::map<Link, NetDeviceContainer> linkDevices;
static std::map<Link, LinkInfo> g_currentTopologyLinksByKey;
static uint32_t g_nextIpv4Network = 0;

Link
MakeLinkKey(uint32_t first, uint32_t second)
{
  if (first < second)
  {
    return Link(static_cast<int>(first), static_cast<int>(second));
  }
  return Link(static_cast<int>(second), static_cast<int>(first));
}

static std::string
DisplayNodeId(int nodeIndex)
{
  if (nodeIndex < 0)
  {
    return std::to_string(nodeIndex);
  }

  const uint32_t internalIndex = static_cast<uint32_t>(nodeIndex);
  for (const auto& info : topoNodeInfos)
  {
    if (info.node_index == internalIndex)
    {
      return std::to_string(info.node_id);
    }
  }
  return std::to_string(nodeIndex);
}

std::string
FormatLinkKey(const Link& link)
{
  return DisplayNodeId(link.first) + "<->" + DisplayNodeId(link.second);
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

static Time
LinkDelayTime(const LinkInfo& link)
{
  if (link.has_delay_us)
  {
    return MicroSeconds(link.delay_us);
  }
  return MilliSeconds(link.delay_ms);
}

static void
ConfigureRuntimeLinkAttributes(const NetDeviceContainer& devices, const LinkInfo& link)
{
  Ptr<PointToPointNetDevice> first = DynamicCast<PointToPointNetDevice>(devices.Get(0));
  Ptr<PointToPointNetDevice> second = DynamicCast<PointToPointNetDevice>(devices.Get(1));
  if (first == nullptr || second == nullptr)
  {
    return;
  }

  if (link.has_link_bandwidth_kbps || link.has_bandwidth_gbps)
  {
    DataRateValue dataRate(DataRate(LinkDataRateString(link)));
    first->SetAttribute("DataRate", dataRate);
    second->SetAttribute("DataRate", dataRate);
  }

  if (link.has_delay_us || link.has_delay_ms)
  {
    Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(first->GetChannel());
    if (channel != nullptr)
    {
      channel->SetAttribute("Delay", TimeValue(LinkDelayTime(link)));
    }
  }

  if (link.has_link_load_up_kbps)
  {
    first->SetAttribute("DataLoad",
                        DataRateValue(DataRate(std::to_string(link.link_load_up_kbps) + "kbps")));
  }
  if (link.has_link_load_down_kbps)
  {
    second->SetAttribute("DataLoad",
                         DataRateValue(DataRate(std::to_string(link.link_load_down_kbps) + "kbps")));
  }
}

std::string
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
  oss << std::endl
      << "    sat    : " << satLinks << std::endl
      << "    feeder : " << feederLinks << std::endl
      << "    ground : " << groundLinks << std::endl
      << "    other  : " << otherLinks;
  return oss.str();
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
    ConfigureRuntimeLinkAttributes(devices, link);
    currentLinks.insert(key);
    return "新增";
  }
  else if (currentLinks.find(key) == currentLinks.end())
  {
    ConfigureRuntimeLinkAttributes(existing->second, link);
    LinkUp(nodes.Get(link.source), nodes.Get(link.destination));
    currentLinks.insert(key);
    return "恢复";
  }
  ConfigureRuntimeLinkAttributes(existing->second, link);
  currentLinks.insert(key);
  return "保持";
}

static void
ReplaceCurrentTopologyLinkState(const std::vector<LinkInfo>& links)
{
  g_currentTopologyLinksByKey.clear();
  for (const auto& link : links)
  {
    g_currentTopologyLinksByKey[MakeLinkKey(link.source, link.destination)] = link;
  }
}

static std::vector<LinkInfo>
CurrentTopologyLinksVector()
{
  std::vector<LinkInfo> links;
  links.reserve(g_currentTopologyLinksByKey.size());
  for (const auto& item : g_currentTopologyLinksByKey)
  {
    links.push_back(item.second);
  }
  return links;
}

static LinkInfo
MergeTopologyLinkInfo(const LinkInfo& base, const LinkInfo& patch)
{
  LinkInfo merged = base;
  if (patch.has_type)
  {
    merged.type = patch.type;
    merged.has_type = true;
  }
  if (patch.has_delay_us)
  {
    merged.delay_us = patch.delay_us;
    merged.delay_ms = patch.delay_ms;
    merged.has_delay_us = true;
    merged.has_delay_ms = patch.has_delay_ms;
  }
  else if (patch.has_delay_ms)
  {
    merged.delay_ms = patch.delay_ms;
    merged.has_delay_ms = true;
    merged.has_delay_us = false;
  }
  if (patch.has_link_bandwidth_kbps)
  {
    merged.link_bandwidth_kbps = patch.link_bandwidth_kbps;
    merged.has_link_bandwidth_kbps = true;
    merged.has_bandwidth_gbps = false;
  }
  else if (patch.has_bandwidth_gbps)
  {
    merged.bandwidth_gbps = patch.bandwidth_gbps;
    merged.has_bandwidth_gbps = true;
    merged.has_link_bandwidth_kbps = false;
  }
  if (patch.has_link_load_up_kbps)
  {
    merged.link_load_up_kbps = patch.link_load_up_kbps;
    merged.has_link_load_up_kbps = true;
  }
  if (patch.has_link_load_down_kbps)
  {
    merged.link_load_down_kbps = patch.link_load_down_kbps;
    merged.has_link_load_down_kbps = true;
  }
  if (patch.has_hold_time_s)
  {
    merged.hold_time_s = patch.hold_time_s;
    merged.has_hold_time_s = true;
  }
  return merged;
}

std::vector<LinkInfo>
ApplyTopologyLinkPatchToState(const TopologyPatchInfo& patch)
{
  for (const auto& remove : patch.link_removes)
  {
    g_currentTopologyLinksByKey.erase(MakeLinkKey(remove.source, remove.destination));
  }

  for (const auto& upsert : patch.link_upserts)
  {
    Link key = MakeLinkKey(upsert.source, upsert.destination);
    auto existing = g_currentTopologyLinksByKey.find(key);
    if (existing == g_currentTopologyLinksByKey.end())
    {
      g_currentTopologyLinksByKey[key] = upsert;
    }
    else
    {
      g_currentTopologyLinksByKey[key] = MergeTopologyLinkInfo(existing->second, upsert);
    }
  }

  return CurrentTopologyLinksVector();
}

TopologyLinkUpdateSummary
KeepCurrentTopologyLinksSummary()
{
  TopologyLinkUpdateSummary summary;
  summary.desired_links = g_currentTopologyLinksByKey.size();
  summary.unchanged_links = g_currentTopologyLinksByKey.size();
  summary.note = "未提供链路更新，保持上一状态";
  return summary;
}

TopologyLinkUpdateSummary
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
    }
    else if (state == "恢复")
    {
      ++summary.reenabled_links;
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
  }

  if (recomputeRoutes)
  {
    Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
  }
  ReplaceCurrentTopologyLinkState(links);
  return summary;
}

} // namespace ns3
