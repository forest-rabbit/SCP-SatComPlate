#ifndef TOPO_NODE_STATE_H
#define TOPO_NODE_STATE_H

#include "topo-data.h"
#include "topo-json.h"

#include <string>
#include <vector>

namespace ns3 {

struct TopologyNodeUpdateSummary
{
  std::string node_summary;
  std::string cluster_summary;
};

TopologyNodeResolver MakeTopologyNodeResolver();
bool HasExplicitTopologyNodeIds();

std::vector<LinkInfo> ReadResolvedTopologyLinksJsonFile(const std::string& filename);
TopologyPatchInfo ReadResolvedTopologyPatchJsonFile(const std::string& filename);

void CreateNodesFromJsonInfo(const std::vector<TopologyNodeInfo>& nodeInfos);
TopologyNodeUpdateSummary ApplyTopologyNodeInfos(const std::vector<TopologyNodeInfo>& nodeInfos);
TopologyNodeUpdateSummary ApplyTopologyNodePatches(const std::vector<TopologyNodePatch>& patches);

} // namespace ns3

#endif
