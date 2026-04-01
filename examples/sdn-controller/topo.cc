#include "topo.h"
#include "cluster.h"
#include "ns3/boolean.h"
#include "ns3/integer.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/openflow-sdn-controller.h"
#include "ns3/openflow-switch-net-device.h"
#include "ns3/point-to-point-module.h"
#include "all-node.h"
#include "access.h"
#include "para.h"
#include "json.hpp"

// #include "ns3/core-module.h"
#include <cstdint>
#include <mutex>
#include <ns3/ipv4-address.h>
#include <ns3/node.h>
#include <ns3/object.h>
#include <ns3/ptr.h>
#include <sys/types.h>
#include <unordered_map>

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample-topo");

using json = nlohmann::json;

namespace ns3{
  NodeContainer mcs;         // 主控制器节点0， 1
  NodeContainer scs;         // 地面从控制器
  NodeContainer gwss;        // 地面站Gateway Station 5个
  NodeContainer Gnodes;      // 地面网络节点
  NodeContainer slavescs;    // 从控制器节点
  NodeContainer sates;       // 所有卫星节点
  std::vector<NodeContainer> sateNodes;  // 所有卫星节点，按照轨道和轨道内的卫星排列
  NodeContainer allnode;
//   std::vector<NodeContainer> satClusterNodes;    // 所有卫星节点，数组中的一行表示一簇，nodeContainer中的第一个表示簇首 
  std::vector<uint32_t>      NodesSlaveID;  // 保存所有卫星节点中子控制器ID，按照node.GetID()来索引元素
  std::vector<uint32_t>      changeClusterID;  // 记录所有卫星节点簇ID变化次数

  SatRouting experiment;

  // uint32_t sates_num = 66;   // N: LEO卫星总数
  // uint32_t orbit_num = 6;   // No: 轨道数
  // uint32_t sate_num = sates_num / orbit_num;  // Ns:每个轨道上卫星数量

  // Ptr<ns3::ofi::MasterController> MasterController;  // 主控制器节点
  // Ptr<ns3::ofi::MasterController> BackupMasterController;  // 备份主控制器节点
  // std::vector<Ptr<ns3::ofi::SlaveController>> SlavesControllers;  // 从控制器节点 

  int interval = 3;    // 卫星检测周期
  int masterID = 0;    // 主控制器ID
  // ofi::node_info master_node_info;
  // ofi::node_info backup_master_node_info;
  // std::vector<ofi::node_info> gws_node_info;
  // std::vector<ofi::node_info> slaves_node_info;

  // 本标志用于不同子控制器间同步主控制器接管信息
  bool toMasterflag = false;

  // std::unordered_map<Mac48Address, ofi::Ipv4Inter, ofi::Mac48AddressHash, ofi::Mac48AddressEqual> MacMaps;
  // std::unordered_map<Ipv4Address, uint32_t, Ipv4AddressHash, Ipv4AddressEqual> ipv4AddrMaps;

  NetDeviceContainer SwitchDevices;      // openflow交换机
  // NetDeviceContainer allswitchDevices;   // csma交换机
  // NetDeviceContainer sateSwitchDevices;      // 卫星节点openflow交换机

  // 局部变量
  Ptr<ns3::ofi::MasterController> Mcontroller;
  Ptr<ns3::ofi::MasterController> BackupMcontroller;
  Ptr<ArpCache> GlobelArpCache;

  // 函数功能：输出卫星/地面节点信息以及网卡信息
  void print_node_info(){
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("examples/sdn-controller/output/node-info-tables.txt");

    for(uint32_t i=0; i<allnode.GetN(); i++){
      uint32_t size = allnode.Get(i)->GetNDevices();
      for(uint32_t j=0; j<size; j++){
        // 获取目的地址
        Ptr<Node> nodep =allnode.Get(i);
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
  
  // for(uint32_t i = 0; i < node.GetN(); i++)
  // {
  //   Ptr<Node> m_node = node.Get(i);
  //   if(i == 0) index = node.Get(i)->GetId();
  //   cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << "\tdeviceSize:" << m_node->GetNDevices() << endl;
  //   for(uint32_t j = 1; j < m_node->GetNDevices(); j++)
  //   {
  //     // if(j>=5) break;
  //     // cout << "node:" << i << "\tdevice:" << j << endl;
  //     Ptr<NetDevice> dev = m_node->GetDevice(j);
  //     Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(dev);
  //     if(p2p_dev == nullptr) cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << " nullptr" << endl;
  //     else if(!p2p_dev->IsLinkUp()) cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << " link down" << endl;
  //     if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//add connect flag
  //     {
  //       Ptr<Channel> channel = p2p_dev->GetChannel();
  //       cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << "\tchannelsize:" << channel->GetNDevices() << endl;
  //       for(uint32_t k = 0; k < channel->GetNDevices(); k++)
  //       {
  //         Ptr<NetDevice> adj_device = channel->GetDevice(k);
  //         if( adj_device != dev)
  //         {
  //           Ptr<Node> adj_node = adj_device->GetNode();
  //           adj_list[node.Get(i)->GetId() - index].push_back(adj_node->GetId() - index);// 
  //           cout << "cluster             adj_list[" << node.Get(i)->GetId() - index << "]=" << adj_node->GetId() - index << endl;
  //         }
  //       }
  //     }
  //   }
  // }

  }

  void testBreakDetect(){
    // std::vector<NodeContainer> satClusterNodes;
    // 每个分簇选择距离从控制器最远的节点，设置为故障
    for(uint32_t i=0; i<satClusterNodes.size(); i++){
      Ptr<satelliteNode> node = DynamicCast<satelliteNode>(satClusterNodes[i].Get(satClusterNodes[i].GetN()-1));
      cout << "簇" << i << "\t节点" << node->GetId() << "设置为故障节点！" << endl;
      node->m_state = breakNode;
    }
  }

  void setGroundSateLink(PointToPointHelper pointToPoint){
    //本函数选择部分卫星节点分别与地面站相连
    uint32_t gwsIndex = 0;

    pointToPoint.Install(sates.Get(41), gwss.Get(gwsIndex));
    pointToPoint.Install(sates.Get(50), gwss.Get(gwsIndex));

    gwsIndex ++;
    pointToPoint.Install(sates.Get(40), gwss.Get(gwsIndex));

    gwsIndex ++;
    pointToPoint.Install(sates.Get(40), gwss.Get(gwsIndex));

    gwsIndex ++;
    pointToPoint.Install(sates.Get(40), gwss.Get(gwsIndex));

    gwsIndex ++;
    pointToPoint.Install(sates.Get(30), gwss.Get(gwsIndex));
    pointToPoint.Install(sates.Get(31), gwss.Get(gwsIndex));

    // pointToPoint.Install(sates.Get(41), gwss.Get(gwsIndex));
    // pointToPoint.Install(sates.Get(50), gwss.Get(gwsIndex));

    // gwsIndex ++;
    // pointToPoint.Install(sates.Get(8), gwss.Get(gwsIndex));

    // gwsIndex ++;
    // pointToPoint.Install(sates.Get(45), gwss.Get(gwsIndex));

    // gwsIndex ++;
    // pointToPoint.Install(sates.Get(4), gwss.Get(gwsIndex));

    // gwsIndex ++;
    // pointToPoint.Install(sates.Get(0), gwss.Get(gwsIndex));
    // pointToPoint.Install(sates.Get(31), gwss.Get(gwsIndex));
  }

  void initTopo(){
    // #ifdef NS3_OPENFLOW

    for(int i=0; i<mcsnum; i++){
      Ptr<satelliteNode> node = CreateObject<satelliteNode> ();
      mcs.Add(node);
    } 
    //从控制器数量修改
    if(_slaveMode){
      for(int i = 0; i<scsnum; i++){
        Ptr<satelliteNode> node = CreateObject<satelliteNode> ();
        scs.Add(node);
      }
    }
    // mcs.Create (mcsnum);
    gwss.Create (gwsnum);
    Gnodes.Add(mcs);
    Gnodes.Add(gwss);
    if(_slaveMode) Gnodes.Add(scs); 

		for(uint32_t i=0; i<orbit_num; i++){
			NodeContainer nodes;
			for(uint32_t j=0; j<sate_num; j++){
				Ptr<satelliteNode> node = CreateObject<satelliteNode> ();
        // node->SetBeginId(5);
				nodes.Add(node);
			}
      sates.Add(nodes);
			sateNodes.push_back(nodes);
		}

    GlobelArpCache = CreateObject<ArpCache>();
    
    // NodeContainer
    if(_slaveMode) allnode = NodeContainer(mcs, scs, gwss, sates);
    else allnode = NodeContainer(mcs, gwss, sates);


    // 配置 PointToPoint 信道属性
    PointToPointHelper pointToPoint;
    if(linkBandwidth == 10000000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("10Gbps"));
    else if(linkBandwidth == 100000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));
    else if(linkBandwidth == 500000000) pointToPoint.SetDeviceAttribute ("DataRate", StringValue("500Mbps"));
    else pointToPoint.SetDeviceAttribute ("DataRate", StringValue("100Mbps"));

    // if(_isSate == 1)    pointToPoint.SetChannelAttribute ("Delay", StringValue("16ms"));        
    // else if(_isSate == 2)   pointToPoint.SetChannelAttribute ("Delay", StringValue("22ms"));    
    // else if(_isSate == 3)   pointToPoint.SetChannelAttribute ("Delay", StringValue("7ms"));
    // else if(_isSate == 4)   pointToPoint.SetChannelAttribute ("Delay", StringValue("7ms"));    
    pointToPoint.SetChannelAttribute ("Delay", StringValue("100ms"));  

    pointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("1000p"));//队列容量K=1000

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

    intraPlanePointToPoint.SetQueue("ns3::DropTailQueue","MaxSize",StringValue("1000p"));                    // 队列容量K=1000

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
    if(_slaveMode){ 
      // 从控制器位于地面中心
      stack.Install(NodeContainer(mcs, scs, gwss, sates)); 
    }
    else{
      // 从控制器位于簇首
      stack.Install(NodeContainer(mcs, gwss, sates));
    }

    // 为主控制器和地面站安装天线模型
    for (uint32_t i = 0; i < mcs.GetN(); ++i) {
      Ptr<Node> node = mcs.Get(i);
      // node->SetBeginId(gwsnum + 1);
      // 创建一个 CosineAntennaModel 对象
      Ptr<ParabolicAntennaModel> antenna = CreateObject<ParabolicAntennaModel>();
      antenna->SetAttribute("Orientation", DoubleValue(120.0)); // 设置天线倾角为120度
      node->AggregateObject(antenna); // 安装相同的天线模型到每个节点
    }
    for (uint32_t i = 0; i < gwss.GetN(); ++i) {
      Ptr<Node> node = gwss.Get(i);
      // 创建一个 CosineAntennaModel 对象
      Ptr<ParabolicAntennaModel> antenna = CreateObject<ParabolicAntennaModel>();
      antenna->SetAttribute("Orientation", DoubleValue(120.0)); // 设置天线倾角为120度
      node->AggregateObject(antenna); // 安装相同的天线模型到每个节点
    }
    // 从控制器安装天线模型
    if(_slaveMode) {
      for (uint32_t i = 0; i < scs.GetN(); ++i) {
      Ptr<Node> node = scs.Get(i);
      Ptr<ParabolicAntennaModel> antenna = CreateObject<ParabolicAntennaModel>();
      antenna->SetAttribute("Orientation", DoubleValue(120.0)); // 设置天线倾角为120度
      node->AggregateObject(antenna); // 安装相同的天线模型到每个节点
      }
    }

    // 创建移动模型（此处使用ConstantPositionMobilityModel） 特点：简单的移动模型，节点保持固定的位置不动
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allnode);

    // // 创建交换机设备
    // CsmaHelper csma;
    // csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Gbps")));
    // csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(14*1000000)));
    
    // allswitchDevices = csma.Install(NodeContainer(mcs, gwss, sates));    // all

    #ifdef _linkErrorModel
    // 通过全局命名空间/方法,设置错误模型的错误率
    Config::SetDefault ("ns3::RateErrorModel::ErrorRate", DoubleValue (0.0001));
    Config::SetDefault ("ns3::RateErrorModel::ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
    // 设置错误模型的错误方法为数据包模式
    // Config::SetDefault ("ns3::BurstErrorModel::ErrorRate", DoubleValue (0.01));
    // Config::SetDefault ("ns3::BurstErrorModel::BurstSize", StringValue ("ns3::UniformRandomVariable[Min=1|Max=3]"));

    std::string errorModelType = "ns3::RateErrorModel";     // 字符串赋值为错误模型id
    ObjectFactory factory;                                  // 创建对象工厂
    factory.SetTypeId (errorModelType);                     // 设置错误模型id
    Ptr<ErrorModel> em = factory.Create<ErrorModel> ();     // 根据使用说明使用工厂对象创建对象em
    #endif

    NetDeviceContainer p2pDevices;
    NetDeviceContainer tmpDevices;

    // 搭建p2p拓扑
    // 主控制器与地面站相连
    for(uint32_t i = 0; i < mcs.GetN(); ++i){
      for(uint32_t j=0; j < gwss.GetN(); ++j){
        p2pDevices.Add(pointToPoint.Install(NodeContainer(mcs.Get(i), gwss.Get(j))));
      }
    }

    if(_slaveMode) {
      // 主控制器与从控制器相连
      for(uint32_t i = 0; i < mcs.GetN(); ++i){
        for(uint32_t j=0; j < scs.GetN(); ++j){
          p2pDevices.Add(pointToPoint.Install(NodeContainer(mcs.Get(i), scs.Get(j))));
        }
      }
      // 从控制器与地面站相连
      for(uint32_t i = 0; i < scs.GetN(); ++i){
        for(uint32_t j=0; j < gwss.GetN(); ++j){
          p2pDevices.Add(pointToPoint.Install(NodeContainer(scs.Get(i), gwss.Get(j))));
        }
      }
    }


		// 轨道内链路连接
		for(uint32_t i=0; i<orbit_num; i++){
			for(uint32_t j=0; j<sate_num; j++){
				Ptr<Node> node = sateNodes[i].Get(j);
				Ptr<Node> next = sateNodes[i].Get((j+1)%sate_num);
                tmpDevices = intraPlanePointToPoint.Install(NodeContainer(node, next));
				p2pDevices.Add(tmpDevices);
                #ifdef _linkErrorModel
                tmpDevices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue (em));
                tmpDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue (em));
                #endif
			}
		}

    if(!_scenario) //正常场景
    {
      // 轨道间链路连接
      for(uint32_t j=0; j<sate_num; j++){
        for(uint32_t i=0; i<orbit_num; i++){
          Ptr<Node> node = sateNodes[i].Get(j);
          Ptr<Node> next = sateNodes[(i+1)%orbit_num].Get(j);
          tmpDevices = interPlanePointToPoint.Install(NodeContainer(node, next));
          p2pDevices.Add(tmpDevices);
          #ifdef _linkErrorModel
          tmpDevices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue (em));
          tmpDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue (em));
          #endif
        }
      }
    }
    else //最少异轨链路场景
    {
      std::vector<std::pair<int,int>> links;//节点间连接关系
      std::string name;

      if(_isSate == 1) //SatI
      {
        name = "examples/sdn-controller/SatI.txt";
      }
      else if(_isSate == 2) //SatII
      {
        name = "examples/sdn-controller/SatII.txt";
      }
      else if(_isSate == 4) //SatIV
      {
        name = "examples/sdn-controller/SatII.txt";     // TODO: 待添加异轨链路分配
      }

      std::ifstream file(name);
      if(!file.is_open())
      {
        std::cout<<"无法打开最少异轨链路文件： "<< name <<std::endl;
        return;
      }
      std::string line;
      while(std::getline(file, line))
      {
        std::stringstream ss(line);
        char ignore;//跳过逗号和括号
        int node1, node2;
        while(ss>>ignore>>node1>>ignore>>node2>>ignore) 
        {
          links.emplace_back(node1,node2);
          //跳过逗号
          ss>>ignore;
        }
      }
      file.close();
      //轨间连接
      // std::cout << "链路信息:" << std::endl;
      // for (const auto &link : links) {
      //     std::cout << "(" << link.first << ", " << link.second << ")" << std::endl;
      // }

      for(const auto& link : links)
      {
        Ptr<Node> node = sates.Get(link.first);
        Ptr<Node> next = sates.Get(link.second);
        tmpDevices = interPlanePointToPoint.Install(NodeContainer(node, next));
        p2pDevices.Add(tmpDevices);
        #ifdef _linkErrorModel
        tmpDevices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue (em));
        tmpDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue (em));
        #endif
      }
    }


    // 创建TrafficControlHelper帮助器来配置流量控制
    TrafficControlHelper tch;
    // 配置RED队列规则
    tch.SetRootQueueDisc("ns3::RedQueueDisc", "MaxSize", StringValue("1000p"), "MaxTh", DoubleValue(998),       // 设置最大阈值
                         "MinTh", DoubleValue(990),      // 设置最小阈值
                         "MeanPktSize", UintegerValue(1024));
    tch.Install(p2pDevices);

    // if(!_BreakDetect && !_sim){
    //   // 反向缝导致的链路变化
    //   if(_isSate == 1) LinkChange(sates,"examples/sdn-controller/Sat1(100s)_linkchange1.csv");
    //   else if(_isSate == 3) LinkChange(sates,"examples/sdn-controller/Sat3(100s)_linkchange.csv");
    //   else LinkChange(sates,"examples/sdn-controller/Sat2(100s)_linkchange.csv");
    // }

    // 基于链路可用度的链路变化
    if(!_sim) SetLinkAvaAvailability(sates);

    changeClusterID.resize(sateBegID+sates_num, 0);
    setGroundSateLink(pointToPoint);

   // 主控制器设置ip
    uint32_t size = mcs.Get(0)->GetNDevices();
    Ipv4AddressHelper mipv4Helper;
    mipv4Helper.SetBase("10.1.0.0", "255.255.0.0");
    for(uint32_t i=0; i<size; i++){
      mipv4Helper.Assign(mcs.Get(0)->GetDevice(i));
    }

    // 备份主控制器设置ip
    size = mcs.Get(1)->GetNDevices();
    Ipv4AddressHelper bipv4Helper;
    bipv4Helper.SetBase("10.2.0.0", "255.255.0.0");
    for(uint32_t i=0; i<size; i++){
      mipv4Helper.Assign(mcs.Get(1)->GetDevice(i));
    } 
    // 地面从控制器设置ip - 10.3.1.0
    if(_slaveMode){
      for(uint32_t i=0; i<scs.GetN(); i++){
        Ipv4AddressHelper gipv4Helper;
        std::string str = "10.3." + std::to_string(i+1) + ".0";
        Ipv4Address addr (str.c_str ());
        gipv4Helper.SetBase(addr, "255.255.255.0");
        uint32_t size = scs.Get(i)->GetNDevices();
        for(uint32_t j=0; j<size; j++){
          gipv4Helper.Assign(scs.Get(i)->GetDevice(j));
        }
      }
    }

    // 地面站设置ip
    for(uint32_t i=0; i<gwss.GetN(); i++){
      Ipv4AddressHelper gipv4Helper;
      std::string str;
      if(_slaveMode) str = "10.4." + std::to_string(i+1) + ".0";
      else str = "10.3." + std::to_string(i+1) + ".0";
      Ipv4Address addr (str.c_str ());
      gipv4Helper.SetBase(addr, "255.255.255.0");
      uint32_t size = gwss.Get(i)->GetNDevices();
      for(uint32_t j=0; j<size; j++){
        gipv4Helper.Assign(gwss.Get(i)->GetDevice(j));
      }
    }

    // 普通卫星设置ip
    for(uint32_t i = 0; i < sateNodes.size(); ++i){
      Ipv4AddressHelper sipv4Helper;
      std::string str;
      if(_slaveMode) str = "10." + std::to_string(i+5);
      else str = "10." + std::to_string(i+4);
      for(uint32_t j=0; j < sateNodes[i].GetN(); j++){
        std::string temp = str + "." + std::to_string(j+1) + ".0";
        Ipv4Address addr (temp.c_str ());
        sipv4Helper.SetBase(addr, "255.255.255.0");
        uint32_t size = sateNodes[i].Get(j)->GetNDevices();
        for(uint32_t k=0; k < size; k++){
          sipv4Helper.Assign(sateNodes[i].Get(j)->GetDevice(k));
        }
      }
    }
    

    // 断开反向缝链路连接
    if((_isSate == 1) && (!_scenario))
    {
      for(uint32_t j=0; j<sate_num; j++){
          Ptr<Node> node = sateNodes[0].Get(j);
          Ptr<Node> next = sateNodes[orbit_num-1].Get(j);         
          {
            // 初始时刻断开SatI反向缝
            LinkDown(node, next);

          }
      }
    }
    
    print_node_info();

    // 调用初始分簇算法
    // 启动动态分簇算法
    ActiveCluster(sates, satClusterNodes);

    //**全网分一个簇**//
    // NodeContainer re_sates;
    // re_sates.Add(sates.Get(0));
    // for(uint32_t i = 0; i < sates.GetN(); i++)
    // {
    //   if(sates.Get(i) != re_sates.Get(0))
    //   {
    //     re_sates.Add(sates.Get(i));
    //   }
    // }
    // satClusterNodes.push_back(re_sates);//选择中心节点
    // for(auto&t:satClusterNodes)
    // {
    //   for(uint32_t i = 0; i <t.GetN(); i++)
    //   {
    //     cout<<t.Get(i)->GetId()<<" ";
    //   }
    //   cout<<endl;
    // }
    // for (uint32_t i = 0; i < sates.GetN(); ++i) 
    // {
    //     Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
    //     monitor->SetLinkCapacity(linkBandwidth);
    //     monitor->SetStopTime(totalTimeStep);
    //     monitor->StartMonitoring(sates.Get(i));
    //     // std::cout << "nodeID:" << i << "\t最大链路利用率：" << monitor->max_utilization << std::endl;
    //     monitors.push_back(monitor);
    //     // 设置定时器，每秒钟检查一次最大利用率
    //     Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, monitor);
    // }
    
    //从控制器--星上簇首为从控制器、地面站为从控制器
    NodesSlaveID.resize(sateBegID+sates_num);
    if (_slaveMode){
      // 模式1: 地面站作为从控制器
      for(uint32_t i = 0; i < scs.GetN(); i++)
        slavescs.Add(scs.Get(i));
    }
    else{
      // 模式0: 簇首作为从控制器
      for(uint32_t i=0; i < satClusterNodes.size(); ++i){
        slavescs.Add(satClusterNodes[i].Get(0));
        for(uint32_t j=0; j<satClusterNodes[i].GetN(); j++){
        // 记录每一个节点对应的子控制器ID
          NodesSlaveID[satClusterNodes[i].Get(j)->GetId()] = satClusterNodes[i].Get(0)->GetId();
        }
      }
    }

    // 设置节点类型
    // 设置主控制器类型
    Ptr<satelliteNode> node = DynamicCast<satelliteNode>(mcs.Get(0));
    node->SetToMasterController(interval, 0, mcs, slavescs);
    node = DynamicCast<satelliteNode>(mcs.Get(1));
    node->SetToMasterController(interval, 0, mcs, slavescs);
    if(_slaveMode)
    {
      // _slaveMode=1 : 地面从控制器
      NodeContainer satClusterHead; //簇头节点集合
      for(uint32_t  i=0; i<satClusterNodes.size(); i++){
        satClusterHead.Add(satClusterNodes[i].Get(0));
        for(uint32_t  j=0; j<satClusterNodes[i].GetN(); j++){
          Ptr<satelliteNode> temp = DynamicCast<satelliteNode>(satClusterNodes[i].Get(j));
          temp->SetToNormal(interval, mcs, slavescs, satClusterNodes[i]); // 普通节点为簇首和簇成员节点
        }
      }
      for(uint32_t i = 0; i < scs.GetN(); i++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(scs.Get(i));
        node->SetToSlaveController(interval, mcs, slavescs, satClusterHead); // 从控制器为地面站，簇内节点集合为各簇首--satClusterHead改为一个对应的簇首节点，即一个从控对应一个簇首对应615代码
      }
    }
    else
    {
      //_slaveMode=0 : 簇首从控制器
      for(uint32_t  i=0; i<satClusterNodes.size(); i++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(satClusterNodes[i].Get(0));
        node->SetToSlaveController(interval, mcs, slavescs, satClusterNodes[i]);
        for(uint32_t  j=1; j<satClusterNodes[i].GetN(); j++){
          Ptr<satelliteNode> temp = DynamicCast<satelliteNode>(satClusterNodes[i].Get(j));
          temp->SetToNormal(interval, mcs,  slavescs, satClusterNodes[i]);
        }
      }
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    if(_SDNRoute){
      Ipv4GlobalRoutingHelper::SDNRoutingTables(Gnodes, sates);   //OSPF最短路由
      experiment.InitialSatRouter(Gnodes, sates, satClusterNodes, monitors, _consType, orbit_num, sate_num); // 初始化卫星路由策略
    }

    if(_BreakDetect) testBreakDetect();

    // 打印所有节点的路由表
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("examples/sdn-controller/output/routing-tables-6s.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (6), stream, Time::S);
    Ptr<OutputStreamWrapper> stream2 = ascii.CreateFileStream ("examples/sdn-controller/output/routing-tables-11s.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (11), stream2, Time::S);
    Ptr<OutputStreamWrapper> stream3 = ascii.CreateFileStream ("examples/sdn-controller/output/routing-tables-16s.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (16), stream3, Time::S);

    if(_DynamicCluster && !_sim){
      // 周期性调度，根据动态分簇结果更新管控架构
      Simulator::Schedule(Seconds(clusterUpdateStep), &updateTopo);

      // 周期性调度，分离分簇和路由
      Simulator::Schedule(Seconds(routeUpdateStep), &updateRou);
    }

    NS_LOG_INFO ("Configure Tracing.");

    // #else
    // NS_LOG_INFO ("NS-3 OpenFlow is not enabled. Cannot run simulation.");
    // #endif // NS3_OPENFLOW
  }

  //基于post和link
  void initTopoSim(string dataArray){
    mcsnum = 0;
    //起始节点为地面站节点
    gwss.Create (gwsnum);
    sates.Create(sates_num);
    allnode = NodeContainer(gwss, sates);

    // for(uint32_t i=0; i<orbit_num; i++){
		// 	NodeContainer nodes;
		// 	for(uint32_t j=0; j<sate_num; j++){
		// 		Ptr<Node> node = CreateObject<Node> ();
		// 		nodes.Add(node);
		// 	}
		// 	sateNodes.push_back(nodes);
		// }

    // 配置 PointToPoint 信道属性--带宽和传播时延
    std::unordered_map<int,vector<int>> connect_node; 
    PointToPointHelper pointToPoint;
    //解析Json数据
    json jsonData = json::parse(dataArray);
    uint32_t id1, id2;
    Ptr<Node> node1, node2;
    for(const auto& item : jsonData["data"]){
        id1 = item["node1_id"];
        id2 = item["node2_id"];
        if(id_node[id1] < sateBegID || id_node[id2] < sateBegID) continue;
        //根据id1和id2得到对应ns3容器中的节点--双向映射
        node1 = allnode.Get(id_node[id1]); 
        node2 = allnode.Get(id_node[id2]);
        
        //bug：双向连接
        bool is_connect = false;
        for (uint32_t j = 0; j < node1->GetNDevices(); ++j)
        {
          Ptr<NetDevice> device = node1->GetDevice(j);
          Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(device);
          if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//判断链路是否存在
          {
            Ptr<Channel> channel = p2p_dev->GetChannel();
            for(uint32_t k = 0; k < channel->GetNDevices(); k++)
            {
              Ptr<NetDevice> adj_device = channel->GetDevice(k);
              if( adj_device != device)
              {
                Ptr<Node> adj_node = adj_device->GetNode();
                if(adj_node->GetId() == node2->GetId()) {
                  is_connect = true;
                  break;
                } 
              }
            }
          }
          if(is_connect) break;
        }
        if(is_connect) continue;


        // 链路带宽
        string dr = to_string(item["link_bandwidth"]);
        dr+="kbps";
        pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue(DataRate(dr)));
        // 传播时延
        double seconds = item["delay"];
        pointToPoint.SetChannelAttribute ("Delay", TimeValue(MicroSeconds(seconds)));
        pointToPoint.Install(NodeContainer(node1, node2));
    }

    // 安装协议栈
    InternetStackHelper stack;
    stack.Install(NodeContainer(gwss, sates));

    // 地面站设置ip
    for(uint32_t i=0; i<gwss.GetN(); i++){
      Ipv4AddressHelper gipv4Helper;
      std::string str;
      str = "10.1." + std::to_string(i+1) + ".0";
      Ipv4Address addr (str.c_str ());
      gipv4Helper.SetBase(addr, "255.255.255.0");
      uint32_t size = gwss.Get(i)->GetNDevices();
      for(uint32_t j=0; j<size; j++){
        gipv4Helper.Assign(gwss.Get(i)->GetDevice(j));
      }
    }


    // // 普通卫星设置ip
    // for(uint32_t i = 0; i < sateNodes.size(); ++i){
    //   Ipv4AddressHelper sipv4Helper;
    //   std::string str;
    //   str = "10." + std::to_string(i+4);
    //   for(uint32_t j=0; j < sateNodes[i].GetN(); j++){
    //     std::string temp = str + "." + std::to_string(j+1) + ".0";
    //     Ipv4Address addr (temp.c_str ());
    //     sipv4Helper.SetBase(addr, "255.255.255.0");
    //     uint32_t size = sateNodes[i].Get(j)->GetNDevices();
    //     for(uint32_t k=0; k < size; k++){
    //       sipv4Helper.Assign(sateNodes[i].Get(j)->GetDevice(k));
    //     }
    //   }
    // }

    // 普通卫星设置ip
    for(uint32_t i = 0; i < sates.GetN(); ++i){
      Ipv4AddressHelper sipv4Helper;
      // 动态计算地址段，防止超过255
      uint32_t baseSegment = 2 + i / 255;  // 第二段从2开始，每255个节点递增
      uint32_t thirdSegment = (i % 255) + 1; // 第三段循环1-255
      std::string baseAddress = "10." + std::to_string(baseSegment) 
                           + "." + std::to_string(thirdSegment) + ".0";

      Ipv4Address addr (baseAddress.c_str ());
      sipv4Helper.SetBase(addr, "255.255.255.0");
      uint32_t size = sates.Get(i)->GetNDevices();
      for(uint32_t k=0; k < size; k++){
        sipv4Helper.Assign(sates.Get(i)->GetDevice(k));
        }
      }
    
    //print_node_info();

    // 调用初始分簇算法
    // 启动动态分簇算法
    ActiveCluster(sates, satClusterNodes);

    //Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    if(_SDNRoute){
      //Ipv4GlobalRoutingHelper::SDNRoutingTables(gwss, sates);   //OSPF最短路由
      experiment.InitialSatRouter(gwss, sates, satClusterNodes, monitors, _consType, orbit_num, sate_num); // 初始化卫星路由策略
    }

  } 
  void updateTopoSim(string dataArray, string& retArray){
  //修改需求：
  /**
   1.根据post_node统计全网内的节点数目，包含卫星和地面站节点数
   2.搭建星地网络拓扑：
   1）构建地面站和主控制器之间的连接关系
   2）构建星间链路拓扑信息
   3）设置节点IP协议栈
   3.构建拓扑后需要添加可见性判断--星地和星间可见性
  **/

    // 解析 JSON 数据
    json jsonData = json::parse(dataArray);
    uint32_t id1, id2;
    Ptr<Node> node1, node2;

    for(const auto& item : jsonData["data"]){
      if(item["type"] == "feeder") LinkChangeSim(allnode, item["hold_time"], item["node1_id"], item["node2_id"]); //更新星地链路可见时间
      else{
        if(item["type"] == "ground") continue; // 目前不考虑地面间链路的变化
        id1 = item["node1_id"];
        id2 = item["node2_id"];
        node1 = allnode.Get(id_node[id1]); 
        node2 = allnode.Get(id_node[id2]);
        //if(node1->GetId() < sateBegID || node2->GetId() < sateBegID) continue;
        std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> P2Pdevices = GetDevicesFromNodes(node1, node2);
        Ptr<PointToPointNetDevice> dev1 = P2Pdevices.first;
        Ptr<PointToPointNetDevice> dev2 = P2Pdevices.second;
        Ptr<PointToPointChannel> channel1 = DynamicCast<PointToPointChannel>(dev1->GetChannel());
        Ptr<PointToPointChannel> channel2 = DynamicCast<PointToPointChannel>(dev2->GetChannel());
        if(channel1 != channel2) std::cerr << "The two nodes are not connected through the same channel." << std::endl;
        
        // 更新链路和dev信息
        // 链路带宽
        string dr = to_string(item["link_bandwidth"]) ;
        dr += "kbps";
        dev1->SetAttribute("DataRate", DataRateValue(DataRate(dr)));
        dev2->SetAttribute("DataRate", DataRateValue(DataRate(dr)));
        // 链路时延
        double seconds = item["delay"];
        channel1->SetAttribute("Delay", TimeValue(MicroSeconds(seconds)));
        // 链路当前负载
        // node1到node2
        // node2到node1
        dr = to_string(item["link_load_up"]) ;
        dr += "kbps";
        dev1->SetAttribute("DataLoad", DataRateValue(DataRate(dr)));
        dr = to_string(item["link_load_down"]) ;
        dr += "kbps";
        dev2->SetAttribute("DataLoad", DataRateValue(DataRate(dr)));

        // // 输出检查是否设置成功
        // Time delay = channel1->GetDelay();
        // cout << "链路带宽：" 
        //      << "dev1 " << dev1->GetDataRate() << " bps\t"
        //      << "dev2 " << dev2->GetDataRate() << " bps\n"
        //      << "delay:" 
        //      << "dev1 " << delay.GetMicroSeconds() << " us\n"
        //      << "链路负载：" 
        //      << "dev1 " << dev1->GetDataLoad() << " bps\t"
        //      << "dev2 " << dev2->GetDataLoad() << " bps\n"
        //      << endl;
      }
    }

    /* 调用分簇 以及 路由算法，重新分簇和计算路由*/
    // 1、分簇——依据平均链路利用率计算簇的适应度
    UpdateCluster(sates);
    // cout<<"分簇更新"<<endl;
    // cout<<sates_num<<endl;
    // cout<<sate_num<<endl;
    // cout<<orbit_num<<endl;
    /* 注意：路由需要更新到retArray中所有链路的面向连接路由信息 */
    // 2、路由——更新卫星路由策略   
    nlohmann::json_abi_v3_11_3::ordered_json paths = json::array();
    Ptr<Node> srcNode; // 源节点
    Ptr<Node> dstNode; // 目的节点

    NodeContainer mainPathNodes, remainNodes, backupPathNodes;
    std::vector<std::vector<NodeContainer>> lastBackupPathNodes;
    lastBackupPathNodes.resize(sates_num);
    for (uint32_t  i = 0; i < sates_num; i++) {
        lastBackupPathNodes[i].resize(sates_num);
    }
    NodeContainer lastPathNodes[sates_num][sates_num];
    //NodeContainer lastBackupPathNodes[sates_num][sates_num];
    uint32_t tunnelID = 101;    // !< 隧道号，全局唯一ID，标识某条路径。由模型方提供，不会重复
    std::map<uint32_t, NodeContainer> pathIdentify;
    std::map<uint32_t, NodeContainer> backupPathIdentify;

    for(uint32_t i = 0; i < sates.GetN(); ++i)
    {
        for(uint32_t j = 0; j < sates.GetN(); ++j)
        {
            if(j == i)  continue;
            srcNode = sates.Get(i);
            dstNode = sates.Get(j); 
            lastPathNodes[i][j] = experiment.GetMainPathNodesSim(srcNode, dstNode, sates);     // 最后一个参数目前只包含卫星，后续考虑增加地面站
            //lastBackupPathNodes[i][j] = experiment.GetBackupPathNodesSim(srcNode, dstNode, mainPathNodes, sates); 
            pathIdentify[tunnelID] = lastPathNodes[i][j];
            backupPathIdentify[tunnelID] = lastPathNodes[i][j];//lastBackupPathNodes[i][j];
            tunnelID++;
        }
    }
    experiment.UpdateSatRouterSim(sates, satClusterNodes, _consType, orbit_num, sate_num);
    
    // 以下代码基于tunnel号
    for(std::map<uint32_t, NodeContainer>::iterator it = pathIdentify.begin(); it != pathIdentify.end(); it++)
    {
      uint32_t pathTunnel = it->first;
      uint32_t pathNodeNum = it->second.GetN();
      uint32_t backupPathNodeNum = backupPathIdentify[pathTunnel].GetN();
      
      if(pathNodeNum == 0) continue;

      srcNode = it->second.Get(0);
      dstNode = it->second.Get(pathNodeNum-1);

      nlohmann::json_abi_v3_11_3::ordered_json temp;
      std::cout << "srcNode: " << srcNode->GetId() << ", dstNode: " << dstNode->GetId() << std::endl;
      mainPathNodes = experiment.GetMainPathNodesSim(srcNode, dstNode, sates);     // 最后一个参数目前只包含卫星，后续考虑增加地面站
      uint32_t mainPathNumtmp = mainPathNodes.GetN();
      if(mainPathNumtmp == 0)
      {
        //std::cout << "no routing path" << std::endl;
        mainPathNodes.Add(it->second);
        mainPathNumtmp = mainPathNodes.GetN();
      }
      
      //std::cout << std::endl << "mainPath: " ;
      // for(uint32_t k = 0; k < (mainPathNumtmp-1); k++)
      // {
      //   std::cout << mainPathNodes.Get(k)->GetId() << "->";
      // }
      //std::cout << mainPathNodes.Get(mainPathNumtmp-1)->GetId() << std::endl;

      //std::cout << "lastMainPath: " ;
      // for(uint32_t k = 0; k < (pathNodeNum-1); k++)
      // {
      //   std::cout << it->second.Get(k)->GetId() << "->";
      // }
      //std::cout << it->second.Get(pathNodeNum-1)->GetId() << std::endl;

      backupPathNodes = experiment.GetBackupPathNodesSim(srcNode, dstNode, mainPathNodes, sates); 
      uint32_t backupPathNumtmp = backupPathNodes.GetN();
      if(backupPathNumtmp == 0)
      {
        //std::cout << "no backup routing path" << std::endl;
        backupPathNodes.Add(backupPathIdentify[pathTunnel]);
        backupPathNumtmp = backupPathNodes.GetN();
      }
      //std::cout << "backupPathNodes: " ;
      // for(uint32_t k = 0; k < (backupPathNumtmp-1); k++)
      // {
      //   std::cout << backupPathNodes.Get(k)->GetId() << "->";
      // }
      //std::cout << backupPathNodes.Get(backupPathNumtmp-1)->GetId() << std::endl;  

      //std::cout << "lastBackupMainPath: " ;
      // for(uint32_t k = 0; k < (backupPathNodeNum-1); k++)
      // {
      //   std::cout << backupPathIdentify[pathTunnel].Get(k)->GetId() << "->";
      // }
      //std::cout << backupPathIdentify[pathTunnel].Get(backupPathNodeNum-1)->GetId() << std::endl;
            
      bool isPathChange = 0;
      for(uint32_t m = 0; m < pathNodeNum; ++m)
      {
        if(mainPathNodes.GetN() != pathNodeNum)
        {
          //std::cout << "mainPathNodes Change." << std::endl;
          isPathChange = 1;
          break;
        }
        uint32_t mainNodeId = mainPathNodes.Get(m)->GetId();
        uint32_t lastNodeId = it->second.Get(m)->GetId();
        if(mainNodeId != lastNodeId)
        {
          //std::cout << "mainPathNodes Change." << std::endl;
          isPathChange = 1;
          break;
        }
      }

      for(uint32_t m = 0; m < backupPathNodeNum; ++m)
      {
        if(backupPathNodes.GetN() != backupPathIdentify[pathTunnel].GetN())
        {
          //std::cout << "backupPathNodes Change." << std::endl;
          isPathChange = 1;
          break;
        }
        uint32_t backupNodeId = backupPathNodes.Get(m)->GetId();
        uint32_t lasBackupNodeId = backupPathIdentify[pathTunnel].Get(m)->GetId();
        if(backupNodeId != lasBackupNodeId)
        {
          //std::cout << "backupPathNodes Change." << std::endl;
          isPathChange = 1;
          break;
        }
      }

      if(isPathChange)
      {
        temp["src_node_id"] = node_id[srcNode->GetId()];//srcNode->GetId() - mcsnum + 1
        temp["dst_node_id"] = node_id[dstNode->GetId()];
        temp["tunnel_id"] = pathTunnel;      // tunnel_id

        json nodes = json::array();
        for(uint32_t k=0; k<mainPathNodes.GetN(); k++)
        {
            nodes.push_back(node_id[(Ptr<Node>(mainPathNodes.Get(k)))->GetId()]);
        }
        temp["links"] = nodes;

        json backupNodes = json::array();
        for(uint32_t  j=0; j<backupPathNodes.GetN(); j++)
        {
            backupNodes.push_back(node_id[(Ptr<Node>(backupPathNodes.Get(j)))->GetId()]);
        }
        temp["backup_links"] = backupNodes;  
        
        paths.push_back(temp);          
      }
    }

    // 以下代码基于无tunnel版本 ， 逻辑不同
    // for(uint32_t i = 0; i < sates.GetN(); ++i)
    // {
    //     for(uint32_t j = 0; j < sates.GetN(); ++j)
    //     {
    //         if(j == i)  continue;
    //         nlohmann::json_abi_v3_11_3::ordered_json temp;
    //         srcNode = sates.Get(i);
    //         dstNode = sates.Get(j);
    //         // std::cout << "srcNode: " << srcNode->GetId() << ", dstNode: " << dstNode->GetId() << std::endl;
    //         mainPathNodes = experiment.GetMainPathNodesSim(srcNode, dstNode, sates);     // 最后一个参数目前只包含卫星，后续考虑增加地面站
    //         std::cout << std::endl << "mainPath: " ;
    //         for(uint32_t k = 0; k < mainPathNodes.GetN(); k++)
    //         {
    //           std::cout << mainPathNodes.Get(k)->GetId() << "->";
    //         }
    //         std::cout << std::endl;

    //         std::cout << "lastMainPath: " ;
    //         for(uint32_t k = 0; k < lastPathNodes[i][j].GetN(); k++)
    //         {
    //           std::cout << lastPathNodes[i][j].Get(k)->GetId() << "->";
    //         }
    //         std::cout << std::endl;

    //         backupPathNodes = experiment.GetBackupPathNodesSim(srcNode, dstNode, mainPathNodes, sates); 
    //         std::cout << "backupPathNodes: " ;
    //         for(uint32_t k = 0; k < backupPathNodes.GetN(); k++)
    //         {
    //           std::cout << backupPathNodes.Get(k)->GetId() << "->";
    //         }
    //         std::cout << std::endl;  

    //         std::cout << "lastBackupMainPath: " ;
    //         for(uint32_t k = 0; k < lastBackupPathNodes[i][j].GetN(); k++)
    //         {
    //           std::cout << lastBackupPathNodes[i][j].Get(k)->GetId() << "->";
    //         }
    //         std::cout << std::endl;
            
    //         bool isPathChange = 0;
    //         uint32_t lastPathNum = lastPathNodes[i][j].GetN();
    //         for(uint32_t m = 0; m < lastPathNum; ++m)
    //         {
    //           if(mainPathNodes.GetN() != lastPathNodes[i][j].GetN())
    //           {
    //             std::cout << "mainPathNodes Change." << std::endl;
    //             isPathChange = 1;
    //             break;
    //           }
    //           uint32_t mainNodeId = mainPathNodes.Get(m)->GetId();
    //           uint32_t lastNodeId = lastPathNodes[i][j].Get(m)->GetId();
    //           if(mainNodeId != lastNodeId)
    //           {
    //             std::cout << "mainPathNodes Change." << std::endl;
    //             isPathChange = 1;
    //             break;
    //           }
    //         }

    //         uint32_t lastBackupPathNum = lastBackupPathNodes[i][j].GetN();
    //         for(uint32_t m = 0; m < lastBackupPathNum; ++m)
    //         {
    //           if(backupPathNodes.GetN() != lastBackupPathNodes[i][j].GetN())
    //           {
    //             std::cout << "backupPathNodes Change." << std::endl;
    //             isPathChange = 1;
    //             break;
    //           }
    //           uint32_t backupNodeId = backupPathNodes.Get(m)->GetId();
    //           uint32_t lasBackupNodeId = lastBackupPathNodes[i][j].Get(m)->GetId();
    //           if(backupNodeId != lasBackupNodeId)
    //           {
    //             std::cout << "backupPathNodes Change." << std::endl;
    //             isPathChange = 1;
    //             break;
    //           }
    //         }

    //         if(isPathChange)
    //         {
    //           temp["src_node_id"] = srcNode->GetId() - mcsnum + 1;
    //           temp["dst_node_id"] = dstNode->GetId() - mcsnum + 1;
    //           temp["tunnel_id"] = 100;      // tunnel_id
              
    //           json nodes = json::array();
    //           for(uint32_t k=0; k<mainPathNodes.GetN(); k++)
    //           {
    //               nodes.push_back((Ptr<Node>(mainPathNodes.Get(k)))->GetId() - mcsnum + 1);
    //           }
    //           temp["links"] = nodes;

    //           json backupNodes = json::array();
    //           for(uint32_t  j=0; j<backupPathNodes.GetN(); j++)
    //           {
    //               backupNodes.push_back((Ptr<Node>(backupPathNodes.Get(j)))->GetId() - mcsnum + 1);
    //           }
    //           temp["backup_links"] = backupNodes;  
              
    //           paths.push_back(temp);              
    //         }
    //     }
    // }
    
    retArray = paths.dump(4);
  }

  void updateRouteSim(string dataArray, string& retArray){
    json jsonData = json::parse(dataArray);
    json item = jsonData["data"];
    uint32_t id1 = item["src_node_id"];
    uint32_t id2 = item["dst_node_id"];
    uint32_t tunnel_id = item["tunnel_id"];  
    Ptr<Node> node1 = allnode.Get(id_node[id1]); // 传输的json数据的节点id都是从1开始
    Ptr<Node> node2 = allnode.Get(id_node[id2]);
    
    /* 根据源节点和目的节点计算面向连接路由并更新到retArray中 */
    /* tunnel_id的作用：直接放置于返回格式中，用于标识一条路径 */

    NodeContainer mainPathNodes = experiment.GetMainPathNodesSim(node1, node2, sates);     // 最后一个参数目前只包含卫星，后续考虑增加地面站
    json nodes = json::array();
    for(uint32_t j=0; j<mainPathNodes.GetN(); j++)
    {
        nodes.push_back(node_id[(Ptr<Node>(mainPathNodes.Get(j)))->GetId()]);
    }

    NodeContainer backupPathNodes = experiment.GetBackupPathNodesSim(node1, node2, mainPathNodes, sates);
    json backupNodes = json::array();
    for(uint32_t  j=0; j<backupPathNodes.GetN(); j++)
    {
        backupNodes.push_back(node_id[(Ptr<Node>(backupPathNodes.Get(j)))->GetId()]);
    }
 
    nlohmann::json_abi_v3_11_3::ordered_json temp;
    temp["src_node_id"] = id1;
    temp["dst_node_id"] = id2;
    temp["tunnel_id"] = tunnel_id;
    temp["links"] = nodes;
    temp["backup_links"] = backupNodes;
    retArray = temp.dump(4);
  }

  void deleteRouteSim(string dataArray){
    json jsonData = json::parse(dataArray);
    json item = jsonData["data"];
    uint32_t id1 = item["src_node_id"];
    uint32_t id2 = item["dst_node_id"];
    Ptr<Node> node1 = allnode.Get(id_node[id1]); // 传输的json数据的节点id都是从1开始
    Ptr<Node> node2 = allnode.Get(id_node[id2]);

    /* 根据源节点和目的节点删除对应路由，无需返回值 */
    experiment.DeleteSatRoutesSim(node1, node2);
  }

  void updateClusterSim(string& retArray){
    json clusters = json::array();

    for(uint32_t  i=0; i<satClusterNodes.size(); i++){
      json temp;
      temp["header_id"] = node_id[(Ptr<Node>(satClusterNodes[i].Get(0)))->GetId()];
      json nodes = json::array();
      for(uint32_t  j=1; j<satClusterNodes[i].GetN(); j++){
        nodes.push_back(node_id[(Ptr<Node>(satClusterNodes[i].Get(j)))->GetId()]);
      }
      temp["intra_nodes"] = nodes;
      clusters.push_back(temp);
    }

    retArray = clusters.dump(4);
  }

  void updateTopo(){
    // #ifdef NS3_OPENFLOW
    // 周期性更新簇
    // slavescs内保存所有子控制器，新建一个表保存所有卫星节点对应的子控制器ID
    // 出现变更，即更新
    // 需要更新主控制器，备份主控制器以及所有子控制器、普通卫星节点的子控制器信息
    // SlavesControllers子控制器集合

    // // ******************************* 输出，用于检查是否成功更新簇信息 *******************************
    // std::cout << "-----------------------------" << std::endl;
    // for(uint32_t i=0; i < satClusterNodes.size(); ++i){
    //   std::cout << "簇首ID:" << satClusterNodes[i].Get(0)->GetId() << "\t成员ID:" ;
    //   for(uint32_t j=0; j<satClusterNodes[i].GetN(); j++){
    //     std::cout << satClusterNodes[i].Get(j)->GetId() << ", ";
    //   }
    // }
    // std::cout << std::endl << "---------------------------" << std::endl;
    std::cout << "周期性更新拓扑" << std::endl;
    // OpenFlowSwitchHelper ofSwitchHelper;
    for(auto&t:satClusterNodes)
    {
      for(uint32_t i = 0; i <t.GetN(); i++)
      {
        cout<<t.Get(i)->GetId()<<" ";
      }
      cout<<endl;
    }

    if(!_slaveMode){
      // 从控制器为簇首时，簇发生更新，需要更新主从和普通节点
      // 更新子控制器集合
      slavescs = NodeContainer();
      for(uint32_t i=0; i < satClusterNodes.size(); ++i){
        slavescs.Add(satClusterNodes[i].Get(0));
        for(uint32_t j = 0; j != satClusterNodes[i].GetN(); j ++){
          Ptr<Node> node = DynamicCast<satelliteNode>(satClusterNodes[i].Get(j));
          if(NodesSlaveID[node->GetId()] != satClusterNodes[i].Get(0)->GetId()){
            // 本节点的子控制器ID发生变化
            changeClusterID[node->GetId()] ++;
            NodesSlaveID[node->GetId()] = satClusterNodes[i].Get(0)->GetId();
          }
        }
      }
      // 更新节点状态以及控制器信息
      Ptr<satelliteNode> node = DynamicCast<satelliteNode>(mcs.Get(0));
      node->SetToMasterController(interval, 0, mcs, slavescs);
      node = DynamicCast<satelliteNode>(mcs.Get(1));
      node->SetToMasterController(interval, 0, mcs, slavescs);

      for(uint32_t  i=0; i<satClusterNodes.size(); i++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(satClusterNodes[i].Get(0));
        node->SetToSlaveController(interval, mcs, slavescs, satClusterNodes[i]);
        for(uint32_t  j=1; j<satClusterNodes[i].GetN(); j++){
          Ptr<satelliteNode> temp = DynamicCast<satelliteNode>(satClusterNodes[i].Get(j));
          temp->SetToNormal(interval, mcs, slavescs, satClusterNodes[i]);
        }
      }
    }
    else{
        // 更新节点状态以及控制器信息--从控制器为地面站不随着时间变化
      NodeContainer satClusterHead;
      for(uint32_t  i=0; i<satClusterNodes.size(); i++){
        satClusterHead.Add(satClusterNodes[i].Get(0));
        for(uint32_t  j=0; j<satClusterNodes[i].GetN(); j++){
          Ptr<satelliteNode> temp = DynamicCast<satelliteNode>(satClusterNodes[i].Get(j));
          temp->SetToNormal(interval, mcs, slavescs, satClusterNodes[i]);
        }
      }
      for(uint32_t i = 0; i < scs.GetN(); i++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(scs.Get(i));
        node->SetToSlaveController(interval, mcs, slavescs, satClusterHead);
      }
    }
    
    Time currentTime = Simulator::Now();
    // 将时间转换为秒
    double currentTimeInSeconds = currentTime.GetSeconds();
    if(_mode == 0 || (_mode == 2 && currentTimeInSeconds >= 15) || _mode == 3){
      Ptr<satelliteNode> node = DynamicCast<satelliteNode>(mcs.Get(0));
      node->SendIdentity();
    }

    // 更新卫星路由策略
    // if(_SDNRoute && (Simulator::Now().GetSeconds() >= 1.0)) experiment.UpdateSatRouter(sates, satClusterNodes, monitors, _consType, orbit_num, sate_num);

    if(_BreakDetect) testBreakDetect();
    
    // 周期性调度，根据动态分簇结果更新管控架构
    Simulator::Schedule(Seconds(clusterUpdateStep), &updateTopo);

    // #endif
  }

    void updateRou(){
    // #ifdef NS3_OPENFLOW

    // 更新卫星路由策略
    if(_SDNRoute && (Simulator::Now().GetSeconds() >= 1.0) && (Simulator::Now().GetSeconds() < totalTimeStep)) 
        experiment.UpdateSatRouter(sates, satClusterNodes, monitors, _consType, orbit_num, sate_num);
    Simulator::Schedule(Seconds(routeUpdateStep), &updateRou);

    // #endif
  }

  void rouReconvergence(){
    // 主控制器失联，测试卫星路由收敛时间
    if(_SDNRoute && (Simulator::Now().GetSeconds() < totalTimeStep)) 
        experiment.UpdateSatRouterForConvergence(sates, satClusterNodes, monitors, _consType, orbit_num, sate_num);
  }

  // 主控制器失效时调用，将主控制器迁移至备份主控
  void master_migration(){
    // // 地面站
    // for (uint32_t i = 0; i < gwss.GetN(); ++i) {
    //   Ptr<Node> node = gwss.Get(i);
    //   assert(node->m_devices.size() >= 2);
    //   for(uint32_t j=node->GetBeginId(); j<node->GetNDevices()-1; j++){
    //     Ptr<OpenFlowSwitchNetDevice> odev = DynamicCast<OpenFlowSwitchNetDevice>(node->GetDevice(j));
    //     odev->ChangeController(BackupMasterController);
    //   }
    // }

    // MasterController->ToBackup();
    Ptr<satelliteNode> node = DynamicCast<satelliteNode>(mcs.Get(masterID));
    masterID = (masterID+1)%2;
    node->ToBackup(masterID);

    // auto temp = MasterController;
    // MasterController = BackupMasterController;
    // BackupMasterController = temp;
  
    std::cout << "完成主备切换！" << std::endl;
  }

}