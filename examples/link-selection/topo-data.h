#ifndef TOPO_DATA_H
#define TOPO_DATA_H

#include <cstdint>
#include <string>

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
    bool has_delay_us;
    bool has_link_bandwidth_kbps;
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
        has_delay_us(false),
        has_link_bandwidth_kbps(false),
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

#endif
