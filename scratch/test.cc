#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/cluster-module.h"

#include <random>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("SimulationTest");

int access_flag = 0;
int d_flag = 0;

void UseMaxUtilization(const vector<Ptr<LinkUtilizationMonitor>>& monitors, NodeContainer node, SatCluster& sat, const vector<vector<int>>& adj) 
{
    if (Simulator::Now() >= Seconds(20)) 
    { // 假设仿真结束时间为20秒
        return; // 终止递归调度
    }
    cout<<"Time: "<<Simulator::Now().GetSeconds()<<endl;

    ////////////////故障模块测试////////////////
     SetLinkFault(node);
     std::unordered_map<uint32_t,uint32_t> fault = JudgeFault(node);
     for(const auto& it : fault)
     {
        sat.FaultType(it.first, it.second, sat.m_cluster, node);
     }
    //  sat.FaultRecovery(node);
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
                if(t.id == monitor->m_node->GetId())
                {
                    t.max_utilization = max_util;
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
    vector<NodeContainer> nodes = sat.DynamicTransform(100, node, d_flag, access_flag);
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

    // 重新安排自己再次被调度
    Simulator::Schedule(Seconds(1), &UseMaxUtilization, monitors, node, sat, adj);
}

int main (int argc, char *argv[])
{

    NS_LOG_UNCOND ("Simulation Test");
    int num_plane = 6;
    int num_sat_plane = 11;
  
    NodeContainer total_node;
    total_node.Create(num_plane * num_sat_plane);
    cout<<"the node num: "<<total_node.GetN()<<endl;

    PointToPointHelper link;
    link.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    link.SetChannelAttribute("Delay", StringValue("3ms"));
     
    Ipv4InterfaceContainer interfaces;
    InternetStackHelper stack;
    stack.Install(total_node);
    //the same ortbit 
    for(int i = 0; i < num_plane; i++)
    {
        for(int j = 0; j < num_sat_plane; j++)
        {
            int current_index = i*num_sat_plane + j;
            NodeContainer intra_container;
            if(j < num_sat_plane - 1)
            {
                int next_index = current_index + 1;
                intra_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            else
            {
                int next_index = i*num_sat_plane;
                intra_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            NetDeviceContainer intra_device = link.Install(intra_container);
            // Install Internet stack
            // InternetStackHelper intra_stack;
            // intra_stack.Install(intra_container);
            //IP address
            string sat_address1 = "10."+to_string(i+1)+"."+to_string(j+1)+"."+"0";//port 1
            Ipv4AddressHelper intra_ipv4;
            intra_ipv4.SetBase(Ipv4Address(sat_address1.c_str()), "255.255.255.0", "0.0.0.1");
            Ipv4InterfaceContainer intra_interfaces = intra_ipv4.Assign(intra_device);
            interfaces.Add(intra_interfaces); // first--the same orbit
        }
    }
    // the different orbit
    for(int i = 0; i < num_plane; i++)
    {
        for(int j = 0; j < num_sat_plane; j++)
        {
            int current_index = i*num_sat_plane + j;
            NodeContainer inter_container;
            if(i < num_plane - 1)
            {
                int next_index = (i+1)*num_sat_plane + j;
                inter_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            else
            {
                int next_index = j;
                inter_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            NetDeviceContainer inter_device = link.Install(inter_container);
            // Install Internet stack
            // InternetStackHelper inter_stack;
            // inter_stack.Install(inter_container);
            //IP address
            string sat_address2 = "10."+to_string((i+1)%(num_plane+1)+1)+"."+to_string(j+1)+"."+"0";//port 2
            Ipv4AddressHelper inter_ipv4;
            inter_ipv4.SetBase(Ipv4Address(sat_address2.c_str()), "255.255.255.0", "0.0.0.4");
            Ipv4InterfaceContainer inter_interfaces = inter_ipv4.Assign(inter_device);
            interfaces.Add(inter_interfaces); // second--the different orbbit
        }
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    // 预设端口号范围
    uint16_t portStart = 9; // well-known echo port number
    uint32_t numNodes = total_node.GetN();

    // 为每个节点分配一个唯一的端口号
    std::map<Ptr<Node>, uint16_t> nodePortMap;
    for(uint32_t i = 0; i < numNodes; ++i) 
    {
        uint16_t port = portStart + i; // 假设端口号从9开始，每个节点增加1
        nodePortMap[total_node.Get(i)] = port;
    }
    // 安装服务器应用程序到每个节点
    for(uint32_t i = 0; i < numNodes; ++i) 
    {
        UdpServerHelper server(nodePortMap[total_node.Get(i)]);
        ApplicationContainer apps = server.Install(total_node.Get(i));
        apps.Start(Seconds(1.0));
        apps.Stop(Seconds(20.0));
    }

    // 为每个节点配置客户端应用程序，使其向其他节点发送数据包
for(uint32_t i = 0; i < numNodes; ++i) {
    Ptr<Node> clientNode = total_node.Get(i);
    std::random_device rd; 
    std::mt19937 gen(rd()); 
    // 创建一个在[min, max]范围内均匀分布的分布器[0,interfaces.GetN()-1]
    std::uniform_int_distribution<> distr(0,numNodes-1);
    std::uniform_int_distribution<> dist(1000,2000);
    std::uniform_real_distribution<double> inter(0.001, 0.009);

    uint64_t packet_num = dist(gen);
    uint32_t random_number;
    double interval  = inter(gen);

    do {
        random_number = distr(gen);
    } while (random_number == i);

    Ptr<Ipv4> ip = total_node.Get(random_number)-> GetObject<Ipv4> ();
    Ipv4Address ip_address = ip->GetAddress(1,0).GetLocal();
    UdpClientHelper client(ip_address, portStart);

    client.SetAttribute("MaxPackets", UintegerValue(packet_num));
    client.SetAttribute("Interval", TimeValue(Seconds(interval)));
    client.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer apps = client.Install(clientNode);
    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(20.0));
}



    //test adlList
    SatCluster sat;
    vector<vector<int>>adj = sat.AdjacenyList(total_node); 
    // for(uint32_t i = 0; i < adj.size(); i++)
    // {
    //     cout<<"node: "<<i<<" "<<"adj: ";
    //     for(uint32_t j = 0; j < adj[i].size(); j++)
    //     {
    //         cout<<adj[i][j]<<" ";
    //     }
    //     cout<<endl;
    // }
    //sat.InitialCluster(total_node);

    vector<NodeContainer> nodes = sat.InitialTransform(total_node);
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
    std::vector<Ptr<LinkUtilizationMonitor>> monitors;
    for (uint32_t i = 0; i < total_node.GetN(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
        monitor->SetLinkCapacity(10000000);
        monitor->SetStopTime(20);
        monitor->StartMonitoring(total_node.Get(i));
        monitors.push_back(monitor);
        // 设置定时器，每秒钟检查一次最大利用率
        Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, monitor);
    }
    // // 安排 UseMaxUtilization 函数第一次被调度
    //sat.DynamicCluster(monitors, total_node, adj);
     Simulator::Schedule(Seconds(1), &UseMaxUtilization, monitors, total_node, sat, adj);



    

    // output node ID and IP address
    // for(int i = 0; i < num_plane*num_sat_plane; i++)
    // {
    //     Ptr<Node> node = total_node.Get(i);
    //     Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    //     for (uint32_t  devIndex = 0; devIndex < node->GetNDevices(); devIndex++)
    //     {
    //         cout << "Node ID: " << node->GetId() << endl;
    //         cout<<" IP Address: "<< ipv4->GetAddress(devIndex, 0).GetLocal() << endl;
    //         Ptr<NetDevice> dev = node->GetDevice(devIndex);
    //         Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
    //         if (p2pDev)
    //         {
    //             // 获取远端节点的ID
    //             Ptr<Channel> channel = p2pDev->GetChannel();
    //             for (uint32_t j = 0; j < channel->GetNDevices(); j++) 
    //             {
    //                 Ptr<NetDevice> adjacentDevice = channel->GetDevice(j);
    //                 if (adjacentDevice != dev) 
    //                 {
    //                     Ptr<Node> adjacentNode = adjacentDevice->GetNode();
    //                     //cout<< "Adj ID: "<<adjacentNode->GetId()<<" IP Address: "<<node->GetObject<Ipv4>()->GetAddress(j,0).GetLocal();
    //                 }
    //             }
    //             cout<<endl;
    //         }
    //     }

    // }
    
    Simulator::Run ();
    Simulator::Destroy ();
}