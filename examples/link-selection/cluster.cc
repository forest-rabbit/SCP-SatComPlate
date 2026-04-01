#include "cluster.h"
#include "para.h"
#include <cstdint>
#include <fstream>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <cmath>

NS_LOG_COMPONENT_DEFINE("OpenFlowSDNExample-cluster");

namespace ns3
{
  // 聚类模式：0 双层分簇；1 链路利用率分簇；2 轨道分簇；3 连通性分簇
  static constexpr uint32_t kClusterModeTwoLayer = 0;
  static constexpr uint32_t kClusterModeUtilization = 1;
  static constexpr uint32_t kClusterModeOrbit = 2;
  static constexpr uint32_t kClusterModeConnectivity = 3;
  // 连通性分簇默认簇规模上下限（可按需调整）
  static constexpr uint32_t kConnMinSizeDefault = 4;
  static constexpr uint32_t kConnMaxSizeDefault = 16;

  SatCluster sat;
  NodeContainer allnodes;
  std::vector<Ptr<LinkUtilizationMonitor>> monitors;
  std::vector<NodeContainer> satClusterNodes;          // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首 

  // 指标输出文件
  static std::ofstream g_metricsCsv;
  static TwoLayerConfig g_metricsConfig = SatCluster::MakeDefaultTwoLayerConfig();

  void EnsureMetricsFile()
  {
    if (!g_metricsCsv.is_open())
    {
      g_metricsCsv.open("examples/link-selection/output/cluster_metrics"+to_string(_clusterMode)+".csv", std::ios::out | std::ios::trunc);
      g_metricsCsv << "time,tag,k,avg_size,size_std,size_min,size_max,"
                   << "diameter_avg,diameter_max,avg_intra_hops,inter_links_sum,"
                   << "max_boundary_util,J_delay,J_stable,cost,is_connected"
                   << std::endl;
    }
  }

  void LogClusterMetrics(const std::string& tag, const NodeContainer& nodes)
  {
    EnsureMetricsFile();
    double now = Simulator::Now().GetSeconds();

    auto adj = sat.AdjacenyList(nodes);
    auto metrics = sat.ComputeClusterMetrics(nodes, adj);
    bool connected = sat.CheckInterClusterConnectivity(adj);
    CostState cost_state = sat.EvaluateCost(nodes, g_metricsConfig);

    double k = static_cast<double>(sat.m_cluster.size());
    double sum = 0.0;
    for (const auto& c : sat.m_cluster) sum += c.satellites.size();
    double avg_size = k > 0 ? sum / k : 0.0;
    double var = 0.0;
    for (const auto& c : sat.m_cluster)
    {
      double diff = c.satellites.size() - avg_size;
      var += diff * diff;
    }
    double size_std = k > 0 ? std::sqrt(var / k) : 0.0;

    double size_min = sat.m_cluster.empty() ? 0.0 : sat.m_cluster.front().satellites.size();
    double size_max = size_min;
    double diameter_sum = 0.0;
    double diameter_max = 0.0;
    double boundary_max = 0.0;
    double intra_sum = 0.0;
    double inter_links_sum = 0.0;
    for (const auto& m : metrics)
    {
      diameter_sum += m.diameter;
      diameter_max = std::max(diameter_max, static_cast<double>(m.diameter));
      boundary_max = std::max(boundary_max, m.max_boundary_util);
      intra_sum += m.avg_intra_hops;
      inter_links_sum += m.inter_links_count;
    }
    double diameter_avg = !metrics.empty() ? diameter_sum / metrics.size() : 0.0;
    double avg_intra_hops = !metrics.empty() ? intra_sum / metrics.size() : 0.0;

    for (const auto& c : sat.m_cluster)
    {
      size_min = std::min<size_t>(size_min, c.satellites.size());
      size_max = std::max<size_t>(size_max, c.satellites.size());
    }

    g_metricsCsv << now << "," << tag << "," << k << "," << avg_size << "," << size_std
                 << "," << size_min << "," << size_max
                 << "," << diameter_avg << "," << diameter_max
                 << "," << avg_intra_hops << "," << inter_links_sum
                 << "," << boundary_max
                 << "," << cost_state.J_delay << "," << cost_state.J_stable << "," << cost_state.cost
                 << "," << (connected ? 1 : 0) << std::endl;
  }

  // 默认双层分簇参数配置
  static TwoLayerConfig defaultTwoLayerConfig = SatCluster::MakeDefaultTwoLayerConfig();

  // 轨道分簇定时回调：静态划分，但周期性记录指标
  void OrbitClusterTick(NodeContainer node)
  {
    if (Simulator::Now() >= Seconds(totalTimeStep)) return;

    LogClusterMetrics("Orbit", node);
    Simulator::Schedule(Seconds(clusterUpdateStep), &OrbitClusterTick, node);
  }

  // 双层分簇定时回调
  void TwoLayerClusterTick(NodeContainer node)
  {
    if (Simulator::Now() >= Seconds(totalTimeStep)) return;

    // 处理 max_utilization
    for (auto monitor : monitors) 
    {
        double max_util = monitor->max_utilization;
        //cout<<"Time: "<<Simulator::Now().GetSeconds()<<" Node: "<<monitor->m_node->GetId()<<" Link: "<<max_util<<endl;
        
        // 将 max_util 传递给其他函数进行处理
        for(auto& it : sat.m_cluster)
        {
            for(auto& t : it.satellites)
            {
                if(t.id + sateBegID == monitor->m_node->GetId())
                {
                    t.max_utilization = max_util;
                    // if(t.id == 0) cout<<"节点最大链路利用率: "<<max_util<<endl;
                    break;
                }
            }
        }    
    }
    // cluster phase
    for(auto& it : sat.m_cluster)
    {
      it.fitness = sat.CalculateFitness(it);
    }

    TwoLayerConfig cfg = defaultTwoLayerConfig;
    std::vector<NodeContainer> nodes = sat.TwoLayerClustering(node, cfg);
    satClusterNodes.assign(nodes.begin(), nodes.end());

    LogClusterMetrics("two-layer", node);

    // 打印一次当前分簇结果
    std::cout << "[TwoLayer] t=" << Simulator::Now().GetSeconds() << "s, k=" << nodes.size() << std::endl;
    for(uint32_t t = 0; t != nodes.size(); ++t)
    {
      std::cout << "cluster " << t+1 << ":";
      for(auto it = nodes[t].Begin(); it != nodes[t].End(); ++it)
      {
        std::cout << (*it)->GetId() << " ";
      }
      std::cout << std::endl;
    }

    // 检查每个簇的连通性
    uint32_t tt = 0;
    std::vector<vector<int>>adj = sat.AdjacenyList(node); 
    for(auto& it : sat.m_cluster)
    {
        cout<<"cluster "<<tt+1<<":";
        cout << "簇的连通性为: " << sat.IsFullConnectivity(it, adj) << "\n";
        tt++;
    }
    cout<<endl;

    // 下一次调度
    Simulator::Schedule(Seconds(clusterUpdateStep), &TwoLayerClusterTick, node);
  }

  // 连通性分簇定时回调
  void ConnectivityClusterTick(NodeContainer node, uint32_t minSize, uint32_t maxSize)
  {
    if (Simulator::Now() >= Seconds(totalTimeStep)) return;

    std::vector<NodeContainer> nodes;
    ConnectivityPartition(node, nodes, minSize, maxSize);
    satClusterNodes.assign(nodes.begin(), nodes.end());
    // nodes = sat.InitialTransform(node); 
    // satClusterNodes.assign(nodes.begin(), nodes.end());
    // 下一次调度
    Simulator::Schedule(Seconds(clusterUpdateStep), &ConnectivityClusterTick, node, minSize, maxSize);
  }


  // 基于最大链路利用率的动态分簇回调函数
  void UseMaxUtilization(const vector<Ptr<LinkUtilizationMonitor>> &monitors, NodeContainer node, const vector<vector<int>> &adj, vector<NodeContainer> &nodes)
  {
    if (Simulator::Now() >= Seconds(totalTimeStep)) return; // 终止递归调度
    cout<<"Time: "<<Simulator::Now().GetSeconds()<<endl;

    // 处理 max_utilization
    for (auto monitor : monitors) 
    {
        double max_util = monitor->max_utilization;
        //cout<<"Time: "<<Simulator::Now().GetSeconds()<<" Node: "<<monitor->m_node->GetId()<<" Link: "<<max_util<<endl;
        
        // 将 max_util 传递给其他函数进行处理
        for(auto& it : sat.m_cluster)
        {
            for(auto& t : it.satellites)
            {
                if(t.id + sateBegID == monitor->m_node->GetId())
                {
                    t.max_utilization = max_util;
                    // if(t.id == 0) cout<<"节点最大链路利用率: "<<max_util<<endl;
                    break;
                }
            }
        }    
    }
    // cluster phase
    for(auto& it : sat.m_cluster)
    {
      it.fitness = sat.CalculateFitness(it);
    }

    //////////////////////迭代和簇首选举模块单独测试//////////////////
    // sat.IterateClusters(sat.m_cluster, 100, node);
    // cout<<"******* 3.The select cluster head phase *******"<<endl;
    // uint32_t tt = 0;
    // for(auto& it : sat.m_cluster)
    // {
    //     cout << "簇的连通性为: " << sat.IsFullConnectivity(it, adj) << "\n";
    //     cout<<"cluster "<<tt+1<<":";
    //     sat.ElectClusterHead(it, node);
    //     cout<<" CH: "<<it.cluster_head<<endl;
    //     tt++;
    // }
    // cout<<endl;
    ////////////////////////////////////////////////////

    cout<<"*****动态分簇接口*****"<<endl;
    //只有链路分配场景才会调用初始分簇
    //nodes = sat.InitialTransform(node); 
    nodes = sat.DynamicTransform(100, node, d_flag, c_flag);   // 100为迭代次数
    satClusterNodes.assign(nodes.begin(), nodes.end());
    
    for(uint32_t t = 0; t != nodes.size(); t++)
    {
        cout<<"cluster "<<t+1<<":";
        NodeContainer::Iterator i;
        for(i = nodes[t].Begin (); i != nodes[t].End (); ++i)
        {
            cout<<(*i)->GetId()<<" ";
        }      
        cout<<endl;
    }
    // 输出指标
    LogClusterMetrics("LinkUtilization", node);
    // std::cout << "\n邻接表输出:" << std::endl;

    // for (uint32_t nodeAId = 0; nodeAId < allnodes.GetN(); ++nodeAId)
    // {
    //     Ptr<Node> nodeA = allnodes.Get(nodeAId);
    //     std::cout << "Node " << nodeA->GetId() << ": ";
    //     // 遍历节点A的设备
    //     for (uint32_t devA = 0; devA < nodeA->GetNDevices(); ++devA)
    //     {
    //         Ptr<NetDevice> netDeviceA = nodeA->GetDevice(devA);
    //         Ptr<PointToPointNetDevice> p2pNetDeviceA = DynamicCast<PointToPointNetDevice>(netDeviceA);

    //         // 检查设备是否为 PointToPointNetDevice
    //         if (p2pNetDeviceA && p2pNetDeviceA->IsLinkUp())
    //         {
    //             Ptr<Channel> channelA = p2pNetDeviceA->GetChannel();
    //             // 遍历通道上的所有设备，找出与 nodeA 连接的其他节点
    //             for (uint32_t devB = 0; devB < channelA->GetNDevices(); ++devB)
    //             {
    //                 Ptr<NetDevice> netDeviceB = channelA->GetDevice(devB);
    //                 // 跳过自身的设备
    //                 if (netDeviceB != netDeviceA)
    //                 {
    //                     Ptr<PointToPointNetDevice> p2pNetDeviceB = DynamicCast<PointToPointNetDevice>(netDeviceB);
    //                     // 确定连接的节点
    //                     Ptr<Node> nodeB = netDeviceB->GetNode();
    //                     std::cout << nodeB->GetId() << ",";
    //                 }
    //             }
    //         }
    //     }

    //     std::cout << std::endl;
    // }

    // 重新安排自己再次被调度
    Simulator::Schedule(Seconds(clusterUpdateStep), &UseMaxUtilization, monitors, node, adj, nodes);

  }

	void OutputNodeInfo(const vector<Ptr<LinkUtilizationMonitor>> &monitors)
	{
		// 打开文件
		std::ofstream file;
		file.open("examples/link-selection/output/linkUtilization.txt", std::ios::app);

    file << "currTime:" << Simulator::Now().GetSeconds() << std::endl;
		for (uint32_t i = 0; i < monitors.size(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = monitors[i];
        for(auto iter = monitor->m_totalDevice.begin(); iter != monitor->m_totalDevice.end(); iter++){
          file << "nodeID:" << i << "\tif:" << iter->first << "\trecvBytes:" << iter->second << std::endl;
        }
    }
		file.close();

		Simulator::Schedule(Seconds(1.0), &OutputNodeInfo, monitors);
	}

//sim==1——面向连接的动态分簇接口（仿真中心对接）
  void UpdateCluster(NodeContainer node)
  {
    std::vector<NodeContainer> nodes; //新的分簇结果
    // 用于存储每个节点的最大 DataLoad 值（单位为 Kbps）
    std::unordered_map<uint32_t, uint32_t> maxDataLoadMap;

    //更新节点端口处的平均链路利用率
    for (uint32_t i = 0; i < node.GetN(); ++i)
    {
      uint32_t maxDataLoad = 0; 
      Ptr<Node> m_node = node.Get(i);
      uint32_t id = m_node->GetId();
      for (uint32_t j = 1; j < m_node->GetNDevices(); ++j)
      {
        bool is_ground = false;
        Ptr<NetDevice> device = m_node->GetDevice(j);
        Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(device);
        if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//判断链路是否存在
        {
          // 判断是否连接地面节点
          Ptr<Channel> channel = p2p_dev->GetChannel();
          for(uint32_t k = 0; k < channel->GetNDevices(); k++)
          {
            Ptr<NetDevice> adj_device = channel->GetDevice(k);
            if( adj_device != device)
            {
              Ptr<Node> adj_node = adj_device->GetNode();
              if(adj_node->GetId() < sateBegID) {
                continue;
                is_ground = true;
              }; // 避免地面节点
            }
          }
          if(is_ground) continue;

          DataRateValue dataRateValue;
          p2p_dev->GetAttribute("DataLoad", dataRateValue);
          DataRate dataRate = dataRateValue.Get();
          // 更新最大值
          uint32_t dataRateValueUint = dataRate.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
          if (dataRateValueUint > maxDataLoad)
          {
              maxDataLoad = dataRateValueUint;
          }
        }
      }
      // 将当前节点的最大 DataLoad 值存入 unordered_map
      maxDataLoadMap[id] = maxDataLoad;
    }
    // // 输出每个节点的最大 DataLoad 值
    // for (const auto &entry : maxDataLoadMap)
    // {
    //     cout << "Node " << entry.first << " Max DataLoad: " << entry.second << " Kbps" << endl;
    // }
    
    // 处理最大利用率--max_utilization
    for(auto& it : sat.m_cluster)
    {
        for(auto& t : it.satellites)
        {
          uint32_t node_id = t.id + sateBegID;
          if(maxDataLoadMap.find(node_id) != maxDataLoadMap.end())
          {
            t.max_utilization = maxDataLoadMap[node_id];
          }
        }
    }    
    for(auto& it : sat.m_cluster)
    {
      it.fitness = sat.CalculateFitness(it);
    }

    //cout<<"*****动态分簇接口*****"<<endl;
    nodes = sat.DynamicTransform(100, node, d_flag, c_flag);   // 100为迭代次数
    satClusterNodes.assign(nodes.begin(), nodes.end());

    //LogClusterMetrics("dynamic", node);
  }

//初始分簇--基于sim值判断否启动动态分簇
  void ActiveCluster(NodeContainer node, std::vector<NodeContainer> &nodes)
  {
    // 初始化分簇阶段
    std::vector<vector<int>>adj = sat.AdjacenyList(node); 
    // for(uint32_t i = 0; i < adj.size(); i++)
    // {
    //     cout<<"node: "<<i+7<<" "<<"adj: ";
    //     for(uint32_t j = 0; j < adj[i].size(); j++)
    //     {
    //         cout<<adj[i][j]+7<<" ";
    //     }
    //     cout<<endl;
    // }

    sat.ClusterSize(adj,0);
    allnodes = node;

    // 直接模式分簇：轨道/连通性（不依赖 sat 内部状态）
    if (_clusterMode == kClusterModeOrbit)
    {
      std::cout << "[Cluster] mode=orbit" << std::endl;
      OrbitPartitionCluster(node, sate_num, nodes);
      satClusterNodes.assign(nodes.begin(), nodes.end());

      // 将分簇结果同步回 sat.m_cluster，便于 LogClusterMetrics 正常计算规模/直径等
      sat.m_cluster.clear();
      sat.m_cluster.reserve(nodes.size());
      for (uint32_t ci = 0; ci < nodes.size(); ++ci)
      {
        Cluster c;
        c.fitness = 0.0;
        c.cluster_head = nodes[ci].Get(0)->GetId() - sateBegID; // 取簇内第一个节点作为簇首（轨道划分静态）
        c.candidate_head = c.cluster_head;
        for (auto it = nodes[ci].Begin(); it != nodes[ci].End(); ++it)
        {
          Satellite s;
          s.id = (*it)->GetId() - sateBegID;
          s.cluster_index = ci;
          s.max_utilization = 0.0;
          c.satellites.push_back(s);
        }
        sat.m_cluster.push_back(c);
      }
      // 周期性回调可按需添加：轨道划分通常静态，这里不调度
      for (uint32_t i = 0; i < node.GetN(); ++i) 
      {
        Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
        monitor->SetLinkCapacity(linkBandwidth);
        monitor->SetStopTime(totalTimeStep);
        monitor->StartMonitoring(node.Get(i));
        //std::cout << "nodeID:" << i << "\t最大链路利用率：" << monitor->max_utilization << std::endl;
        monitors.push_back(monitor);
        // 设置定时器，每秒钟检查一次最大利用率
        Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, monitor);
      }
      LogClusterMetrics("Orbit", node);
      // 周期记录轨道分簇指标
      Simulator::Schedule(Seconds(clusterUpdateStep), &OrbitClusterTick, node);
      return;
    }
    if (_clusterMode == kClusterModeConnectivity)
    {
      std::cout << "[Cluster] mode=connectivity" << std::endl;
      ConnectivityPartition(node, nodes, kConnMinSizeDefault, kConnMaxSizeDefault);
      satClusterNodes.assign(nodes.begin(), nodes.end());
      
      // nodes = sat.InitialTransform(node); 
      // satClusterNodes.assign(nodes.begin(), nodes.end());
      //Simulator::Schedule(Seconds(clusterUpdateStep), &ConnectivityClusterTick, node, kConnMinSizeDefault, kConnMaxSizeDefault);
      return;
    }

    // 需要 sat 状态的分簇：初始分簇 + 监测链路利用率
    nodes = sat.InitialTransform(node); 
    satClusterNodes.assign(nodes.begin(), nodes.end());



    // change the satellite node's link utilization --初始化分簇部分
    // std::vector<Ptr<LinkUtilizationMonitor>> monitors;
    for (uint32_t i = 0; i < node.GetN(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
        monitor->SetLinkCapacity(linkBandwidth);
        monitor->SetStopTime(totalTimeStep);
        monitor->StartMonitoring(node.Get(i));
        //std::cout << "nodeID:" << i << "\t最大链路利用率：" << monitor->max_utilization << std::endl;
        monitors.push_back(monitor);
        // 设置定时器，每秒钟检查一次最大利用率
        Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, monitor);
    }

    cout<<"*****初始化分簇结果*****"<<endl;
    for(uint32_t t = 0; t != nodes.size(); t++)
    {
        cout<<"cluster "<<t+1<<":";
        NodeContainer::Iterator i;
        for(i = nodes[t].Begin (); i != nodes[t].End (); ++i)
        {
            cout<<(*i)->GetId()<<" ";
        }      
        cout<<endl;
    }

    // 根据开关选择分簇方式
    if(_clusterMode == kClusterModeTwoLayer)
    {
      std::cout << "[Cluster] mode=two-layer" << std::endl;
      Simulator::Schedule(Seconds(clusterUpdateStep), &TwoLayerClusterTick, node);
    }
    else if(_clusterMode == kClusterModeUtilization || _DynamicCluster)
    {
      std::cout << "[Cluster] mode=link-utilization" << std::endl;
      // 安排 UseMaxUtilization 函数第一次被调度
      Simulator::Schedule(Seconds(clusterUpdateStep), &UseMaxUtilization, monitors, node, adj, nodes);
    }

		if(_NodeUiliz) Simulator::Schedule(Seconds(1.0), &OutputNodeInfo, monitors);
  }

  // 簇首重选举，新的分簇结果更新到satClusterNodes
  // 输入参数可以调整
  void RelectCluster(uint32_t breakID, NodeContainer node, vector<NodeContainer> &nodes){
    // 这里调用新的簇首选择策略/分簇策略
    // 需要确认分簇结果是否更新到管控架构中
    nodes.clear();
    nodes = sat.FaultRecovery(breakID, node);
    // if(_CtrlInfoOutput){
    //   cout<<"*****簇首重选举结果*****"<<endl;
    //   for(uint32_t t = 0; t != nodes.size(); t++)
    //   {
    //       cout<<"cluster "<<t+1<<":";
    //       NodeContainer::Iterator i;
    //       for(i = nodes[t].Begin (); i != nodes[t].End (); ++i)
    //       {
    //           cout<<(*i)->GetId()<<" ";
    //       }      
    //       cout<<endl;
    //   }
    // }
  }

  
  // 基于轨道索引的分簇：按轨道 (satPerOrbit) 将节点分组
  void OrbitPartitionCluster(NodeContainer node, uint32_t satPerOrbit, std::vector<NodeContainer> &nodes)
  {
    nodes.clear();
    if (satPerOrbit == 0) return;
    std::map<uint32_t, NodeContainer> orbitMap;
    for (uint32_t i = 0; i < node.GetN(); ++i)
    {
      Ptr<Node> n = node.Get(i);
      uint32_t id = n->GetId();
      uint32_t orbitIndex = id / satPerOrbit;
      orbitMap[orbitIndex].Add(n);
    }
    for (auto &p : orbitMap)
    {
      nodes.push_back(p.second);
    }
    satClusterNodes = nodes;
    LogClusterMetrics("orbit", node);
  }

  // 基于图连通性的分簇：按照连通分量分组，可选最小/最大簇规模约束
  void ConnectivityPartition(NodeContainer node, std::vector<NodeContainer> &nodes, uint32_t minSize, uint32_t maxSize)
  {
    nodes.clear();
    auto adj = sat.AdjacenyList(node);
    uint32_t n = adj.size();
    std::vector<bool> visited(n, false);

    // 先按连通分量生成粗粒度簇
    for (uint32_t i = 0; i < n; ++i)
    {
      if (visited[i]) continue;
      std::queue<int> q;
      std::vector<uint32_t> compIds; // 使用相对索引（begin_id 偏移）
      visited[i] = true;
      q.push(i);
      while (!q.empty())
      {
        int u = q.front();
        q.pop();
        compIds.push_back(static_cast<uint32_t>(u));
        for (int v : adj[u])
        {
          if (!visited[v])
          {
            visited[v] = true;
            q.push(v);
          }
        }
      }

      // 如果设置了最大规模，对该连通分量做二次拆分（保持每个子簇连通）
      if (maxSize > 0 && compIds.size() > maxSize)
      {
        std::unordered_set<uint32_t> compSet(compIds.begin(), compIds.end());
        std::unordered_map<uint32_t, bool> assigned;
        for (auto id : compIds) assigned[id] = false;

        for (auto id : compIds)
        {
          if (assigned[id]) continue;
          std::queue<uint32_t> subQ;
          NodeContainer chunk;
          uint32_t currentSize = 0;

          subQ.push(id);
          assigned[id] = true;

          while (!subQ.empty() && currentSize < maxSize)
          {
            uint32_t u = subQ.front();
            subQ.pop();
            chunk.Add(node.Get(u));
            currentSize++;

            if (currentSize >= maxSize) break;

            for (int v : adj[u])
            {
              uint32_t vv = static_cast<uint32_t>(v);
              if (compSet.find(vv) != compSet.end() && !assigned[vv])
              {
                assigned[vv] = true;
                subQ.push(vv);
              }
            }
          }

          nodes.push_back(chunk);
        }
      }
      else
      {
        NodeContainer comp;
        for (auto id : compIds)
        {
          comp.Add(node.Get(id));
        }
        nodes.push_back(comp);
      }
    }

    // 如果设置了最小规模，对过小簇尝试合并到相邻簇
    if (minSize > 0)
    {
      // 构建簇节点 ID 列表（相对索引）
      std::vector<std::vector<uint32_t>> clusterIds(nodes.size());
      uint32_t localBegin = node.Get(0)->GetId();
      for (size_t idx = 0; idx < nodes.size(); ++idx)
      {
        for (auto it = nodes[idx].Begin(); it != nodes[idx].End(); ++it)
        {
          clusterIds[idx].push_back((*it)->GetId() - localBegin);
        }
      }

      for (size_t i = 0; i < clusterIds.size(); ++i)
      {
        if (clusterIds[i].size() >= minSize || clusterIds[i].empty()) continue;

        size_t best = clusterIds.size();
        uint32_t bestEdge = 0;
        std::unordered_set<uint32_t> smallSet(clusterIds[i].begin(), clusterIds[i].end());

        for (size_t j = 0; j < clusterIds.size(); ++j)
        {
          if (i == j || clusterIds[j].empty()) continue;
          std::unordered_set<uint32_t> otherSet(clusterIds[j].begin(), clusterIds[j].end());
          uint32_t edgeCnt = 0;
          for (auto id : clusterIds[i])
          {
            for (int v : adj[id])
            {
              if (otherSet.find(static_cast<uint32_t>(v)) != otherSet.end())
              {
                edgeCnt++;
              }
            }
          }
          if (edgeCnt > bestEdge)
          {
            bestEdge = edgeCnt;
            best = j;
          }
        }

        // 如果没有边界连接，则合并到当前最大簇
        if (best == clusterIds.size())
        {
          size_t largest = clusterIds.size();
          uint32_t maxSizeCluster = 0;
          for (size_t j = 0; j < clusterIds.size(); ++j)
          {
            if (i == j || clusterIds[j].empty()) continue;
            if (clusterIds[j].size() > maxSizeCluster)
            {
              maxSizeCluster = clusterIds[j].size();
              largest = j;
            }
          }
          best = largest;
        }

        if (best < clusterIds.size())
        {
          clusterIds[best].insert(clusterIds[best].end(), clusterIds[i].begin(), clusterIds[i].end());
          clusterIds[i].clear();
        }
      }

      // 重建 nodes，过滤掉空簇
      std::vector<NodeContainer> merged;
      for (const auto& ids : clusterIds)
      {
        if (ids.empty()) continue;
        NodeContainer c;
        for (auto id : ids)
        {
          c.Add(node.Get(id));
        }
        merged.push_back(c);
      }
      nodes.swap(merged);
    }

    satClusterNodes = nodes;
    LogClusterMetrics("connectivity", node);
  }

}