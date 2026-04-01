#ifndef SATROUTING_H
#define SATROUTING_H

#include "para.h"
#include "cluster.h"
//#include "all-node.h"
#include <ns3/node.h>
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/dsr-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/olsr-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/global-router-interface.h"
#include "ns3/simulation-singleton.h"
#include "ns3/sat-global-route-manager-impl.h"
#include "ns3/simulator.h"
#include "ns3/packet-sink.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4.h"
#include "ns3/file-helper.h"

#include <string>
#include <cstring>
#include <vector>
#include <iomanip>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <fstream>
#include <iostream>

#define random(x)(rand()%x)
#define __rouECMP

namespace ns3 {

/**
 * Satellite Routing.
 *
 * It handles the creation and run of an experiment.
 */
class SatRouting
{
  public:
    SatRouting();

    /**
     * Handles the command-line parameters.
     * \param argc The argument count.
     * \param argv The argument vector.
     */
    void CommandSetup(int argc, char** argv);
    
    /**
     * 初始时刻计算路由表
     * \param totalsates 所有卫星节点.
     * \param clusters 分簇后所有卫星节点的向量，数组中的一行表示一簇，nodeContainer中的第一个卫星是簇首.
     * \param monitors 监控节点发送数据量容器.
     * \param consType 星座类型.
     * \param orbitNums 卫星轨道数.
     * \param satPerOrbits 每个轨道卫星数.
     */
    void InitialSatRouter(NodeContainer&Gnodes, NodeContainer& totalsates, std::vector<NodeContainer>& clusters, const vector<Ptr<LinkUtilizationMonitor>> &monitors, bool consType, uint32_t orbitNums, uint32_t satPerOrbits);
    
    /**
     * 周期性或故障情况更新路由表
     * \param totalsates 所有卫星节点.
     * \param clusters 分簇后所有卫星节点的向量，数组中的一行表示一簇，nodeContainer中的第一个卫星是簇首
     * \param monitors 监控节点发送数据量容器
     * \param consType 星座类型.
     * \param orbitNums 卫星轨道数.
     * \param satPerOrbits 每个轨道卫星数.
     */
    void UpdateSatRouter(NodeContainer& totalsates, std::vector<NodeContainer>& clusters, const vector<Ptr<LinkUtilizationMonitor>> &monitors, bool consType, uint32_t orbitNums, uint32_t satPerOrbits);

    /**
     * 周期性或故障情况更新路由表-对接仿真中心
     * \param totalsates 所有卫星节点.
     * \param clusters 分簇后所有卫星节点的向量，数组中的一行表示一簇，nodeContainer中的第一个卫星是簇首
     * \param consType 星座类型.
     * \param orbitNums 卫星轨道数.
     * \param satPerOrbits 每个轨道卫星数.
     */
    void UpdateSatRouterSim(NodeContainer& totalsates, std::vector<NodeContainer>& clusters, bool consType, uint32_t orbitNums, uint32_t satPerOrbits);
    
    /**
     * 删除给定节点对间的路由表-对接仿真中心
     * \param nodeSrc 源卫星节点.
     * \param nodeDst 目的卫星节点.
     */
    void DeleteSatRoutesSim(Ptr<Node> nodeSrc, Ptr<Node> nodeDst);

    /**
     * 主控制器失联情况，测试路由收敛时间
     * \param totalsates 所有卫星节点.
     * \param clusters 分簇后所有卫星节点的向量，数组中的一行表示一簇，nodeContainer中的第一个卫星是簇首
     * \param monitors 监控节点发送数据量容器
     * \param consType 星座类型.
     * \param orbitNums 卫星轨道数.
     * \param satPerOrbits 每个轨道卫星数.
     */
    void UpdateSatRouterForConvergence(NodeContainer& totalsates, std::vector<NodeContainer>& clusters, const vector<Ptr<LinkUtilizationMonitor>> &monitors, bool consType, uint32_t orbitNums, uint32_t satPerOrbits);

    /**
     * 更新簇的平均链路带宽利用率
     * \param j 簇ID号(0 ~ ClusterNum-1).
     */     
    void UpdateClusterLinkUtilization(uint32_t j);

    /**
     * 更新簇的负载分布指数
     * \param j 簇ID号(0 ~ ClusterNum-1).
     */ 
    void UpdateClusterLoadDIndex(uint32_t j);

    /**
     * 更新簇的平均链路带宽利用率-仿真中心对接
     * \param j 簇ID号(0 ~ ClusterNum-1).
     */     
    void UpdateClusterLinkUtilizationSim(NodeContainer& cluster, uint32_t index);

    /**
     * 更新簇的负载分布指数-仿真中心对接
     * \param j 簇ID号(0 ~ ClusterNum-1).
     */ 
    void UpdateClusterLoadDIndexSim(NodeContainer& cluster, uint32_t index);
    
    /**
     * 更新簇的直径
     * \param _clusterI 簇容器
     */ 
    void UpdateClusterDiameter(NodeContainer& _clusterI);

    /**
     * 更新簇的总节点代价信息
     * \param clusters 簇容器
     */    
    void UpdateClusterNetworkInfo(NodeContainer& clusters);

    /**
     * 计算平均缓冲队列长度
     * \param node 所有卫星
     */ 
    void CalAvgQueueLen(NodeContainer& nodes);

    /**
     * \brief Get the link utilization for each device
     *  保存至RlinkUtilization.csv文件     每一行：deviceIPAddress， linkutilization
    */
    void OutputTxPInfo(const vector<Ptr<LinkUtilizationMonitor>> &monitors);

    // double CalculateLinkUtilization(uint32_t bytes, uint32_t capacity);

    
    /**
     * \brief 更新簇内簇间代价信息
     * \param _satellite 簇容器
     * \param _clusters 簇容器的向量
     * \param _AbstractClusterNodes 抽象节点容器
     * \param _BoundaryNode 边界节点容器
     * \param monitors 链路数据量监控容器
     */  
    void UpdateClusterInfo(NodeContainer& _satellite, 
                           std::vector<NodeContainer>& _clusters, 
                           NodeContainer& _AbstractClusterNodes, 
                           std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>>& _BoundaryNode, 
                           const vector<Ptr<LinkUtilizationMonitor>> &monitors);

    /**
     * \brief 更新簇内簇间代价信息-仿真中心对接
     * \param _satellite 簇容器
     * \param _clusters 簇容器的向量
     * \param _AbstractClusterNodes 抽象节点容器
     * \param _BoundaryNode 边界节点容器
     */  
    void UpdateClusterInfoSim(NodeContainer& _satellite, 
                           std::vector<NodeContainer>& _clusters, 
                           NodeContainer& _AbstractClusterNodes, 
                           std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>>& _BoundaryNode);                       

    // void AddGroundCRouter(NodeContainer& gNodes, NodeContainer& satNodes, 
    //         std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode);

    /**
     * \brief 初始化计算簇内路由
     * \param clusters 簇容器（向量）.
     * \param BoundaryNode 边界节点.
     */ 
    void AddIntraCRouter(std::vector<NodeContainer>& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode);
    
    /**
     * \brief 初始化计算簇间路由
     * \param clusters 簇容器.
     * \param BoundaryNode 边界节点.
     */     
    void AddInterCRouter(NodeContainer& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode);
    
    /**
     * \brief 更新簇内路由
     * \param clusters 簇容器（向量）.
     * \param BoundaryNode 边界节点.
     */        
    void RecomputeIntraCRouter(std::vector<NodeContainer>& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode);
    
    /**
     * \brief 更新簇间路由
     * \param clusters 簇容器.
     * \param BoundaryNode 边界节点.
     */     
    void RecomputeInterCRouter(NodeContainer& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode);
    
    /**
     * \brief 协同簇内簇间路由
     * \param clusters 簇容器（向量）.
     * \param nodes 所有卫星节点.
     * \param AbstractClusterNodes 抽象簇节点.
     * \param BoundaryNode 边界节点.
     * \param ConsType 星座类型.
     * \param Orbit 卫星轨道数.
     * \param SatPerOrbit 每个轨道卫星数.
     */     
    void CoordinateIntraInterCRouter(std::vector<NodeContainer>& clusters, NodeContainer& nodes, NodeContainer& AbstractClusterNodes, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode, bool ConsType, uint32_t Orbit, uint32_t SatPerOrbit);
    
    /**
     * \brief 添加簇内备份路由
     * \param clusters 簇容器（向量）.
     * \param nodes 所有卫星节点.
     * \param ConsType 星座类型.
     * \param Orbit 卫星轨道数.
     * \param SatPerOrbit 每个轨道卫星数.
     */ 
    void AddIntraCBackupRouter(std::vector<NodeContainer>& clusters, NodeContainer& nodes, bool ConsType,uint32_t Orbit, uint32_t SatPerOrbit);
    
    /**
     * \brief 删去抽象节点间的连接关系及IP
     * \param AbstractClusterNodes 抽象簇节点.
     */ 
    void DeleteIPforAbstractNode(NodeContainer& AbstractClusterNodes);
    
    /**
     * \brief 获取节点1个接口的第0个IP地址
     * \param node 节点指针.
     * \return 对应的IP地址.
     */    
    Ipv4Address GetIPAddress(Ptr<ns3::Node> node);

    /**
     * \brief 根据IP地址获取节点指针
     * \param dIPAddress IP地址.
     * \return 对应的节点指针.
     */       
    Ptr<Node> GetNodefromIP(Ipv4Address dIPAddress);

    /** 
     * \brief 查找节点的邻居节点
     * \param node 节点指针.
     * \param mode 查找模式.
     *        若node为卫星节点，mode=0；
     *        若node为抽象簇节点，mode=1；
     * \return 邻居节点容器.
     */
    NodeContainer FindNeibors(Ptr<Node> node, bool mode);
    
    /**
     * \brief 判断两个节点是否连接
     * \param node1 节点1指针.
     * \param node2 节点2指针.
     * \param mode 查找模式.
     *      若node为卫星节点，mode=0；
     *      若node为抽象簇节点，mode=1；
     * \return 若连接返回true，否则返回false.
     */   
    bool IsConnect(Ptr<Node> node1, Ptr<Node> node2, bool mode);
    
    /**
     * \brief 获取节点对之间的网卡对
     * \param node1 节点1指针.
     * \param node2 节点2指针.
     * \return 网卡对（指针）.
     */    
    std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> GetDevicesFromNodes(Ptr<Node> node1, Ptr<Node> node2);

    /**
     * \brief 普通卫星故障，子控制器重计算路由表
     *        all-node.cc 调用
     */
    void RecomputeRouteInfo(void);
    
    /**
     * \brief 获取主路径节点-仿真中心对接
     * \param ScrNode 源节点.
     * \param DestNode 目的节点.
     * \param nodes 所有卫星节点容器.
     * \return 主路径节点容器.
     */    
    NodeContainer GetMainPathNodesSim(Ptr<Node> ScrNode, Ptr<Node> DestNode, NodeContainer& nodes);
    
    /**
     * \brief 获取备份路径节点-仿真中心对接
     * \param ScrNode 源节点.
     * \param DestNode 目的节点.
     * \param mainPathNodes 主路径节点容器.
     * \param nodes 所有卫星节点容器.
     * \return 备份路径节点容器.
     */        
    NodeContainer GetBackupPathNodesSim(Ptr<Node> ScrNode, Ptr<Node> DestNode, NodeContainer& mainPathNodes, NodeContainer& nodes);
    
    /**
     * \brief 根据IP地址获取节点ID
     * \param ip IP地址.
     * \return 节点ID.
     */     
    uint32_t GetIdFromIp(Ipv4Address ip);
    
    /**
     * \brief 从路由表中获取下一跳
     * \param ScrNode 源节点.
     * \param DestNode 目的节点.
     * \return 下一跳路由表项指针.
     */    
    Ipv4RoutingTableEntry* GetNextFromTable(Ptr<Node> ScrNode, Ptr<Node> DestNode);
    
    /**
     * \brief 从路由表中获取备份下一跳
     * \param ScrNode 源节点.
     * \param DestNode 目的节点.
     * \param nodes 所有主路径卫星节点容器.
     * \return 备份下一跳路由表项指针.
     */     
    Ipv4RoutingTableEntry* GetBackupNextFromTable(Ptr<Node> ScrNode, Ptr<Node> DestNode, NodeContainer& nodes);
    
    /**
     * \brief 更新主路径-仿真中心对接
     * \param nodes 所有卫星节点容器.
     */     
    void GetMainPathSim(NodeContainer& nodes);
    
    /**
     * \brief 更新主路径-仿真中心对接
     * \param nodes 所有卫星节点容器.
     * \param deletenodes 要删除的卫星节点容器(主路径卫星).
     * \param remainnodes 剩余卫星节点容器(在这些卫星上计算备份路径).
     */    
    void GetBackupPathSim(NodeContainer& nodes, NodeContainer& deletenodes, NodeContainer& remainnodes);
    
    /**
     * \brief 删除节点
     * \param nodes 所有卫星节点容器.
     * \param deletenodes 要删除的卫星节点容器(主路径卫星).
     * \return 剩余卫星节点容器.
     */    
    NodeContainer DeleteNode(NodeContainer& nodes, NodeContainer& deletenodes);

  private:    
    std::string m_CSVfileName{"sat-routing.output.csv"};    //!< CSV filename.
    int m_nSinks{10};                                       //!< Number of sink nodes.
    std::string m_protocolName{"AODV"};                     //!< Protocol name.
    bool m_traceMobility{false};                            //!< Enable mobility tracing.
    bool m_flowMonitor{true};                               //!< Enable FlowMonitor.
    uint8_t RouCmpTimes;                                    //!< Total routing compute times. 
    
    std::vector<double>   m_averButilization;                             // 簇的平均带宽利用率
    std::vector<double>   m_loadDIndex;                                   // 簇的负载分布指数
    std::vector<double>   m_clusterDiameter;                              // 簇直径
    std::vector<std::map<ns3::Ipv4Address, double>> m_cluster;            // 簇内各端口链路带宽利用率
    std::map<ns3::Ipv4Address, double> m_RlinkUtilization;                // 卫星各端口链路带宽利用率
    std::map<ns3::Ipv4Address, std::vector<double>> m_clusterNetworkInf;  // 加权簇网络信息

    NodeContainer AbstractClusterNodes;                                   // 抽象簇节点容器
    NodeContainer AbstractClusterNodesBackup;                             // 备份抽象簇节点容器

    double total_duration_second;                                         //!< 总路由计算时间.
    double avgRouCompt_second = 0.0;                                      //!< 平均路由计算时间.
    double total_intraCduration_second;                                   //!< 总簇内路由计算时间.
    double avgintraCRouCompt_second = 0.0;                                //!< 平均簇内路由计算时间.
    double total_interCduration_second;                                   //!< 总簇间路由计算时间.
    double avginterCRouCompt_second = 0.0;                                //!< 平均簇间路由计算时间.
};

}
#endif