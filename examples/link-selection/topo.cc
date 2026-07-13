#include "topo.h"
#include "jsontopo/topo-link-state.h"
#include "jsontopo/topo-node-state.h"
#include "jsontopo/topo-runtime.h"
#include "jsontopo/topo-json.h"
#include "ns3/boolean.h"
#include "ns3/csma-net-device.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/integer.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "access.h"
#include "para.h"

// #include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <ns3/ipv4-address.h>
#include <ns3/node.h>
#include <ns3/object.h>
#include <ns3/ptr.h>
#include <set>
#include <sstream>
#include <sys/types.h>
#include <unordered_map>

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample-topo");

namespace ns3{
  NodeContainer sates;       // 所有卫星节点
  std::vector<NodeContainer> sateNodes;  // 所有卫星节点，按照轨道和轨道内的卫星排列
  std::vector<uint32_t>      NodesSlaveID;  // 保存所有卫星节点中子控制器ID，按照node.GetID()来索引元素
  std::vector<uint32_t>      changeClusterID;  // 记录所有卫星节点簇ID变化次数
  NetDeviceContainer p2pDevices; 
  NodeContainer Gnodes;      // 地面网络节点
  NodeContainer topoNodes;   // JSON拓扑中的所有节点，包含卫星和地面站
  std::vector<TopologyNodeInfo> topoNodeInfos;
  std::string nodesJsonFile;
  std::string topologyJsonFile;
  std::string timeSlicesJsonFile;
  static std::map<uint32_t, Ipv4Address> g_serviceAddressesByNodeId;

  // 激光链路分配文件的时间戳和链路配置映射
  typedef std::vector<Link> LinkSet; // 链路集合，存储链路的节点对
  std::map<int, LinkSet> timeLinksMap; // 时间戳到链路集合的映射

  // 跟踪每个节点已经使用的接口索引
  std::map<uint32_t, std::set<uint32_t>> nodeUsedIndices;

  static void
  AssignStableServiceAddresses()
  {
    struct ServiceNode
    {
      uint32_t external_id;
      Ptr<Node> node;
    };

    std::vector<ServiceNode> serviceNodes;
    serviceNodes.reserve(topoNodes.GetN());
    if (topoNodeInfos.size() == topoNodes.GetN())
    {
      for (const auto& info : topoNodeInfos)
      {
        serviceNodes.push_back({info.node_id, topoNodes.Get(info.node_index)});
      }
    }
    else
    {
      for (uint32_t i = 0; i < topoNodes.GetN(); ++i)
      {
        serviceNodes.push_back({i, topoNodes.Get(i)});
      }
    }

    std::sort(serviceNodes.begin(), serviceNodes.end(),
              [](const ServiceNode& lhs, const ServiceNode& rhs) {
                return lhs.external_id < rhs.external_id;
              });

    NS_ABORT_MSG_IF(serviceNodes.size() >= 0x000ffffe,
                    "稳定服务地址空间172.16.0.0/12不足");
    g_serviceAddressesByNodeId.clear();
    for (uint32_t i = 0; i < serviceNodes.size(); ++i)
    {
      Ptr<Node> node = serviceNodes[i].node;
      Ptr<CsmaNetDevice> device = CreateObject<CsmaNetDevice>();
      device->SetAddress(Mac48Address::Allocate());
      device->SetQueue(CreateObject<DropTailQueue<Packet>>());
      node->AddDevice(device);

      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
      int32_t interface = ipv4->AddInterface(device);
      Ipv4Address address(0xac100001u + i);
      ipv4->AddAddress(interface, Ipv4InterfaceAddress(address, Ipv4Mask("255.255.255.255")));
      ipv4->SetMetric(interface, 1);
      ipv4->SetUp(interface);
      g_serviceAddressesByNodeId[node->GetId()] = address;
    }

    std::cout << "[TOPO:Service] 稳定服务地址分配完成" << std::endl
              << "  count     : " << serviceNodes.size() << std::endl
              << "  range     : 172.16.0.1 - "
              << Ipv4Address(0xac100000u + serviceNodes.size()) << std::endl
              << std::endl;
  }

  Ipv4Address
  GetNodeServiceAddress(Ptr<Node> node)
  {
    auto service = g_serviceAddressesByNodeId.find(node->GetId());
    if (service != g_serviceAddressesByNodeId.end())
    {
      return service->second;
    }

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ABORT_MSG_IF(ipv4 == nullptr || ipv4->GetNInterfaces() <= 1,
                    "节点没有可用的IPv4业务地址: " << node->GetId());
    return ipv4->GetAddress(1, 0).GetLocal();
  }

  static void
  LogTopologyInitStart()
  {
    std::cout << "[TOPO:Init] 开始拓扑初始化" << std::endl;
    if (_useJsonTopo)
    {
      std::cout << "  source     : JsonTopo" << std::endl
                << "  nodes      : " << nodesJsonFile << std::endl
                << "  topology   : " << topologyJsonFile << std::endl;
    }
    else
    {
      std::cout << "  source     : Legacy" << std::endl
                << "  satellites : " << sates_num << std::endl
                << "  orbits     : " << orbit_num << std::endl
                << "  per orbit  : " << sate_num << std::endl;
    }
    std::cout << std::endl;
  }

  // 函数功能：输出卫星节点信息以及网卡信息
  void print_node_info(){
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("examples/link-selection/output/node-info-tables.txt");
    *stream->GetStream() <<  Simulator::Now().GetSeconds() << "s" << std::endl;
    for(uint32_t i=0; i<sates.GetN(); i++){
      uint32_t size = sates.Get(i)->GetNDevices();
      for(uint32_t j=0; j<size; j++){
        // 获取目的地址
        Ptr<Node> nodep =sates.Get(i);
        Ptr<NetDevice> dev = nodep->GetDevice(j);
        // 获取网卡的 IPv4 接口列表
        Ptr<Ipv4> ipv4 = nodep->GetObject<Ipv4>();
        uint32_t interfaceIndex = dev->GetIfIndex();
        // 获取 IPv4 地址
        Ipv4Address Address = ipv4->GetAddress(interfaceIndex, 0).GetLocal();
        Mac48Address mac = Mac48Address::ConvertFrom(dev->GetAddress());
        *stream->GetStream() <<"node："<< nodep->GetId() << " netdevice：" << j << " interface index：" << interfaceIndex 
                              << " IPv4地址：" << Address 
                              << " mac地址：" << mac
                              << std::endl;

        Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(dev);
        if(p2p_dev != nullptr && p2p_dev->IsLinkUp()){
          Ptr<Channel> channel = p2p_dev->GetChannel();
          for(uint32_t k = 0; k < channel->GetNDevices(); k++){
            Ptr<NetDevice> adj_device = channel->GetDevice(k);
            if( adj_device != dev)
            {
              Ptr<Node> adj_node = adj_device->GetNode();
              *stream->GetStream() <<  "adj_node:" << adj_node->GetId() ;
            }
          }
        }
        *stream->GetStream() <<  endl;
        // ipv4AddrMaps[Address] = nodep->GetId();
      }
    }
  }

  //获取邻接表
  void print_adjacency_list(NodeContainer allnodes){
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
}


  void initTopo(){
    // #ifdef NS3_OPENFLOW
    ConfigureDefaultJsonTopologyFiles();
    LogTopologyInitStart();

    if (!nodesJsonFile.empty())
    {
      CreateNodesFromJsonInfo(ReadTopologyNodesJsonFile(nodesJsonFile));
    }
    else
    {
      //创建卫星节点
      for(uint32_t i = 0; i < orbit_num; i++){
        NodeContainer nodes;
        for(uint32_t j=0; j<sate_num; j++){
          Ptr<Node> node = CreateObject<Node> ();
          // node->SetBeginId(5);
          nodes.Add(node);
        }
        sates.Add(nodes);
        topoNodes.Add(nodes);
        sateNodes.push_back(nodes);
      }
    }
    
    // 配置 PointToPoint 信道属性
    PointToPointHelper pointToPoint;
    if(linkBandwidth == 10000000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else pointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    pointToPoint.SetChannelAttribute ("Delay", StringValue("100ms"));  

    pointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("8000p"));//队列容量K=1000

    // 配置 同轨链路 PointToPoint 信道属性
    PointToPointHelper intraPlanePointToPoint;
    if(linkBandwidth == 10000000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Gbps"));
    else if(linkBandwidth == 100000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else intraPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    if(_isSate == 1)    intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("15.56ms"));        // Sat1 同轨链路时延
    else if(_isSate == 2)   intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("17.16ms"));    // Sat2 同轨链路时延
    else if(_isSate == 3)   intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("7.85ms"));     // Sat3 同轨链路时延
    else if(_isSate == 4)   intraPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("8.71ms"));     // Sat4 同轨链路时延

    intraPlanePointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("8000p"));                    // 队列容量K=1000

    // 配置 异轨链路 PointToPoint 信道属性
    PointToPointHelper interPlanePointToPoint;
    if(linkBandwidth == 10000000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Gbps"));
    else if(linkBandwidth == 100000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else interPlanePointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    if(_isSate == 1)    interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("8.93ms"));         // Sat1 异轨链路时延
    else if(_isSate == 2)   interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("9.18ms"));     // Sat2 异轨链路时延
    else if(_isSate == 3)   interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("5.42ms"));     // Sat3 异轨链路时延
    else if(_isSate == 4)   interPlanePointToPoint.SetChannelAttribute ("Delay", StringValue("5.61ms"));     // Sat4 异轨链路时延

    interPlanePointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("1000p"));//队列容量K=1000

    InternetStackHelper stack;
    stack.Install(topoNodes);
    if (_useJsonTopo && !_SDNRoute)
    {
      AssignStableServiceAddresses();
    }
    if (!_useJsonTopo)
    {
      cout<<sates.GetN()<<" 个卫星节点创建完成！"<<endl;
    }

    //搭建非mesh拓扑 - XW
    if(!_isMesh){
      if (!topologyJsonFile.empty())
      {
        std::vector<LinkInfo> links = ReadResolvedTopologyLinksJsonFile(topologyJsonFile);
        BuildNetworkTopology(topoNodes, links);
      }
      else
      {
        std::vector<LinkInfo> links =
          ReadTopologyFile("examples/link-selection/input/topology/csv/topo(324).csv");
        BuildNetworkTopology(sates, links);
      }
    }
    
    //搭建mesh拓扑 - 普通的
    else{

    NetDeviceContainer tmpDevices;

    // 搭建p2p拓扑
		// 轨道内链路连接
		for(uint32_t i=0; i<orbit_num; i++){
			for(uint32_t j=0; j<sate_num; j++){
				Ptr<Node> node = sateNodes[i].Get(j);
				Ptr<Node> next = sateNodes[i].Get((j+1)%sate_num);
        tmpDevices = intraPlanePointToPoint.Install(NodeContainer(node, next));
				p2pDevices.Add(tmpDevices);
			}
		}



    if(!_scenario) //正常场景
    {
      cout<<"搭建mesh拓扑-正常场景！"<<endl;
      // 轨道间链路连接
      for(uint32_t j=0; j<sate_num; j++){
        for(uint32_t i=0; i<orbit_num; i++){
          Ptr<Node> node = sateNodes[i].Get(j);
          Ptr<Node> next = sateNodes[(i+1)%orbit_num].Get(j);
          tmpDevices = interPlanePointToPoint.Install(NodeContainer(node, next));
          p2pDevices.Add(tmpDevices);
        }
      }

      // 普通卫星设置ip
      for(uint32_t i = 0; i < sateNodes.size(); ++i){
        Ipv4AddressHelper sipv4Helper;
        std::string str;
        str = "10." + std::to_string(i+1);
        for(uint32_t j = 0; j < sateNodes[i].GetN(); j++){
          std::string temp = str + "." + std::to_string(j+1) + ".0";
          Ipv4Address addr (temp.c_str ());
          sipv4Helper.SetBase(addr, "255.255.255.0");
          uint32_t size = sateNodes[i].Get(j)->GetNDevices();
          for(uint32_t k=0; k < size; k++){
            sipv4Helper.Assign(sateNodes[i].Get(j)->GetDevice(k));
          }
        }
      }
      //print_node_info();
    }
    else //激光链路分配场景
    {
      // 读取链路配置文件
    }
  } //mesh拓扑结束

    // 基于链路可用度的链路变化
    // if(!_sim) SetLinkAvaAvailability(sates);

    // 调用初始分簇算法
    // 启动动态分簇算法
    // ActiveCluster(sates, satClusterNodes);

    //打印邻接表
    //print_adjacency_list(sates);


    
    // 打印节点信息
    //print_node_info();
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    ScheduleTopologyTimeSlices();

    // 添加簇内簇间路由
    if(_SDNRoute){
      //Ipv4GlobalRoutingHelper::SDNRoutingTables(Gnodes, sates);   //OSPF最短路由
      //experiment.InitialSatRouter(Gnodes, sates, satClusterNodes, monitors, _consType, orbit_num, sate_num); // 初始化卫星路由策略
    }

    if (writeRoutingTables)
    {
      AsciiTraceHelper ascii;
      Ptr<OutputStreamWrapper> stream =
        ascii.CreateFileStream ("examples/link-selection/output/routing-tables-6s.txt");
      Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (6), stream, Time::S);
      Ptr<OutputStreamWrapper> stream2 =
        ascii.CreateFileStream ("examples/link-selection/output/routing-tables-11s.txt");
      Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (11), stream2, Time::S);
      Ptr<OutputStreamWrapper> stream3 =
        ascii.CreateFileStream ("examples/link-selection/output/routing-tables-16s.txt");
      Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (16), stream3, Time::S);
    }

    if (!_useJsonTopo)
    {
      Simulator::Schedule(Seconds(1.0), &updateTopo);
    }



    NS_LOG_INFO ("Configure Tracing.");

  }
  // 更新拓扑
  void updateTopo(){
    if (_useJsonTopo)
    {
      return;
    }
    // 获取当前模拟时间（取整到秒）
    int currentTime = Simulator::Now().GetSeconds();
    std::cout << "当前时间: " << currentTime << "s" << std::endl;
   
    if(!_isMesh) {
      Simulator::Schedule(Seconds(1.0), &updateTopo);
      return;
    } // 非mesh拓扑不更新链路

    if(_isMesh && timeLinksMap.empty()) {
      Simulator::Schedule(Seconds(1.0), &updateTopo);
      return; 
    }// mesh拓扑但无链路变化配置不更新链路
    
    // 调度下一次更新
    Simulator::Schedule(Seconds(1.0), &updateTopo);
  } 

// 读取CSV文件并解析链路信息
std::vector<LinkInfo> ReadTopologyFile(const std::string& filename) {
    std::vector<LinkInfo> links;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        NS_FATAL_ERROR("无法打开文件: " << filename);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        LinkInfo link;
        
        // 解析CSV格式: 源节点,目的节点,时延(ms),带宽(Gbps)
        std::getline(ss, token, ',');
        link.source = std::stoi(token) - 1; // 转换为0索引
        
        std::getline(ss, token, ',');
        link.destination = std::stoi(token) - 1;
        
        std::getline(ss, token, ',');
        link.delay_ms = std::stoi(token);
        
        std::getline(ss, token, ',');
        link.bandwidth_gbps = std::stoi(token);
        
        links.push_back(link);
    }
    
    file.close();
    return links;
}

// 搭建非mesh网络
void BuildNetworkTopology(NodeContainer& satellites,  const std::vector<LinkInfo>& links){
    TopologyLinkUpdateSummary summary = ApplyFullTopologyLinks(satellites, links, false);
    std::cout << "[TOPO:Links] 初始链路安装完成" << std::endl
              << "  total     : " << links.size() << std::endl
              << "  by type   :" << FormatInitialTopologyLinkTypes(links) << std::endl
              << "  installed : " << summary.added_links << std::endl
              << "  reused    : " << summary.unchanged_links + summary.reenabled_links << std::endl
              << std::endl;
}



}
