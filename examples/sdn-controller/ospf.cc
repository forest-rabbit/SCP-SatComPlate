#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/cluster-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h" // 包含流量控制相关的类
#include "ns3/packet-sink-helper.h"
#include "ns3/on-off-helper.h"
#include <cstdlib>
// #include "para.h"
// #include "access.h"

#include <random>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("SimulationTest");

uint32_t sates_num = 60;   // N: LEO卫星总数
uint32_t orbit_num = 6;   // No: 轨道数
double totalTimeStep = 110;    // 仿真时间步总长 

uint32_t sate_num = sates_num / orbit_num;  // Ns:每个轨道上卫星数量
double timeStepSize = 1;   // 时间步长度（s）
long int  linkBandwidth = 100000000;   // 链路带宽 10Gbps 1：100Mbps
int topo = 1;      // 初始化topo构型，1-60，2-108， 3-60+108

//可见性
//获取excel数据
int rows = 0;
int cols = 0;
double* GetData(std::string name)
{
    // 重置行数和列数
     rows = 0;
     cols = 0;
    std::ifstream inFile(name);
    if (!inFile.is_open()) {
        std::cout << "无法打开文件: " << name << std::endl;
        exit(1);
    }

    std::string lineStr;
    std::vector<double> tempData; // 使用动态数组存储数据

    // 逐行读取文件内容
    while (getline(inFile, lineStr)) {
        std::stringstream ss(lineStr);
        std::string str;

        // 按照逗号分割每一行数据
        while (getline(ss, str, ',')) {
            double num = std::stod(str);
            tempData.push_back(num); // 将数据存入动态数组
        }

        // 第一行数据决定列数
        if (cols == 0) {
            cols = tempData.size();
        }
        rows++; // 每读取一行数据，行数加一
    }

    inFile.close(); // 关闭文件

    // 分配足够大小的内存，将数据复制到一维数组中
    double* Data = (double*)malloc(rows * cols * sizeof(double));
    if (Data == nullptr) {
        std::cout << "内存分配失败." << std::endl;
        return nullptr;
    }
    // 将动态数组中的数据复制到一维数组中
    for (int i = 0; i < rows * cols; ++i) 
    {
        *Data = tempData[i];
        Data++;
    }
    return Data; // 返回分配的内存数组指针 优化为vector<double>
}

std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>>
GetDevicesFromNodes(Ptr<Node> node1, Ptr<Node> node2)
{
    Ptr<PointToPointNetDevice> p2pNetDevice;
    Ptr<PointToPointNetDevice> p2pNetDevice2;
    for(uint32_t i = 0; i < node1->GetNDevices(); ++i) //更改 GetBeginId()
    {
        if(i >= 5) break; //bug
        p2pNetDevice = DynamicCast<PointToPointNetDevice>(node1->GetDevice(i));
        if(p2pNetDevice != nullptr)
        {
            Ptr<Channel> channel = p2pNetDevice->GetChannel ();
            // 获取通道中连接的所有设备
            Ptr<NetDevice> otherDevice;
            if (channel->GetNDevices () == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == p2pNetDevice) 
                {
                    otherDevice = channel->GetDevice (1);
                } 
                else 
                {
                    otherDevice = channel->GetDevice (0);
                }
            }
            Ptr<Node> connectedNode = otherDevice->GetNode();
            if(connectedNode->GetId() == node2->GetId())
            {
                p2pNetDevice2 = DynamicCast<PointToPointNetDevice>(otherDevice);
                break;
            }
        }
    }
    return {p2pNetDevice, p2pNetDevice2};
}

void 
LinkDown(Ptr<Node> node1, Ptr<Node> node2)
{
    cout<<"时间: "<<Simulator::Now().GetSeconds()<< " "<<node1->GetId()<<" "<<node2->GetId()<<" 两个节点断开"<<endl;
    std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> P2Pdevices = GetDevicesFromNodes(node1, node2);
    Ptr<PointToPointNetDevice> dev1 = P2Pdevices.first;
    Ptr<PointToPointNetDevice> dev2 = P2Pdevices.second;
    dev1->DownTheLink();
    dev2->DownTheLink();
}

void
LinkUp(Ptr<Node> node1, Ptr<Node> node2)
{
    cout<<"时间: "<<Simulator::Now().GetSeconds()<< " "<<node1->GetId()<<" "<<node2->GetId()<<" 两个节点连接"<<endl;
    std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> P2Pdevices = GetDevicesFromNodes(node1, node2);
    Ptr<PointToPointNetDevice> dev1 = P2Pdevices.first;
    Ptr<PointToPointNetDevice> dev2 = P2Pdevices.second;
    dev1->UpTheLink();
    dev2->UpTheLink();
}

//文件中的节点编号从0开始，输入的是卫星节点容器，打印的节点编号偏移量为7
void 
LinkChange(NodeContainer& nodes,std::string name)
{
    double* Data = GetData(name);
    cout<<"行数："<<" "<<rows<<" "<<"列数："<<cols<<endl;
    for(int i = 0; i < rows; ++i)
    {
        uint32_t Id1 = uint32_t(*(Data - cols * rows + i * cols)); 
        uint32_t Id2 = uint32_t(*(Data - cols * rows + i * cols + 1)); 
        double time = *(Data - cols * rows + i * cols + 2);
        int LinkFlag = int(*(Data - cols * rows + i * cols + 3));
        if(LinkFlag == 0) Simulator::Schedule(Seconds(time), &LinkDown, nodes.Get(Id1), nodes.Get(Id2));//LinkDown
        if(LinkFlag == 1) Simulator::Schedule(Seconds(time), &LinkUp, nodes.Get(Id1), nodes.Get(Id2));//LinkUp;
        //cout<<"*** 节点："<<nodes.Get(Id1)->GetId()<<" 节点："<<nodes.Get(Id2)->GetId()<<"时间："<<time<<" 链路: "<<LinkFlag<<endl;
    }
}

//获取excel数据
void GetData1(vector<vector<double>>& data, const int destNum, double load, std::string name)
{
    vector<vector<double>> temp_data(destNum*100, vector<double>(destNum, 0.0));
	std::ifstream inFile(name, std::ios::in);
	std::string lineStr;
    std::cout << "GET DATA FROM " << name << "\tsateNum:" << destNum << std::endl;

    int i = 0;
    int j = 0;
	while (getline(inFile, lineStr))
	{
    if(i >= (int)temp_data.size()) break;
		std::stringstream ss(lineStr);
		std::string str;
		while (getline(ss, str, ','))
		{
			double num = stod(str);
			temp_data[i][j] = load * num;     // Gbps
            j++;
            if( j == destNum) j = 0;
		}
        i++;
	}
  inFile.close();

  //流量矩阵累加 m:行 n:列 流量发送时间:100
  for(int m = 0; m < destNum; m++)
  {
    for(int n = 0; n < 100; n++)
    {
        int index = m + n*destNum;
        if(index < destNum*100)
        {
            for(int k = 0;k < destNum; k++)
            {
                data[m][k] += temp_data[index][k];
                // cout<<"data[" <<m<<"]["<<k << "]\t"<< data[m][k] <<endl;
            }
        }
    }
  }
//   cout<<"行：" <<data.size()<<"列："<<data[0].size()<<endl;

}

void installClient(vector<vector<double>> data, uint32_t numNodes, uint16_t portStart, NodeContainer& nodes){
//   double currentTime = Simulator::Now().GetSeconds(); // 单位为s
//   if(currentTime >= totalTimeStep) return;

  // 周期性为每个节点配置客户端应用程序，使其向其他节点发送数据包
  for (uint32_t i = 0; i < numNodes; ++i)
  {
    Ptr<Node> clientNode = nodes.Get(i);
    // 循环目的地址
    for(uint32_t j = 0; j < numNodes; j++){
    //   int row = currentTime*numNodes + i;
      if(data[i][j] == 0.0) continue;

      uint32_t packetSize = 1024;   // 字节
      //int maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
      //cout<<maxPacketCount<<endl;
      //double interPacketInterval = (double)100/maxPacketCount;   // 数据包间隔 100s的流量
      //double interPacketInterval = 0.00004;
    
       double dataRate = (double) data[i][j] * 1024.0 * 1024.0 * 1024.0;
    //   cout<<dataRate<<endl; 

      Ptr<Ipv4> destIp = nodes.Get(j)->GetObject<Ipv4>(); //获取目的节点
      Ipv4Address ip_address = destIp->GetAddress(1, 0).GetLocal(); //设置为第一个网卡的IP 
//TCP
      OnOffHelper client("ns3::TcpSocketFactory", Address(InetSocketAddress(ip_address, portStart)));
      client.SetConstantRate(DataRate(dataRate), packetSize);  // 设置速率并指定包大小

//UDP
    //   UdpClientHelper client(ip_address, portStart);

    //   client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
    //   client.SetAttribute("Interval", TimeValue(Seconds(interPacketInterval)));
    //   client.SetAttribute("PacketSize", UintegerValue(1024));

    //   cout << "node:" << clientNode->GetId() 
    //        << "\tmaxPacketCount:" << maxPacketCount
    //        << "\tinterPacketInterval:" << interPacketInterval  
    //        << "\tPacketSize:" << 1024  
    //        << endl;
      ApplicationContainer apps = client.Install(clientNode);
      apps.Start(Seconds(0));
      apps.Stop(Seconds(totalTimeStep));
    }
  }

  // 周期性调度，根据动态分簇结果更新管控架构
  //Simulator::Schedule(Seconds(1), &installClient, data, numNodes, portStart, nodes);
}

// 为每个节点创建应用 client加server
void buildApp(NodeContainer& nodes, double load){
  vector<vector<double>> data(nodes.GetN(), vector<double>(nodes.GetN(), 0.0));
//   // 获取excel数据
//   GetData1(data, nodes.GetN(), load, "scratch/traffic_matrix(Sat1)_100Mbps.csv");
//   // if(_isSate1) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(SatI).csv");
//   // else GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(SatII).csv");

  if(topo == 1){
    if(linkBandwidth == 10000000000) GetData1(data, nodes.GetN(), load, "examples/sdn-controller/traffic_matrix(Sat1).csv");
    else if(linkBandwidth == 100000000) GetData1(data, nodes.GetN(), load, "examples/sdn-controller/traffic_matrix(Sat1)_100Mbps.csv");
    else if(linkBandwidth == 500000000) GetData1(data, nodes.GetN(), load, "examples/sdn-controller/traffic_matrix(Sat1)_500Mbps.csv");
  }
  else if(topo == 2){
    if(linkBandwidth == 10000000000) GetData1(data, nodes.GetN(), load, "examples/sdn-controller/traffic_matrix(Sat2).csv");
    else if(linkBandwidth == 100000000) GetData1(data, nodes.GetN(), load,  "examples/sdn-controller/traffic_matrix(Sat2)_100Mbps.csv");
    else if(linkBandwidth == 500000000) GetData1(data, nodes.GetN(), load, "examples/sdn-controller/traffic_matrix(Sat2)_500Mbps.csv");
  }


  // 预设端口号范围
  uint16_t portStart = 9; // well-known echo port number
  uint32_t numNodes = nodes.GetN();

  // 为每个节点分配一个唯一的端口号
  std::map<Ptr<Node>, uint16_t> nodePortMap;
  for (uint32_t i = 0; i < sates_num; ++i)
  {
    uint16_t port = portStart + i; // 假设端口号从9开始，每个节点增加1
    nodePortMap[nodes.Get(i)] = port;
  }

  // 安装服务器应用程序到每个节点
  for (uint32_t i = 0; i < numNodes; ++i)
  {
    PacketSinkHelper server("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), nodePortMap[nodes.Get(i)]));
    ApplicationContainer apps = server.Install(nodes.Get(i));
    apps.Start(Seconds(0));
    apps.Stop(Seconds(totalTimeStep));
  }

  installClient(data, numNodes, portStart, nodes);
}

void OutputNodeInfo(const vector<Ptr<LinkUtilizationMonitor>> &monitors)
{
    // 打开文件
    std::ofstream file;
    file.open("examples/sdn-controller/output/ospf-linkUtilization.txt", std::ios::app);

file << "currTime:" << Simulator::Now().GetSeconds() << std::endl;
    for (uint32_t i = 0; i < monitors.size(); ++i) 
{
    Ptr<LinkUtilizationMonitor> monitor = monitors[i];
    for(auto iter = monitor->m_totalDevice.begin(); iter != monitor->m_totalDevice.end(); iter++){
        file << "nodeID:" << i << "\tif:" << iter->first << "\trecvBytes:" << iter->second << std::endl;
    }
}
    file.close();

    Simulator::Schedule(Seconds(5.0), &OutputNodeInfo, monitors);
}

int main (int argc, char *argv[])
{

    NS_LOG_UNCOND ("Simulation Test");

    double load = 0.5; // 初始化负载参数

    CommandLine cmd;
    cmd.AddValue ("load", "Network load factor", load);
    cmd.AddValue ("topo", "Some parameter", topo);
    cmd.AddValue ("linkBandwidth", "Some parameter", linkBandwidth);
    cmd.Parse (argc, argv);

    std::cout << "offeredLoad:" << load
                << "\ttopo:" << topo
                << "\tlinkBandwidth:" << linkBandwidth
                << std::endl;
                
                
    // 设置 TCP 协议栈的类型为 NewReno
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName("ns3::TcpCubic")));

    // 设置初始拥塞窗口大小为 2-10-20
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // 设置数据段大小（分段的最大传输单元，通常等同于 packet size）1024+20
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1024));

    // 设置快速重传的重复 ACK 计数阈值为 1（默认是 3）
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));
    // 启用选择性确认 (SACK)，提高快速恢复性能
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    // 设置初始慢启动阈值为 65535 字节 65535
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue(100000));

    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));

    // 设置发送和接受缓冲区
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(65535 * 10)); // 增加发送缓冲区
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(65535 * 10)); // 增加接收缓冲区



    if(topo == 1){
        sates_num = 60;   // N: LEO卫星总数
        orbit_num = 6;   // No: 轨道数
    }
    else if(topo == 2){
        sates_num = 108;   // N: LEO卫星总数
        orbit_num = 12;   // No: 轨道数
    }

    sate_num = sates_num / orbit_num;  // Ns:每个轨道上卫星数量


    // double Throughput = 0;
    // uint32_t lostPackets = 0;
    // uint32_t RxPackets = 0;
    // uint32_t TxPackets = 0;
    // double Delay_time = 0;
    // double start_time = 0.0;//需要根据自定义的流量传输开始时间进行定义（需要根据实际情况进行更改）
    // double end_time = 0.0;//初始化为start_time一样的数据
    // double Latency = 0;
    // double AverageDelayTime = 0.0;
    // double SumDelayTime = 0;
    // double SumJitterTime = 0;
    // double Jitter = 0;
    //Time simulationEndTime = Seconds(totalTimeStep);
    // int num_plane = 6;
    // int num_sat_plane = 11;
    for(double currentLoad = load; currentLoad < 6.5; currentLoad += 0.5)
    {
    cout<<"负载: "<<load<<endl;
    NodeContainer total_node;
    total_node.Create(sate_num*orbit_num); //设置卫星数目
    cout<<"the node num: "<<total_node.GetN()<<endl;

    PointToPointHelper link;
    link.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
    link.SetChannelAttribute("Delay", StringValue("14ms"));
    link.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));//队列容量K=1000
    NetDeviceContainer devices;
     
    Ipv4InterfaceContainer interfaces;
    InternetStackHelper stack;
    stack.Install(total_node);

    // 创建TrafficControlHelper帮助器来配置流量控制
    TrafficControlHelper tch;
    // 配置RED队列规则
    tch.SetRootQueueDisc("ns3::RedQueueDisc", "MaxSize", StringValue("1000p"), "MaxTh", DoubleValue(998),       // 设置最大阈值
                         "MinTh", DoubleValue(990),      // 设置最小阈值
                         "MeanPktSize", UintegerValue(1024));
    tch.Install(devices);

    //the same ortbit 
    for(uint32_t i = 0; i < orbit_num; i++)
    {
        for(uint32_t j = 0; j < sate_num; j++)
        {
            int current_index = i*sate_num + j;
            NodeContainer intra_container;
            if(j < sate_num - 1)
            {
                int next_index = current_index + 1;
                intra_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            else
            {
                int next_index = i*sate_num;
                intra_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            NetDeviceContainer intra_device = link.Install(intra_container);
            devices.Add(intra_device);
            // // Install Internet stack
            // // InternetStackHelper intra_stack;
            // // intra_stack.Install(intra_container);
            // //IP address
            // string sat_address1 = "10."+to_string(i+1)+"."+to_string(j+1)+"."+"0";//port 1
            // Ipv4AddressHelper intra_ipv4;
            // intra_ipv4.SetBase(Ipv4Address(sat_address1.c_str()), "255.255.255.0", "0.0.0.1");
            // Ipv4InterfaceContainer intra_interfaces = intra_ipv4.Assign(intra_device);
            // interfaces.Add(intra_interfaces); // first--the same orbit
        }
    }
    // the different orbit
    for(uint32_t i = 0; i < orbit_num; i++)
    {
        for(uint32_t j = 0; j < sate_num; j++)
        {
            int current_index = i*sate_num + j;
            NodeContainer inter_container;
            if(i < orbit_num - 1)
            {
                int next_index = (i+1)*sate_num + j;
                inter_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            else
            {
                int next_index = j;
                inter_container = NodeContainer(total_node.Get(current_index), total_node.Get(next_index));
            }
            NetDeviceContainer inter_device = link.Install(inter_container);
            devices.Add(inter_device);
            // // Install Internet stack
            // // InternetStackHelper inter_stack;
            // // inter_stack.Install(inter_container);
            // //IP address
            // string sat_address2 = "10."+to_string((i+4)%(orbit_num+4)+4)+"."+to_string(j+1)+"."+"0";//port 2
            // Ipv4AddressHelper inter_ipv4;
            // inter_ipv4.SetBase(Ipv4Address(sat_address2.c_str()), "255.255.255.0", "0.0.0.4");
            // Ipv4InterfaceContainer inter_interfaces = inter_ipv4.Assign(inter_device);
            // interfaces.Add(inter_interfaces); // second--the different orbbit
        }
    }

    // 普通卫星设置ip
    for(uint32_t i = 0; i < orbit_num; ++i){
      Ipv4AddressHelper sipv4Helper;
      std::string str = "10." + std::to_string(i+4);
      for(uint32_t j=0; j < sate_num; j++){
        std::string temp = str + "." + std::to_string(j+1) + ".0";
        Ipv4Address addr (temp.c_str ());
        sipv4Helper.SetBase(addr, "255.255.255.0");
        int current_index = i*sate_num + j;
        uint32_t size = total_node.Get(current_index)->GetNDevices();
        for(uint32_t k=0; k < size; k++){
          sipv4Helper.Assign(total_node.Get(current_index)->GetDevice(k));
        }
      }
    }
    // AsciiTraceHelper ascii;
    // link.EnableAsciiAll(ascii.CreateFileStream("examples/sdn-controller/output/tcp-flow.txt"));

    // 星间可见性       
    if(topo == 1) LinkChange(total_node,"examples/sdn-controller/Sat1(100s)_linkchange.csv");
    else if(topo == 2) LinkChange(total_node,"examples/sdn-controller/Sat2(100s)_linkchange.csv");


    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // 打印所有节点的路由表
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("examples/sdn-controller/output/ospf-routing-tables.txt");
    Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (6), stream, Time::S);

    time_t true_start = time(nullptr);
    //构建并启动应用
    buildApp(total_node,load);

    // change the satellite node's link utilization --初始化分簇部分
    std::vector<Ptr<LinkUtilizationMonitor>> monitors;
    for (uint32_t i = 0; i < total_node.GetN(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
        monitor->m_totalDevice.clear();
        monitor->SetLinkCapacity(linkBandwidth);
        monitor->SetStopTime(totalTimeStep);
        monitor->StartMonitoring(total_node.Get(i));
        monitors.push_back(monitor);
        // // 设置定时器，每秒钟检查一次最大利用率
        // Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, monitor);
    }
    // // // 安排 UseMaxUtilization 函数第一次被调度
    // //sat.DynamicCluster(monitors, total_node, adj);
    //  Simulator::Schedule(Seconds(1), &UseMaxUtilization, monitors, total_node, sat, adj);
    // Simulator::Schedule(Seconds(1.0), &OutputNodeInfo, monitors);
    OutputNodeInfo(monitors);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowmon;
    flowmon = flowmonHelper.InstallAll();



    Simulator::Stop (Seconds (totalTimeStep)); // 设置仿真停止时间
    Simulator::Run ();

    double Throughput = 0;
    uint32_t lostPackets = 0;
    uint32_t RxPackets = 0;
    uint32_t TxPackets = 0;
    double Delay_time = 0;
    double start_time = 0.0;//需要根据自定义的流量传输开始时间进行定义（需要根据实际情况进行更改）
    double end_time = 0.0;//初始化为start_time一样的数据
    double Latency = 0;
    double AverageDelayTime = 0.0;
    double SumDelayTime = 0;
    double SumJitterTime = 0;
    double Jitter = 0;

    std::vector<uint64_t> txBytes;
    std::vector<double> linkUtilizations;
    //-------------------------卫星网络时延、吞吐以及丢包统计-------------------------------------//
    flowmon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowmon->GetFlowStats();
    Ipv4FlowClassifier::FiveTuple t;
    for (auto i = stats.begin(); i != stats.end(); ++i)
    {        
        t = classifier->FindFlow(i->first);
        // std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
        //             << t.destinationAddress << ")\n";
        TxPackets += i->second.txPackets;
        // TxOffered += i->second.txBytes * 8.0 / totalTimeStep / 1000 / 1000;
        RxPackets += i->second.rxPackets;
        Throughput += i->second.rxBytes * 8.0 / (totalTimeStep-10.0) / 1000 / 1000;
        lostPackets += i->second.lostPackets;
        SumDelayTime += i->second.delaySum.GetSeconds();
        SumJitterTime += i->second.jitterSum.GetSeconds();
    }

    end_time = stats.rbegin()->second.timeLastRxPacket.GetSeconds();
    Delay_time = end_time - start_time;
    AverageDelayTime = Delay_time / (RxPackets * 1.0);
    Latency = SumDelayTime / double(RxPackets);
    Jitter = SumJitterTime / double(RxPackets);
    std::cout << "--------------Performance--------------" << std::endl;
    cout<<endl;
                //  "  TxOffered: "<< TxOffered << " Mbps\n"
    cout<<"-------------1.时延,抖动，吞吐，丢包-------------"<<endl;
    std::cout << "  Tx Packets: " << TxPackets << " p\n"
                 "  Rx Packets: " << RxPackets << " p\n"
                 "  lostPackets: " << lostPackets << " p\n"
                 "  Latency: " << Latency*1000 << " ms\n"//全网
                 "  AverageDelayTime:" << AverageDelayTime*1000 <<"ms\n" //单个数据包
                 "  Jitter: " << Jitter*1000 << " ms\n"
                 "  Throughput: "<< Throughput/1000 << " Gbps\n"
                 "  Loss Packet Ratio: " << (double)lostPackets * 100 / TxPackets << " %\n";
    cout<<endl;
    cout<<"--------------2.平均链路利用率,最大链路利用率-----------"<<endl;
    // 仿真时间（秒）
    double duration = Simulator::Now().GetSeconds();
    cout<<"仿真时间: "<<duration<<endl;
    //-------------双端口测试-------------//
    // for (uint32_t i = 0; i < monitors.size(); ++i) 
    // {
    //     Ptr<LinkUtilizationMonitor> monitor1 = monitors[i];
    //     for(auto & t : m_link)
    //     {
    //         for(uint32_t j = 0; j < monitors.size(); ++j)
    //         {
    //             if(i != j)
    //             {
    //                 Ptr<LinkUtilizationMonitor> monitor2 = monitors[j];
    //                 if(t.)
    //             }
    //         }
    //     }
    // }
    //-------------单端口测试-------------//
    for (uint32_t i = 0; i < monitors.size(); ++i) 
    {
        Ptr<LinkUtilizationMonitor> monitor = monitors[i];
        // 获取链路上发送的字节数
        for(auto& j : monitor->m_totalDevice)
        {
            txBytes.push_back(j.second); //单位B
        }
    }
    for(uint32_t i = 0; i < txBytes.size(); i++)
    {
        // 链路利用率
        linkUtilizations.push_back(((txBytes[i]*8.0) / duration) / double(linkBandwidth));
    }
    // 计算平均和最大链路利用率
    double sumUtilization = std::accumulate(linkUtilizations.begin(), linkUtilizations.end(), 0.0);
    double avgUtilization = sumUtilization / linkUtilizations.size();
    double maxUtilization = *std::max_element(linkUtilizations.begin(), linkUtilizations.end());

    std::cout << "Average Link Utilization: " << avgUtilization * 100.0 << "%" << std::endl;
    std::cout << "Max Link Utilization: " << maxUtilization * 100.0 << "%" << std::endl;
    cout<<endl;
    time_t true_end = time(nullptr);
    std::cout << "仿真时间：" << true_end - true_start << std::endl;

    Simulator::Destroy();
    }

    // monitor->CheckForLostPackets();
    // Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    // FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    // for (auto i = stats.begin(); i != stats.end(); ++i)
    // {
    //     Throughput += i->second.rxBytes * 8.0 / simulationEndTime.GetSeconds() / 1000000000; //Mbps
    //     lostPackets += i->second.lostPackets;
    //     SumDelayTime += i->second.delaySum.GetSeconds();
    //     RxPackets += i->second.rxPackets;
    // }

    // end_time = stats.rbegin()->second.timeLastRxPacket.GetSeconds();
    // Delay_time = end_time - start_time;
    // AverageDelayTime = Delay_time / (RxPackets * 1.0);
    // std::cout<<" Throughput: "<< Throughput << " Mbps\n"
    //            " lostPackets:" << lostPackets << " p\n"
    //            " Delay_time:" << Delay_time << " s\n"
    //            " RxPackets:" << RxPackets << " s\n"
    //            " AverageDelayTime:" << AverageDelayTime << " s\n"
    //            " DelayTime:" << (SumDelayTime / (RxPackets * 1.0))/1000 << " ms\n"
    //            " 丢包率:" << lostPackets / ((lostPackets + RxPackets) * 1.0) << " %\n";
    // Simulator::Destroy();
    // time_t true_end = time(nullptr);
    // std::cout << "仿真时间：" << true_end - true_start << std::endl;
    return 0;
}