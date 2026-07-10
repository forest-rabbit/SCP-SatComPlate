#ifndef TOPO_LINK_STATE_H
#define TOPO_LINK_STATE_H

#include "topo-data.h"
#include "ns3/node-container.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ns3 {

typedef std::pair<int, int> Link;

struct TopologyLinkUpdateSummary
{
  uint32_t desired_links = 0;
  uint32_t added_links = 0;
  uint32_t reenabled_links = 0;
  uint32_t disabled_links = 0;
  uint32_t unchanged_links = 0;
  std::string note;
};

Link MakeLinkKey(uint32_t first, uint32_t second);
std::string FormatLinkKey(const Link& link);
std::string FormatInitialTopologyLinkTypes(const std::vector<LinkInfo>& links);

std::vector<LinkInfo> ApplyTopologyLinkPatchToState(const TopologyPatchInfo& patch);
TopologyLinkUpdateSummary KeepCurrentTopologyLinksSummary();
TopologyLinkUpdateSummary ApplyFullTopologyLinks(NodeContainer& nodes,
                                                 const std::vector<LinkInfo>& links,
                                                 bool recomputeRoutes);

} // namespace ns3

#endif
