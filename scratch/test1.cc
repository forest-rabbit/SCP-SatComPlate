#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/timer.h"
#include "ns3/applications-module.h"
#include "ns3/cluster-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("LinkUtilizationMonitoring");

int count1 = 0;
// class LinkUtilizationMonitor:  public ns3::Object{
// public:
//     double max_utilization;
//     Ptr<Node> m_node; // 当前监控的节点

//     static ns3::TypeId GetTypeId() {
//         static ns3::TypeId tid = ns3::TypeId("LinkUtilizationMonitor")
//                                 .SetParent(ns3::Object::GetTypeId())
//                                 .SetGroupName("LinkUtilizationMonitoring");
//         return tid;
//     }

//     void SetLinkCapacity(uint32_t l)
//     {
//         m_linkCapacity = l;
//     }
//     void SetStopTime(double t)
//     {
//         time = t;
//     }
//     // 为每个节点启动监控
//     void StartMonitoring(Ptr<Node> node) {
//         m_node = node; // 存储当前节点
//         // 获取节点上的所有设备
//         for (uint32_t deviceId = 0; deviceId < m_node->GetNDevices(); ++deviceId) {
//             Ptr<NetDevice> device = node->GetDevice(deviceId);
//             Ptr<PointToPointNetDevice> devices = DynamicCast<PointToPointNetDevice>(device);
//             if(m_node->GetId() == 0 && deviceId == 3)
//             {
//                 mac_address = devices->GetAddress();
//             }
//             if (devices == nullptr) {
//             NS_LOG_ERROR("Device cast failed for device " << devices);
//             continue;
//            }
//             device->TraceConnectWithoutContext("DeviceTx", MakeCallback(&LinkUtilizationMonitor::TrackTxBytes, this));
//         }
//     }
//     // 使用设备索引和数据包指针 
//     void TrackTxBytes(Ptr<const Packet> p, Ptr<const PointToPointNetDevice> p2p) {
//         // 累加发送的字节数
//         if(p2p->GetAddress() == mac_address)
//         {
//             count1++;
//         }
//         //cout<<"Time: "<<Simulator::Now().GetSeconds()<<" Node: "<<m_node->GetId()<<" "<<"Device: "<<p2p->GetAddress()<<" DataSize: "<<p->GetSize()<<endl;
//         m_device[p2p->GetAddress()] += p->GetSize();
//     }

//     void CheckAndUpdateMaxUtilization() {
//         if (Simulator::Now() >= Seconds(time)) { // 假设仿真结束时间为20秒
//         return; // 终止递归调度
//         }
//         // cout<<m_node->GetId()<<endl;
//         max_utilization = 0.0;
//         // 遍历所有设备，更新每个节点的最大利用率
//         for (auto &entry : m_device) {
//             // Address deviceId = entry.first;
//             //cout<<"netdevice: "<<deviceId<<" packetNum: "<<entry.second<<endl;
//             double utilization = CalculateLinkUtilization(entry.second, m_linkCapacity);
//             if(max_utilization < utilization)
//             {
//                 max_utilization = utilization;
//             }
//             // std::cout <<"Time: "<<Simulator::Now().GetSeconds()<<" Node " << m_node->GetId() << ", Device " << deviceId
//             //                     <<"Send bytes "<<entry.second<< ", Max Link Utilization: " << utilization << std::endl;
//         }
//         // 打印最大利用率
//         //std::cout <<"Time: "<<Simulator::Now().GetSeconds()<<" Node " << m_node->GetId() << ", Max Link Utilization: " << max_utilization << std::endl;
//         // 重置字节计数器
//         m_device.clear();
//         // 重新调度下次检查调用
//         Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, this);
//     }

// private:
//     uint32_t m_linkCapacity = 0; // 链路容量
//     std::map<uint32_t, uint32_t> m_txBytes; // 每个设备的发送字节数
//     map<Address,uint32_t> m_device;
//     Address  mac_address;
//     double time;
//     double CalculateLinkUtilization(uint32_t bytes, uint32_t capacity) {
//         // 计算利用率
//         return (bytes * 8.0) / capacity; // 使用比特每秒
//     }
// };

void UseMaxUtilization(const vector<Ptr<LinkUtilizationMonitor>>& monitors) 
{
    if (Simulator::Now() >= Seconds(20)) 
    { // 假设仿真结束时间为20秒
        return; // 终止递归调度
    }
    // 处理 max_utilization
    for (auto monitor : monitors) 
    {
        double max_util = monitor->max_utilization;
        cout<<"Time: "<<Simulator::Now().GetSeconds()<<" Node: "<<monitor->m_node->GetId()<<" Link: "<<max_util<<endl;
        // 将 max_util 传递给其他函数进行处理
    }
    // 重新安排自己再次被调度
    
    Simulator::Schedule(Seconds(1), &UseMaxUtilization, monitors);
}

//dfs--input NodeContainer node changes the vector<int>cluster
void dfs(vector<uint32_t>& visit, NodeContainer& cluster, vector<vector<int>> adj, uint32_t v)//a node's adjList
{
    for(uint32_t i = 0; i < cluster.GetN(); ++i)
    {
        if( i != v)
        {
            for(uint32_t j : adj[v]) //change CH's ID
            {
                if( i == j && visit[i] == 0)
                {  
                    visit[i] = 1;
                    dfs(visit, cluster, adj, i);
                }
            }
        }
    }
}
//Full connectivity
bool 
IsFullConnectivity(NodeContainer& cluster, vector<vector<int>> adj)
{
    uint32_t n = cluster.GetN();
    vector<uint32_t> visit(n, 0);
    dfs(visit, cluster, adj, 0);
    for(uint32_t i = 0; i < n; ++i)
    {
        if(visit[i] == 0) return false;
    }
    return true;
}

int main(int argc, char *argv[]) 
{
    NodeContainer nodes;
    nodes.Create(4); // 创建4个节点

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    Ipv4InterfaceContainer interfaces;

    // 创建网状拓扑
    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper address;

    int subnet = 1;
    map<int,map<int,int>> ip_hash;//send--receive--ip index
    for (uint32_t i = 0; i < nodes.GetN(); ++i) 
    {
        for (uint32_t j = i + 1; j < nodes.GetN(); ++j) 
        {
            NetDeviceContainer linkDevices = p2p.Install(NodeContainer(nodes.Get(i), nodes.Get(j)));
            devices.Add(linkDevices);
            std::ostringstream subnetAddr;
            subnetAddr << "10.1." << subnet << ".0";
            address.SetBase(subnetAddr.str().c_str(), "255.255.255.0");
            interfaces.Add(address.Assign(linkDevices));
            ip_hash[i][j] = (2*subnet-1);
            subnet++;
        }
    }
// Ipv4GlobalRoutingHelper::SDSNRoutingTables();
// 预设端口号范围
uint16_t portStart = 9; // well-known echo port number
uint32_t numNodes = nodes.GetN();

// 为每个节点分配一个唯一的端口号
std::map<Ptr<Node>, uint16_t> nodePortMap;
for(uint32_t i = 0; i < numNodes; ++i) {
    uint16_t port = portStart + i; // 假设端口号从9开始，每个节点增加1
    nodePortMap[nodes.Get(i)] = port;
}
// 安装服务器应用程序到每个节点
for(uint32_t i = 0; i < numNodes; ++i) {
    UdpServerHelper server(nodePortMap[nodes.Get(i)]);
    ApplicationContainer apps = server.Install(nodes.Get(i));
    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(20.0));
}

// 为每个节点配置客户端应用程序，使其向其他节点发送数据包
for(uint32_t i = 0; i < numNodes; ++i) {
    Ptr<Node> clientNode = nodes.Get(i);
    for(uint32_t j = i+1; j < numNodes; ++j) {
        Ptr<Node> serverNode = nodes.Get(j);
        uint16_t port = nodePortMap[serverNode];
        UdpClientHelper client(interfaces.GetAddress(2*j-1), port);//
        if(i==0)
        {
            if(j==2)
            {
                client.SetAttribute("MaxPackets", UintegerValue(100));
                client.SetAttribute("Interval", TimeValue(Seconds(0.01)));
                client.SetAttribute("PacketSize", UintegerValue(1024));
            }
            else
            {
                client.SetAttribute("MaxPackets", UintegerValue(10));
                client.SetAttribute("Interval", TimeValue(Seconds(0.01)));
                client.SetAttribute("PacketSize", UintegerValue(1024));
            }
        }
        else if(i==1)
        {
            client.SetAttribute("MaxPackets", UintegerValue(200));
            client.SetAttribute("Interval", TimeValue(Seconds(0.03)));
            client.SetAttribute("PacketSize", UintegerValue(1024));
        }
        else
        {
            client.SetAttribute("MaxPackets", UintegerValue(300));
            client.SetAttribute("Interval", TimeValue(Seconds(0.02)));
            client.SetAttribute("PacketSize", UintegerValue(1024));
        }
        ApplicationContainer apps = client.Install(clientNode);
        apps.Start(Seconds(5.0));
        apps.Stop(Seconds(20.0));
    }
}

//*************** Test Link Fault *******************//
// SatCluster sat;
// vector<vector<int>>adj = sat.AdjacenyList(nodes); 
// if( IsFullConnectivity(nodes, adj) == true)
// {
//     cout<<" Full Connectivity "<<endl;
// }
// else
// {
//     cout<<" No Full Connectivity "<<endl;
// }
// for(uint32_t i = 0; i < adj.size(); i++)
// {
//     cout<<"node: "<<i<<" "<<"adj: ";
//     for(uint32_t j = 0; j < adj[i].size(); j++)
//     {
//         cout<<adj[i][j]<<" ";
//     }
//     cout<<endl;
// }

// SetLinkFault(nodes);
// JudgeFault(nodes);

// adj = sat.AdjacenyList(nodes); 
// if( IsFullConnectivity(nodes, adj) == true)
// {
//     cout<<" Full Connectivity "<<endl;
// }
// else
// {
//     cout<<" No Full Connectivity "<<endl;
// }
// for(uint32_t i = 0; i < adj.size(); i++)
// {
//     cout<<"node: "<<i<<" "<<"adj: ";
//     for(uint32_t j = 0; j < adj[i].size(); j++)
//     {
//         cout<<adj[i][j]<<" ";
//     }
//     cout<<endl;
// }

//*************** 创建链路利用率监控实例 ****************//
    std::vector<Ptr<LinkUtilizationMonitor>> monitors;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
        monitor->SetLinkCapacity(1000000);
        monitor->SetStopTime(20);
        monitor->StartMonitoring(nodes.Get(i));
        monitors.push_back(monitor);
        // 设置定时器，每秒钟检查一次最大利用率
        Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, monitor);
    }
    // 安排 UseMaxUtilization 函数第一次被调度
    Simulator::Schedule(Seconds(1), &UseMaxUtilization, monitors);
//****************************************************//

    // 运行仿真
    Simulator::Run();
    Simulator::Destroy();
    // for (Ptr<LinkUtilizationMonitor> monitor : monitors) 
    // {
    //     double max_util = monitor->max_utilization;
    //     std::cout << "Node " << monitor->m_node->GetId()
    //               << ", Max Link Utilization: " << max_util << "%" << std::endl;
    // }
    // cout<<"packet num: "<<count1<<endl;
    return 0;
}
