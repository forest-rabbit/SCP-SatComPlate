#ifndef TOPO_DATA_H
#define TOPO_DATA_H

#include <cstdint>
#include <string>
#include <vector>

// JSON拓扑和传统CSV拓扑共用的链路描述。
// source/destination保存的是ns-3 NodeContainer内部下标，不一定等于JSON里的原始node_id。
struct LinkInfo {
    uint32_t source;
    uint32_t destination;
    uint32_t delay_ms;
    uint32_t bandwidth_gbps;
    std::string type;
    uint32_t delay_us;
    uint32_t link_bandwidth_kbps;
    uint32_t link_load_up_kbps;
    uint32_t link_load_down_kbps;
    uint32_t hold_time_s;
    bool has_type;
    bool has_delay_us;
    bool has_delay_ms;
    bool has_link_bandwidth_kbps;
    bool has_bandwidth_gbps;
    bool has_link_load_up_kbps;
    bool has_link_load_down_kbps;
    bool has_hold_time_s;

    LinkInfo()
      : source(0),
        destination(0),
        delay_ms(0),
        bandwidth_gbps(0),
        type("sat"),
        delay_us(0),
        link_bandwidth_kbps(0),
        link_load_up_kbps(0),
        link_load_down_kbps(0),
        hold_time_s(0),
        has_type(false),
        has_delay_us(false),
        has_delay_ms(false),
        has_link_bandwidth_kbps(false),
        has_bandwidth_gbps(false),
        has_link_load_up_kbps(false),
        has_link_load_down_kbps(false),
        has_hold_time_s(false)
    {
    }
};

// JSON节点描述。node_id是外部JSON ID，node_index是创建ns-3节点后得到的内部下标。
// 运行期nodes_*.json只允许更新已有节点的簇信息，不在中途新增ns-3节点。
struct TopologyNodeInfo {
    uint32_t node_id;
    std::string node_type;
    bool is_cluster;
    uint32_t cluster_id;
    bool is_cluster_head;
    uint32_t node_index;

    TopologyNodeInfo()
      : node_id(0),
        node_type("sat"),
        is_cluster(false),
        cluster_id(0),
        is_cluster_head(false),
        node_index(0)
    {
    }
};

// 运行期节点增量。has_*用于区分“字段未出现”和“字段显式写为0/false”。
struct TopologyNodePatch {
    uint32_t node_id;
    bool is_cluster;
    uint32_t cluster_id;
    bool is_cluster_head;
    bool has_is_cluster;
    bool has_cluster_id;
    bool has_is_cluster_head;

    TopologyNodePatch()
      : node_id(0),
        is_cluster(false),
        cluster_id(0),
        is_cluster_head(false),
        has_is_cluster(false),
        has_cluster_id(false),
        has_is_cluster_head(false)
    {
    }
};

// source/destination保存的是ns-3 NodeContainer内部下标。
struct TopologyLinkRemove {
    uint32_t source;
    uint32_t destination;

    TopologyLinkRemove()
      : source(0),
        destination(0)
    {
    }
};

struct TopologyPatchInfo {
    std::vector<TopologyNodePatch> node_updates;
    std::vector<LinkInfo> link_upserts;
    std::vector<TopologyLinkRemove> link_removes;
};

#endif
