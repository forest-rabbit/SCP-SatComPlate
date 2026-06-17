#ifndef TOPO_JSON_H
#define TOPO_JSON_H

#include "topo-data.h"
#include <functional>
#include <string>
#include <vector>

namespace ns3 {

// 一个运行期拓扑时间片。可以引用外部nodes/topology文件，也可以直接内嵌nodes/links数组。
// 目录扫描模式下由文件名生成，例如 nodes_5s.json + topology_5s.json -> time_s=5。
struct TopologyTimeSlice
{
  double time_s;
  std::string nodes_file;
  std::string links_file;
  std::vector<TopologyNodeInfo> nodes;
  std::vector<LinkInfo> links;
};

// topo-json只负责解析文件，不直接访问ns-3节点。
// 解析链路时通过resolver把JSON里的node_id转换为topo.cc中的NodeContainer下标。
typedef std::function<uint32_t(uint32_t, bool)> TopologyNodeResolver;

std::vector<TopologyNodeInfo> ReadTopologyNodesJsonFile(const std::string& filename);
std::vector<LinkInfo> ReadTopologyLinksJsonFile(const std::string& filename,
                                                const TopologyNodeResolver& resolver,
                                                bool hasExplicitNodeIds);
std::vector<TopologyTimeSlice> ReadTopologyTimeSlicesJsonFile(const std::string& filename,
                                                              const TopologyNodeResolver& resolver,
                                                              bool hasExplicitNodeIds);
// 扫描Topodata目录，默认忽略0s文件；0s文件只用于初始化，后续时间片才调度更新。
std::vector<TopologyTimeSlice> ScanTopologyTimeSlicesDirectory(const std::string& dirname);
bool TryParseSecondsFromTimeSliceFilename(const std::string& path, double& seconds);

} // namespace ns3

#endif
