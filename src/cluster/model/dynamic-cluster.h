#ifndef DYNAMIC_CLUSTER_H
#define DYNAMIC_CLUSTER_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/betweenness-centrality.h"
#include "ns3/link-utilization.h"

#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <queue>
#include <cmath>

using namespace ns3;
using namespace std;


struct Satellite
{
    uint32_t id;
    uint32_t cluster_index;
    double max_utilization;
    // 重载 == 运算符(use the node id to judge the satellite node)
    bool operator==(const Satellite& other) const 
    {
        return id == other.id;
    }
};

struct Cluster
{
    vector<Satellite> satellites;
    double fitness;
    uint32_t cluster_head;
    uint32_t candidate_head;
    // // 定义 < 运算符
    // bool operator<(const Cluster& other) const {
    // // 实现比较逻辑，例如比较 fitness 成员
    //     return fitness < other.fitness;
    // }
};

// 故障类型
enum State{
  nodeClusterHead, //簇首节点故障
  linkClusterHead, //簇首链路故障
  linkCLusterMember, //簇成员链路故障
  nodeClusterMember  //簇成员节点故障
};


// 外层优化相关结构
struct ClusterMetrics
{
    double avg_intra_hops;      // 簇内平均跳数
    double diameter;             // 簇直径
    uint32_t inter_links_count;  // 跨轨链路数量
    double min_link_lifetime;    // 最短链路寿命
    double max_boundary_util;    // 最大边界利用率
};

struct TwoLayerConfig
{
    double alpha;           // 时延权重
    double beta;            // 稳定性权重
    double gamma;           // 簇数偏离/约束惩罚权重
    uint32_t target_k;      // 目标簇数（可选）
    uint32_t k_min;        // 最小簇数
    uint32_t k_max;        // 最大簇数
    uint32_t max_outer_iter; // 外层最大迭代次数
    double convergence_threshold; // 收敛阈值
    uint32_t D_max;        // 最大簇直径
    uint32_t M_min;        // 最小跨轨链路数
    uint32_t tabu_size;    // 禁忌表大小
    uint32_t stall_limit;  // 连续停滞迭代上限
};

struct ClusterState
{
    vector<Cluster> clusters;
    std::map<Ptr<Node>, Satellite> node_mapping;

    ClusterState() = default;
    ClusterState(const vector<Cluster>& c,
                 const std::map<Ptr<Node>, Satellite>& nm)
        : clusters(c), node_mapping(nm) {}
};

struct CostState
{
    vector<vector<int>> adj;
    vector<ClusterMetrics> metrics;
    double J_delay = 0.0;
    double J_stable = 0.0;
    bool is_connected = false;
    double cost = std::numeric_limits<double>::max();
};


/*
 * satellite network dynamic cluster algorithm
 * Three phase: 1. intitial cluster 2. game cluster 3. select cluster head
 */
class SatCluster
{
public:
std::unordered_set<State> m_state;
uint32_t fault_flag; //故障判断标志位
//require satellite num
vector<Satellite> m_satellite;
vector<Cluster> m_cluster;

static TypeId GetTypeId (void);

/** Get the cluster max and min value
 * 
 * \param sat_node select a node randomly()
 * 
*/
void ClusterSize(const vector<vector<int>>& adj, uint32_t sat_node);

/**
 * Initial cluster
 * 
*/
void  InitialCluster(NodeContainer node);

/**
 * Get the node's connection degree in cluster that includes the node
 * 
*/
uint32_t GetDegree(Ptr<Node> node, uint32_t id);

/***
 * 
*/
vector<Satellite> OptionalNode(Cluster& cluster, NodeContainer& nodes);

/** 
 * Get adjaceny list
 * \param
 * \return
 */
vector<vector<int>> AdjacenyList(NodeContainer node);

/**
 * BFS algorithm
 * \param
 * \return
*/ 
void bfs(const vector<vector<int>>& adj, const int& start, vector<bool>& visit, const int& depth, vector<vector<int>>& cluster);

/**
 * Get the max connectivity degree and node in the cluster
 * \param
 * \return 
*/
pair<int,int> ConnectivtyMaxDegree(const vector<vector<int>>& adj, vector<int>& a);

/**
 * Get the max connectivity degree and node in the cluster
 * 
*/
std::unordered_set<uint32_t> MinDegree(const vector<vector<int>>& adj, vector<Satellite>& sat);

/** Get the min connectivity degree with no visit--select the node with no visit
 * 
 * 
*/
uint32_t ConnectivityMinDegree(const vector<int>& a, const vector<vector<int>>& adj, const vector<bool>& visit);

/** Get the each node's connectivty degree int the cluster
 * 
*/
void MergeCluster(vector<Cluster>& clusters, NodeContainer node, const vector<vector<int>>& adj);

/** Get the cluster's fitness
 * 
*/
double CalculateFitness(const Cluster& cluster);

/** Get the node's adjacent cluster (canditate's cluster)
 * 
*/
std::unordered_set<uint32_t> CanditateCluster(const Satellite& sat, NodeContainer node);

/** Determine whether the node has migrated 
 * 
*/
bool IsMigrate(const Satellite& sat, const vector<Cluster>& clusters, NodeContainer node);

/** Migrate the satellite node
 * 
*/
void MigrateSatellite(Satellite& sat, vector<Cluster>& newClusters, NodeContainer node);

/** Iterate all the satellite node 
 * 
*/
void IterateClusters(vector<Cluster>& clusters, int maxIterations, NodeContainer node);

/** Calculate the node of utility 
 * 
*/
double CalculateUtility(const uint32_t& id, const uint32_t& size, const uint32_t& index, const vector<vector<int>>& adj, NodeContainer node); 

/** Elect the cluster head
 * 
*/
void ElectClusterHead(Cluster& cluster, NodeContainer node);

/** Full Connectivity  dfs--input NodeContainer node changes the vector<int>cluster
 * 
 * 
*/
void dfs(std::map<uint32_t,uint32_t>& visit, const Cluster& cluster, const vector<vector<int>>& adj, uint32_t v);

/** Full Connectivity 
 * 
 * 
*/
bool IsFullConnectivity(const Cluster& cluster, const vector<vector<int>>& adj);

//bool IsFullConnectivity(vector<Cluster>& clusters, vector<vector<int>> adj);

/** Fault judge
 * 
*/
void  FaultType(uint32_t id, uint32_t type, vector<Cluster>& clusters, NodeContainer nodes);

/** Fault recovery
 * 
*/
vector<NodeContainer> FaultRecovery(uint32_t node_id, NodeContainer nodes);

/**  Inital cluster data form transform
 * 
 */
vector<NodeContainer> InitialTransform(NodeContainer node);

/** Dynamic cluster data form transform
 * 
*/
vector<NodeContainer> DynamicTransform(int iter_num, NodeContainer nodes, int& dis_flag, int& con_flag);

    
// 状态备份与恢复
    ClusterState BackupState();
    void RestoreState(const ClusterState& state);


// ========== 外层优化新增函数 ==========
    /**
     * 双层优化主入口
     * \param nodes 所有卫星节点
     * \param config 双层优化配置参数
     * \return 最优分簇结果
     */
    vector<NodeContainer> TwoLayerClustering(NodeContainer nodes, const TwoLayerConfig& config);
    
    /**
     * 计算簇的性能指标
     * \param nodes 节点容器
     * \param adj 邻接表
     * \return 各簇的性能指标
     */
    vector<ClusterMetrics> ComputeClusterMetrics(NodeContainer nodes, 
                                                  const vector<vector<int>>& adj);

    /**
     * 统一计算代价与指标
     */
    CostState EvaluateCost(NodeContainer nodes, const TwoLayerConfig& config);

    /**
     * 提供一组默认的 TwoLayerConfig 参数，便于直接调用。
     */
    static TwoLayerConfig MakeDefaultTwoLayerConfig();
    
    /**
     * 计算时延代价 J_delay(k)
     * \param metrics 簇性能指标
     * \param nodes 节点容器
     * \return 时延代价
     */
    double ComputeDelayTarget(const vector<ClusterMetrics>& metrics, NodeContainer nodes,
                             const vector<vector<int>>& adj);
    
    /**
     * 计算稳定性代价 J_stable(k)
     * \param metrics 簇性能指标
     * \param adj 邻接表
     * \return 稳定性代价
     */
    double ComputeStabilityTarget(const vector<ClusterMetrics>& metrics, 
                                   const vector<vector<int>>& adj);
    
    /**
     * 检查簇间图连通性
     * \param adj 邻接表
     * \return 是否连通
     */
    bool CheckInterClusterConnectivity(const vector<vector<int>>& adj);
    
    /**
     * 构建簇间图
     * \param adj 节点邻接表
     * \return 簇间邻接表
     */
    vector<vector<uint32_t>> BuildClusterGraph(const vector<vector<int>>& adj);

    vector<vector<uint32_t>> ComputeClusterDistances(const vector<vector<uint32_t>>& cluster_graph);
    bool IsClusterGraphConnected(const vector<vector<uint32_t>>& cluster_graph);
    
    /**
     * 寻找分裂目标簇
     * \param metrics 簇性能指标
     * \param adj 邻接表
     * \param config 配置参数
     * \return 目标簇索引，-1表示无合适目标
     */
    int FindSplitTarget(const vector<ClusterMetrics>& metrics, 
                        const vector<vector<int>>& adj,
                        const TwoLayerConfig& config);
    
    /**
     * 执行分裂操作
     * \param cluster_idx 待分裂簇索引
     * \param nodes 节点容器
     * \param adj 邻接表
     * \return 是否成功分裂
     */
    bool ExecuteSplit(uint32_t cluster_idx, NodeContainer nodes, 
                      const vector<vector<int>>& adj);
    
    /**
     * 寻找合并目标簇对
     * \param metrics 簇性能指标
     * \param adj 邻接表
     * \param config 配置参数
     * \return 目标簇对<i,j>，{-1,-1}表示无合适目标
     */
    pair<int, int> FindMergeTarget(const vector<ClusterMetrics>& metrics,
                                    const vector<vector<int>>& adj,
                                    const TwoLayerConfig& config);
    
    /**
     * 执行合并操作
     * \param cluster_i 簇i索引
     * \param cluster_j 簇j索引
     * \param nodes 节点容器
     * \return 是否成功合并
     */
    bool ExecuteMerge(uint32_t cluster_i, uint32_t cluster_j, NodeContainer nodes);
    
    /**
     * 计算簇直径（最大跳数）
     * \param cluster 簇
     * \param adj 邻接表
     * \return 簇直径
     */
    uint32_t ComputeClusterDiameter(const Cluster& cluster, const vector<vector<int>>& adj);
    
    /**
     * 计算簇内平均跳数
     * \param cluster 簇
     * \param adj 邻接表
     * \return 平均跳数
     */
    double ComputeAvgIntraHops(const Cluster& cluster, const vector<vector<int>>& adj);
    
    /**
     * 统计簇的跨轨链路数量
     * \param cluster 簇
     * \param adj 邻接表
     * \return 跨轨链路数量
     */
    uint32_t CountInterOrbitLinks(const Cluster& cluster, const vector<vector<int>>& adj);
    
    /**
     * 获取簇的最小边割
     * \param cluster 簇
     * \param adj 邻接表
     * \return 最小边割大小
     */
    uint32_t GetMinCut(const Cluster& cluster, const vector<vector<int>>& adj);
    
    /**
     * BFS计算簇内所有节点对的最短路径
     * \param cluster 簇
     * \param adj 邻接表
     * \return 距离矩阵
     */
    vector<vector<uint32_t>> ComputeAllPairsDistance(const Cluster& cluster, 
                                                      const vector<vector<int>>& adj);


private:
uint32_t begin_id; // 卫星节点起始ID
std::map<Ptr<Node>, Satellite> node_to_sat; //优化可以将Satellite转换成Satellite*，这可以对地址操作直接改变结构体对象的变量不需要每次更新
uint32_t Nmax; // Maximum number of nodes in cluster 
uint32_t Nmin; // Minimum number of nodes in cluster 
uint32_t Naver;

EventId m_dynamicClusterEvent;


// 外层优化私有成员
std::deque<pair<uint32_t, string>> m_tabu_list; // 禁忌表 <k, operation>
double m_best_cost;                              // 最优代价
uint32_t m_best_k;                               // 最优簇数
vector<Cluster> m_best_clusters;                 // 最优分簇结果

};

#endif