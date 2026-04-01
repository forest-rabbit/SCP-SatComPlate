#include "cluster.h"
#include "all-node.h"
#include "para.h"
#include <cstdint>

NS_LOG_COMPONENT_DEFINE("OpenFlowSDNExample-cluster");

namespace ns3
{
  SatCluster sat;
  NodeContainer allnodes;
  std::vector<Ptr<LinkUtilizationMonitor>> monitors;
  std::vector<NodeContainer> satClusterNodes;          // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首 

  void UseMaxUtilization(const vector<Ptr<LinkUtilizationMonitor>> &monitors, NodeContainer node, const vector<vector<int>> &adj, vector<NodeContainer> &nodes)
  {
    if (Simulator::Now() >= Seconds(totalTimeStep)) return; // 终止递归调度
    cout<<"Time: "<<Simulator::Now().GetSeconds()<<endl;

    // ////////////////故障模块测试////////////////
    // SetLinkFault(node);
    // std::unordered_map<uint32_t,uint32_t> fault = JudgeFault(node);
    // for(const auto& it : fault)
    // {
    //     sat.FaultType(it.first, it.second, sat.m_cluster, node);
    // }
    // sat.FaultRecovery(node);
    // vector<vector<int>> adj= sat.AdjacenyList(node); //update adjList
    // if( sat.IsFullConnectivity(sat.m_cluster, adj) != true)
    // {
    //     cout<<endl;
    //     cout<<"**********Recluster**********"<<endl;
    //     sat.m_cluster.clear();
    //     sat.InitialCluster(node);
    // }
    //////////////////

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
    nodes = sat.DynamicTransform(100, node, d_flag, c_flag);   // 100为迭代次数
    satClusterNodes.assign(nodes.begin(), nodes.end());

    // if(Simulator::Now().GetSeconds() >= 40 && !flag){
    //   flag = true;
    //   uint32_t idx = 6;
    //   if(satellites.size() <= idx) idx = satellites.size() - 1;
    //   std::cout << "子控制器id:" << satellites[idx].Get(1)->GetId() << " 故障，切换至id:" << satellites[idx].Get(0)->GetId() << std::endl;
    // }
    
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

    // 重新安排自己再次被调度
    if(!_sim) Simulator::Schedule(Seconds(clusterUpdateStep), &UseMaxUtilization, monitors, node, adj, nodes);

  }

	void OutputNodeInfo(const vector<Ptr<LinkUtilizationMonitor>> &monitors)
	{
		// 打开文件
		std::ofstream file;
		file.open("examples/sdn-controller/output/linkUtilization.txt", std::ios::app);

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
    nodes = sat.InitialTransform(node); 
    satClusterNodes.assign(nodes.begin(), nodes.end());
    allnodes = node;

    /////////////迭代部分/////////////////////
    // for(auto& it : sat.m_cluster)
    // {
    //     it.fitness = sat.CalculateFitness(it);
    // }
    // sat.IterateClusters(sat.m_cluster, 100, total_node);

    // cout<<"******* 3.The select cluster head phase *******"<<endl;
    // uint32_t tt =0;
    // for(auto& it : sat.m_cluster)
    // {
    //     cout<<"cluster "<<tt+1<<":";
    //     sat.ElectClusterHead(it, total_node);
    //     cout<<" CH: "<<it.cluster_head<<endl;
    //     tt++;
    // }
    // cout<<endl;
    ///////////////////////////////////////////

    // change the satellite node's link utilization --初始化分簇部分
    // std::vector<Ptr<LinkUtilizationMonitor>> monitors;
    for (uint32_t i = 0; i < node.GetN(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
        monitor->SetLinkCapacity(linkBandwidth);
        monitor->SetStopTime(totalTimeStep);
        monitor->StartMonitoring(node.Get(i));
        // std::cout << "nodeID:" << i << "\t最大链路利用率：" << monitor->max_utilization << std::endl;
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

    // // 安排 UseMaxUtilization 函数第一次被调度
    //Simulator::Schedule(Seconds(clusterUpdateStep), &UseMaxUtilization, monitors, node, adj, nodes);
    if(_DynamicCluster && !_sim){
      // 安排 UseMaxUtilization 函数第一次被调度
      Simulator::Schedule(Seconds(clusterUpdateStep), &UseMaxUtilization, monitors, node, adj, nodes);
    }

		if(_NodeUiliz) Simulator::Schedule(Seconds(1.0), &OutputNodeInfo, monitors);
  }

  // 簇首重选举，新的分簇结果更新到satClusterNodes
  // 输入参数可以调整
  void RelectCluster(uint32_t breakID, NodeContainer node, vector<NodeContainer> &nodes){
    if(_CtrlInfoOutput) cout << "检测到从控制器 " << breakID << " 故障，重新分簇" << endl;
    // 这里调用新的簇首选择策略/分簇策略
    // 需要确认分簇结果是否更新到管控架构中
    nodes.clear();
    nodes = sat.FaultRecovery(breakID, node);
    if(_CtrlInfoOutput){
      cout<<"*****簇首重选举结果*****"<<endl;
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
    }
  }
}