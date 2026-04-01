#include "satrouting.h"
#include "para.h"
#include "topo.h"
#include <cstdint>
#include <ns3/net-device.h>
#include <ns3/node-container.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("satrouting");

SatRouting::SatRouting()
{
}

void
SatRouting::UpdateClusterLinkUtilization(uint32_t j)
{
    int i = 0;
    double current_utilization = 0.0;
    double current_averutilization = 0.0;
    // std::cout << "m_cluster_size:" << m_cluster.size() << "\tj:" << j <<std::endl;
    if(m_cluster.size() <= j) return ;
    for(auto &entry: m_cluster[j])
    {
        // current_utilization += (entry.second * 8.0) / linkBandwidth;
        current_utilization += entry.second;
        i++;
    }
    current_averutilization = current_utilization / double(i);
    m_averButilization[j] = current_averutilization;
}

void
SatRouting::UpdateClusterLoadDIndex(uint32_t j)
{
    double i = 0;
    double linkPacketByte_Sum = 0.0;
    double linkPacketByte_Sqr = 0.0;
    double loadDIndex = 0.0;

    uint16_t byte = 1, kBytes = byte << 10;
    if(m_cluster.size() <= j) return ;
    for(auto &entry: m_cluster[j])
    {
        // linkPacketByte_Sum += (entry.second / kBytes);
        // linkPacketByte_Sqr += ((entry.second / kBytes) * (entry.second / kBytes));
        linkPacketByte_Sum += (entry.second * kBytes / 8.0);
        linkPacketByte_Sqr += ((entry.second * kBytes / 8.0) * (entry.second * kBytes / 8.0));
        i += 1;
    }
    loadDIndex = (linkPacketByte_Sum * linkPacketByte_Sum) / (i * linkPacketByte_Sqr);

    m_cluster[j].clear();

    m_loadDIndex[j] = loadDIndex;
}

void
SatRouting::UpdateClusterLinkUtilizationSim(NodeContainer& cluster, uint32_t index)
{
    int i = 0;
    double current_utilization = 0.0;
    double current_averutilization = 0.0;

    for (uint32_t j = 0; j < cluster.GetN(); ++j)
    {
        Ptr<Node> m_node = cluster.Get(j);    
        for (uint32_t k = 1; k < m_node->GetNDevices(); ++k)
        {
            if(k >= 5) break;
            Ptr<NetDevice> device = m_node->GetDevice(k);
            Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(device);
            if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//判断链路是否存在
            {
                DataRateValue dataLoadValue, dataRateValue;
                p2p_dev->GetAttribute("DataLoad", dataLoadValue);
                p2p_dev->GetAttribute("DataRate", dataRateValue);
                DataRate dataLoad = dataLoadValue.Get();
                uint32_t dataLoadValueUint = dataLoad.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
                DataRate dataRate = dataRateValue.Get();
                uint32_t dataRateValueUint = dataRate.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
                current_utilization += ((double)dataLoadValueUint / (double)dataRateValueUint);
                i++;
            }
        }
    }
    current_averutilization = current_utilization / double(i);
    m_averButilization[index] = current_averutilization;
}

void
SatRouting::UpdateClusterLoadDIndexSim(NodeContainer& cluster, uint32_t index)
{
    double i = 0;
    double linkPacketByte_Sum = 0.0;
    double linkPacketByte_Sqr = 0.0;
    double loadDIndex = 0.0;
    double current_utilization = 0.0;
    uint16_t byte = 1, kBytes = byte << 10;

    for (uint32_t j = 0; j < cluster.GetN(); ++j)
    {
        Ptr<Node> m_node = cluster.Get(j);  
        for (uint32_t k = 1; k < m_node->GetNDevices(); ++k)
        {
            if(k >= 5) break;
            Ptr<NetDevice> device = m_node->GetDevice(k);
            Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(device);
            if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//判断链路是否存在
            {
                DataRateValue dataLoadValue, dataRateValue;
                p2p_dev->GetAttribute("DataLoad", dataLoadValue);
                p2p_dev->GetAttribute("DataRate", dataRateValue);
                DataRate dataLoad = dataLoadValue.Get();
                uint32_t dataLoadValueUint = dataLoad.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
                DataRate dataRate = dataRateValue.Get();
                uint32_t dataRateValueUint = dataRate.GetBitRate() / 1000; // 转换为 uint32_t, 单位为Kbps
                current_utilization = ((double)dataLoadValueUint / (double)dataRateValueUint) * kBytes / 8.0;

                linkPacketByte_Sum += current_utilization;
                linkPacketByte_Sqr += (current_utilization * current_utilization);
                i++;
            }
        }
    }    
    loadDIndex = (linkPacketByte_Sum * linkPacketByte_Sum) / (i * linkPacketByte_Sqr);
    m_loadDIndex[index] = loadDIndex;
}

/**
 * 计算节点群的直径
 * @param _clusterI 节点群,例如某个簇
 * @brief 节点对最短路径的最大值
 */
void
SatRouting::UpdateClusterDiameter(NodeContainer& _clusterI) 
{
    uint32_t ClusterID = _clusterI.Get(0)->m_ClusterNumber;  // 簇ID
    uint32_t nodeNum = _clusterI.GetN();                    // 簇内节点数目
    int d[nodeNum][nodeNum];                                // 邻接矩阵

    std::memset(d, 0x3f, sizeof(d));                        // 初始化邻接矩阵：表示极大值，即无穷大
    for(uint32_t i=0; i<nodeNum; i++)
    {
        Ptr<Node> node1 = _clusterI.Get(i);
        for(uint32_t j=0; j<nodeNum; j++)
        {
            Ptr<Node> node2 = _clusterI.Get(j);
            if (i==j)   d[i][j]=0;
            else if (IsConnect(node1, node2, 0))
            {                                   // 若节点相邻，则赋值1
                d[i][j] = 1;
                // std::cout << "d[" << i << "][" << j << "]: " << d[i][j] << std::endl; 
            }
        }
    }
 
    // Floyd-Warshall Algorithm
    for (uint32_t k = 0; k < nodeNum; ++k)
        for (uint32_t i = 0; i < nodeNum; ++i)
            for (uint32_t j = 0; j < nodeNum; ++j)
                d[i][j] = std::min(d[i][j], d[i][k] + d[k][j]);     // 更新邻接矩阵：若可达，则数值变为最短路距离
 
    // 计算直径
    int diameter = 0;
    for (uint32_t i = 0; i < nodeNum; ++i)
        for (uint32_t j = 0; j < nodeNum; ++j)
            diameter = std::max(diameter, d[i][j]);
    
    m_clusterDiameter[ClusterID] = diameter;
    // std::cout << "diameter c-" << ClusterID << "-" << diameter << std::endl;
}

void
SatRouting::UpdateClusterNetworkInfo(NodeContainer& clusters)
{                
    m_clusterNetworkInf.clear();
    
    std::ofstream outFile1;
    // boundarySatIPAddress, clusterLinkUtilization, clusterDiameter, clusterLoadDIndex;
    // std::cout << "SatRouting::UpdateClusterNetworkInfo(): test1" << std::endl;

    outFile1.open("clusterNetworkInf.csv", std::ios::out);

    for (uint32_t i = 0; i < clusters.GetN(); i++)
    {
        Ptr<Node> clusterNode = clusters.Get(i);
        Ptr<NetDevice> device;
        Ptr<PointToPointNetDevice> devices;
        uint8_t buf[4];
        for(uint32_t deviceId = 0; deviceId < clusterNode->GetNDevices(); ++deviceId)
        {
            device = clusterNode->GetDevice(deviceId);           
            devices = DynamicCast<PointToPointNetDevice>(device);
            if (devices == nullptr)
            {
                NS_LOG_ERROR("Device cast failed for device " << devices);
                continue;
            }
            uint32_t p2pIF = devices->GetIfIndex();
            Ptr<Node> node = devices->GetNode();
            Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
            if(ippp->GetNAddresses(p2pIF) == 0) continue;
            Ipv4Address dIPAddress = ippp->GetAddress(p2pIF, 0).GetLocal();

            Ptr<Channel> channel = device->GetChannel ();
            // 获取通道中连接的所有设备
            Ptr<NetDevice> otherDevice;
            if (channel->GetNDevices () == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == device) 
                {
                    otherDevice = channel->GetDevice (1);
                } 
                else 
                {
                    otherDevice = channel->GetDevice (0);
                }
            }
            // 获取与给定设备相连的第一个设备所在的节点及节点ID
            Ptr<Node> connectedNode = otherDevice->GetNode ();
            uint32_t neiId = connectedNode->GetId () - sates_num - gwsnum - mcsnum;     // !< 检查抽象节点ID
            NS_LOG_LOGIC ("Get neighbor Id: " << neiId);
            // std::cout << "Get neighbor Id: " << neiId << std::endl;

            // 为了防止多线程测试重复修改同一文件，更改信息传输从.csv到map
            vector<double>  tmpW;
            tmpW.push_back(m_averButilization[neiId]);
            tmpW.push_back(m_clusterDiameter[neiId]);
            tmpW.push_back(m_loadDIndex[neiId]);

            // m_clusterNetworkInf.insert(std::pair<ns3::Ipv4Address, std::vector<double>>(dIPAddress, tmp));
            m_clusterNetworkInf[dIPAddress] = tmpW;

            dIPAddress.Serialize(buf);
            std::string tmp = (std::to_string(buf[0]) + "." 
                        + std::to_string(buf[1]) + "." 
                        + std::to_string(buf[2]) + "." 
                        +std::to_string(buf[3])).c_str();
            outFile1 << tmp << ',' << std::to_string(m_averButilization[neiId]) << ',' << std::to_string(m_clusterDiameter[neiId]) << ',' << std::to_string(m_loadDIndex[neiId]) << ',' << std::to_string(i) << ',' << std::to_string(neiId) << std::endl;
        }           
    }
    outFile1.close();
    // std::cout << "SatRouting::UpdateClusterNetworkInfo(): test2" << std::endl;
}

void
SatRouting::CommandSetup(int argc, char** argv)
{
    CommandLine cmd(__FILE__);
    cmd.AddValue("CSVfileName", "The name of the CSV output file name", m_CSVfileName);
    cmd.AddValue("traceMobility", "Enable mobility tracing", m_traceMobility);
    cmd.AddValue("protocol", "Routing protocol (OLSR, AODV, DSDV, DSR)", m_protocolName);
    cmd.AddValue("flowMonitor", "enable FlowMonitor", m_flowMonitor);
    cmd.Parse(argc, argv);

    std::vector<std::string> allowedProtocols{"OLSR", "AODV", "DSDV", "DSR"};

    if (std::find(std::begin(allowedProtocols), std::end(allowedProtocols), m_protocolName) ==
        std::end(allowedProtocols))
    {
        NS_FATAL_ERROR("No such protocol:" << m_protocolName);
    }
    
# ifdef __rouECMP
    Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting",BooleanValue(true));
# endif
}

//获取某节点的ip地址
Ipv4Address 
SatRouting::GetIPAddress(Ptr<ns3::Node> node)
{
  Ptr<Ipv4> ippp = node-> GetObject<Ipv4> ();
  //获得第1个接口的第0个地址--多数为第一个接口，第0个接口是本机127.0.0.1
  Ipv4Address ipaddress = ippp->GetAddress(1,0).GetLocal();
  return ipaddress;
}

// 通过设备IP找到对应节点
Ptr<Node>
SatRouting::GetNodefromIP(Ipv4Address dIPAddress)
{
    Ptr<Node> n = CreateObject<Node>();
    for (auto i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
        Ptr<Node> node = *i;
        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        uint32_t interfacenumber = ippp->GetNInterfaces();                      // 得到该节点的接口数目           
        for (uint32_t iterate = 1; iterate < interfacenumber; iterate++)        // 遍历该节点的每一个接口
        {
            if(ippp->GetNAddresses(iterate) == 0) continue;
            Ipv4Address ipaddress = ippp->GetAddress(iterate, 0).GetLocal();    // 获取到某节点的接口IP地址            
            if (ipaddress == dIPAddress) 
            {
                n = node;       // 将获取的所有接口IP与目的节点IP比较, 若相同则返回节点  
                return n;       
            }    
        }
    }
    return nullptr;
}

//寻找周围邻居节点
NodeContainer 
SatRouting::FindNeibors(Ptr<Node> node, bool mode)
{
    NodeContainer neighbor;
    uint32_t end;
    if(mode) 
        end = node->GetNDevices() - 1;
    else 
        end = ISLNum < node->GetNDevices() ? ISLNum : node->GetNDevices() - 1;

    for(uint32_t i = 0; i <= end; ++i)
    {
        Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(i));

        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        if(ippp->GetNAddresses(i) == 0) continue;

        if(p2pNetDevice != nullptr)
        {
            if(!p2pNetDevice->IsSatLinkUp())   continue;
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
            // 获取与给定设备相连的第一个设备所在的节点
            Ptr<Node> connectedNode = otherDevice->GetNode ();
            // 输出结果
            neighbor.Add(connectedNode);
            //std::cout << "与给定设备相连的第一个设备所在的节点ID: " << connectedNode->GetId () << std::endl;
        }
    }
    return neighbor;
}

//判断是否连接
bool 
SatRouting::IsConnect(Ptr<Node> node1, Ptr<Node> node2, bool mode)
{
    NodeContainer neighbor = FindNeibors(node1, mode);
    for(uint32_t i = 0; i < neighbor.GetN(); ++i)
    {
        if(neighbor.Get(i)->GetId() == node2->GetId()) return true;
    }
    return false;
}

std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>>
SatRouting::GetDevicesFromNodes(Ptr<Node> node1, Ptr<Node> node2)
{
    Ptr<PointToPointNetDevice> p2pNetDevice;
    Ptr<PointToPointNetDevice> p2pNetDevice2;
    for(uint32_t i = 1; i < node1->GetNDevices(); ++i)
    {
        if(i >= 5) break;
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
SatRouting::UpdateClusterInfo(NodeContainer& _satellite, 
                              std::vector<NodeContainer>& _clusters, 
                              NodeContainer& _AbstractClusterNodes, 
                              std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>>& _BoundaryNode, 
                              const vector<Ptr<LinkUtilizationMonitor>> &monitors)
                              
{
    //------------------1-为所有卫星节点设置簇ID（0 ~ clusternum-1）------------------//
    uint32_t SatNum = _satellite.GetN();                 // 获取卫星个数
    // uint32_t ClusterNum = _clusters.size();
    for (int i = 0; i < int(SatNum); ++i)
    {
        _satellite.Get(i)->m_ClusterNumber = -1;         // 初始化所有卫星簇ID，预防故障节点
    }

    for(uint32_t i = 0; i < _clusters.size(); ++i)
    {
        for (int j = 0; j < int(_clusters[i].GetN()); ++j)
        {
            _clusters[i].Get(j)->m_ClusterNumber = i;   // ！< node.h 添加属性 m_ClusterNumber
        }
    }
    //------------------2-初始化抽象簇节点------------------//
    uint32_t ClusterNum = _clusters.size();
    for (int i = 0; i < int(_AbstractClusterNodes.GetN()); ++i)
    {
        _AbstractClusterNodes.Get(i)->m_ClusterNumber = -1;    // 初始化簇ID
    }
    //------------------3-抽象簇节点拓扑连接+初始化边界节点------------------//
    static uint8_t RouStep = 0;
    RouStep++;
    
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate("10Gbps")));
    p2p.SetChannelAttribute ("Delay", TimeValue(NanoSeconds(6560)));
    int ch4 = 1;
    uint32_t faultClusterNum = (uint32_t)-1;
    for (int i = 0; i < int(_satellite.GetN()); ++i)
    {
        uint32_t myclusterNum =  _satellite.Get(i)->m_ClusterNumber;
        if (myclusterNum == faultClusterNum) continue;
        NodeContainer neighbor = FindNeibors(_satellite.Get(i), 0);
        for (int j = 0; j < int(neighbor.GetN()); ++j)
        {
            if(neighbor.Get(j)->GetId() < _satellite.Get(0)->GetId())
            {
                continue;
            }
            uint32_t nei_clusterNum = neighbor.Get(j)->m_ClusterNumber; 
            if ((nei_clusterNum == myclusterNum) || (nei_clusterNum == faultClusterNum)) continue;
            else
            {
                // std::cout << "_AbstractClusterNodes size:" << _AbstractClusterNodes.GetN() << " myclusterNum:" << myclusterNum << " nei_clusterNum:" << nei_clusterNum << std::endl;
                if (IsConnect(_AbstractClusterNodes.Get(myclusterNum), _AbstractClusterNodes.Get(nei_clusterNum), 1)) 
                {
                    _BoundaryNode[myclusterNum][nei_clusterNum].push_back({_satellite.Get(i)->GetId(), neighbor.Get(j)->GetId()});
                }
                else
                {
                    // std::cout << " myclusterNum:" << myclusterNum << " nei_clusterNum:" << nei_clusterNum << " satelliteID: " << _satellite.Get(i)->GetId() << " " << neighbor.Get(j)->GetId() << std::endl;
                    _BoundaryNode[myclusterNum][nei_clusterNum].push_back({_satellite.Get(i)->GetId(), neighbor.Get(j)->GetId()});
                    NodeContainer Abstnode = NodeContainer(_AbstractClusterNodes.Get(myclusterNum), _AbstractClusterNodes.Get(nei_clusterNum));
                    NetDeviceContainer devices = p2p.Install(Abstnode);
                    std::string addr1 = "20.";
                    std::string addr2 = ".0";
                    std::string sat_address = addr1 + std::to_string(RouStep) + "." + std::to_string(ch4) + addr2;
                    Ipv4AddressHelper ipv4;
                    ipv4.SetBase(ns3::Ipv4Address(sat_address.c_str()), "255.255.255.0");
                    ipv4.Assign(devices);
                    ch4++;
                }
            }
        }
    }

    // for (int i = 0; i < int(_AbstractClusterNodes.GetN()); ++i)
    // {
    //     Ptr<Node> node = _AbstractClusterNodes.Get(i);
    //     Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
    //     if(!ippp) continue;
    //     uint32_t interfacenumber = ippp->GetNInterfaces();      // 得到该节点的接口数目
    //     std::cout << "interfaceNum: " << interfacenumber; 
       
    //     for (uint32_t iterate = 1; iterate < interfacenumber; iterate++)     // 遍历该节点的每一个接口
    //     {
    //         if(ippp->GetNAddresses(iterate) == 0) continue;
    //         Ipv4Address ipaddress = ippp->GetAddress(iterate, 0).GetLocal();    // 获取到某节点的接口IP地址
    //         std::cout << "    ipaddress " << iterate << ": " << ipaddress;
    //     }
    //     std::cout << std::endl;
    // }        
    //------------------4-计算图节点/链路权重------------------//
    m_cluster.resize(ClusterNum);

    for (auto monitor : monitors) 
    {
        Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateEachLinkUtilization, monitor);
    }
    OutputTxPInfo(monitors);
    
    // 簇间路由节点权重：平均链路带宽利用率、簇直径、簇平均负载分布
    m_averButilization.resize(ClusterNum, 1.0);
    m_loadDIndex.resize(ClusterNum, 0.0);
    m_clusterDiameter.resize(ClusterNum, 100.0);
    for(uint32_t j = 0; j < _clusters.size(); ++j)
    {
        UpdateClusterLinkUtilization(j);
        UpdateClusterLoadDIndex(j);
        UpdateClusterDiameter(_clusters[j]);
    }
        UpdateClusterNetworkInfo(_AbstractClusterNodes);
    }
}

void
SatRouting::UpdateClusterInfoSim(NodeContainer& _satellite, 
                              std::vector<NodeContainer>& _clusters, 
                              NodeContainer& _AbstractClusterNodes, 
                              std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>>& _BoundaryNode)
                              
{
    //------------------1-为所有卫星节点设置簇ID（0 ~ clusternum-1）------------------//
    uint32_t SatNum = _satellite.GetN();                 // 获取卫星个数
    // uint32_t ClusterNum = _clusters.size();
    for (int i = 0; i < int(SatNum); ++i)
    {
        _satellite.Get(i)->m_ClusterNumber = -1;         // 初始化所有卫星簇ID，预防故障节点
    }

    for(uint32_t i = 0; i < _clusters.size(); ++i)
    {
        for (int j = 0; j < int(_clusters[i].GetN()); ++j)
        {
            _clusters[i].Get(j)->m_ClusterNumber = i;   // ！< node.h 添加属性 m_ClusterNumber
        }
    }
    //------------------2-初始化抽象簇节点------------------//
    uint32_t ClusterNum = _clusters.size();
    for (int i = 0; i < int(_AbstractClusterNodes.GetN()); ++i)
    {
        _AbstractClusterNodes.Get(i)->m_ClusterNumber = -1;    // 初始化簇ID
    }
    //------------------3-抽象簇节点拓扑连接+初始化边界节点------------------//
    static uint8_t RouStepSim = 0;
    static uint8_t AddressSim = 30;
    if(RouStepSim == 255)
    {
        AddressSim++;
        RouStepSim = 0;
    }
    RouStepSim++;
    
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate("10Gbps")));
    p2p.SetChannelAttribute ("Delay", TimeValue(NanoSeconds(6560)));
    int ch4 = 1;
    uint32_t faultClusterNum = (uint32_t)-1;
    for (int i = 0; i < int(_satellite.GetN()); ++i)
    {
        uint32_t myclusterNum =  _satellite.Get(i)->m_ClusterNumber;
        if (myclusterNum == faultClusterNum) continue;
        NodeContainer neighbor = FindNeibors(_satellite.Get(i), 0);
        for (int j = 0; j < int(neighbor.GetN()); ++j)
        {
            if(neighbor.Get(j)->GetId() < _satellite.Get(0)->GetId())
            {
                continue;
            }
            uint32_t nei_clusterNum = neighbor.Get(j)->m_ClusterNumber; 
            if ((nei_clusterNum == myclusterNum) || (nei_clusterNum == faultClusterNum)) continue;
            else
            {
                // std::cout << "_AbstractClusterNodes size:" << _AbstractClusterNodes.GetN() << " myclusterNum:" << myclusterNum << " nei_clusterNum:" << nei_clusterNum << std::endl;
                if (IsConnect(_AbstractClusterNodes.Get(myclusterNum), _AbstractClusterNodes.Get(nei_clusterNum), 1)) 
                {
                    _BoundaryNode[myclusterNum][nei_clusterNum].push_back({_satellite.Get(i)->GetId(), neighbor.Get(j)->GetId()});
                }
                else
                {
                    // std::cout << " myclusterNum:" << myclusterNum << " nei_clusterNum:" << nei_clusterNum << " satelliteID: " << _satellite.Get(i)->GetId() << " " << neighbor.Get(j)->GetId() << std::endl;
                    _BoundaryNode[myclusterNum][nei_clusterNum].push_back({_satellite.Get(i)->GetId(), neighbor.Get(j)->GetId()});
                    NodeContainer Abstnode = NodeContainer(_AbstractClusterNodes.Get(myclusterNum), _AbstractClusterNodes.Get(nei_clusterNum));
                    NetDeviceContainer devices = p2p.Install(Abstnode);
                    std::string addr1 = std::to_string(AddressSim);
                    std::string addr2 = ".0";
                    std::string sat_address = addr1 + "." + std::to_string(RouStepSim) + "." + std::to_string(ch4) + addr2;
                    Ipv4AddressHelper ipv4;
                    ipv4.SetBase(ns3::Ipv4Address(sat_address.c_str()), "255.255.255.0");
                    ipv4.Assign(devices);
                    ch4++;
                }
            }
        }
    }     
    //------------------4-计算图节点/链路权重------------------//
    m_cluster.resize(ClusterNum);
    
    // 簇间路由节点权重：平均链路带宽利用率、簇直径、簇平均负载分布
    m_averButilization.resize(ClusterNum, 1.0);
    m_loadDIndex.resize(ClusterNum, 0.0);
    m_clusterDiameter.resize(ClusterNum, 100.0);
    for(uint32_t j = 0; j < _clusters.size(); ++j)
    {
        UpdateClusterLinkUtilizationSim(_clusters[j], j);
        UpdateClusterLoadDIndexSim(_clusters[j], j);
        UpdateClusterDiameter(_clusters[j]);
    }
    UpdateClusterNetworkInfo(_AbstractClusterNodes);
}


void 
SatRouting::OutputTxPInfo(const vector<Ptr<LinkUtilizationMonitor>> &monitors)
{   
    double tmp;
    // m_RlinkUtilization.clear();// test12 该句导致链路利用率为空（该执行顺序先于link-utilization.cc赋值）

    std::ofstream outFile;
    outFile.open("RlinkUtilization.csv", std::ios::app);

    for(uint32_t i = 0; i < monitors.size(); ++i)
    {
        Ptr<LinkUtilizationMonitor> monitor = monitors[i];

        for(auto &entry: monitor->eachD_utilization)        // !< 引用link-utilization.h
        {        
            Ipv4Address dIP = entry.first;
            uint8_t buf[4];
            dIP.Serialize(buf);

            // 为了防止多线程测试重复修改同一文件，更改信息传输从.csv到map
            tmp = entry.second;
            m_RlinkUtilization[dIP] = tmp;
            // std::cout << "Time: " << Simulator::Now() << ", dIP: " << dIP << ", linkUtilization: " << tmp << std::endl;

            std::string tmp = (std::to_string(buf[0]) + "." 
                        + std::to_string(buf[1]) + "." 
                        + std::to_string(buf[2]) + "." 
                        +std::to_string(buf[3])).c_str();
            outFile << tmp << ',' << std::to_string(entry.second) << std::endl;
            /* 每一行：deviceIPAddress， linkutilization */
        }
    }
    outFile.close();    

    for(uint32_t i = 0; i < monitors.size(); ++i)
    {
        Ptr<LinkUtilizationMonitor> monitor = monitors[i];

        std::map<ns3::Ipv4Address, double>::iterator it;
        for (it = monitor->eachD_utilization.begin(); it != monitor->eachD_utilization.end(); it++) 
        {
            Ipv4Address dIP = it->first;
            Ptr<Node> node = GetNodefromIP(dIP);
            uint32_t nodeNumtmp = node->m_ClusterNumber;
            if(nodeNumtmp == (uint32_t)-1)  continue;
            m_cluster[nodeNumtmp][dIP] = it->second;
        }
    }

    Simulator::Schedule(Seconds(1.0), &SatRouting::OutputTxPInfo, this, monitors);
}

void
SatRouting::InitialSatRouter(NodeContainer&Gnodes, NodeContainer& totalsates, std::vector<NodeContainer>& clusters, const vector<Ptr<LinkUtilizationMonitor>> &monitors, bool consType, uint32_t orbitNums, uint32_t satPerOrbits)
{
    uint32_t ClusterNum = clusters.size();// 获取簇个数               
    AbstractClusterNodes.Create(ClusterNum);
    AbstractClusterNodesBackup.Create(ClusterNum);
    InternetStackHelper internet;
    internet.Install(AbstractClusterNodes);
    internet.Install(AbstractClusterNodesBackup);

    RouCmpTimes = 0;
    total_duration_second = 0.0;
    total_intraCduration_second = 0.0;
    total_interCduration_second = 0.0;

    std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode(ClusterNum, std::vector<std::vector<std::pair<uint32_t, uint32_t>>>(ClusterNum));

    // 获取开始时间
    auto start = std::chrono::steady_clock::now();

    // 分簇后更新簇结构信息
    UpdateClusterInfo(totalsates, clusters, AbstractClusterNodes, BoundaryNode, monitors);
    auto UpdateClusterInfoCend = std::chrono::steady_clock::now();

    // 计算簇内路由表
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteGlobalRoutes();
    AddIntraCRouter(clusters, BoundaryNode);
    auto intraCend = std::chrono::steady_clock::now();

    // 计算簇间路由表
    AddInterCRouter(AbstractClusterNodes, BoundaryNode);
    auto interCend = std::chrono::steady_clock::now();

    // // 计算地面网络和卫星网络之间的路由表
    // AddGroundCRouter(Gnodes, BoundaryNode);

    // 协同簇内簇间路由
    CoordinateIntraInterCRouter(clusters, totalsates, AbstractClusterNodes, BoundaryNode, consType, orbitNums, satPerOrbits);
    
    // 获取结束时间
    auto end = std::chrono::steady_clock::now();

    // 添加簇内备份路由
    AddIntraCBackupRouter(clusters, totalsates, consType, orbitNums, satPerOrbits);
 
    // 计算并输出函数执行时间
    double duration_second = std::chrono::duration<double>(end - start).count();
    double intraCduration_second = std::chrono::duration<double>(intraCend - UpdateClusterInfoCend).count() / (double)ClusterNum;
    double interCduration_second = std::chrono::duration<double>(interCend - intraCend).count() + std::chrono::duration<double>(UpdateClusterInfoCend - start).count() + std::chrono::duration<double>(end - interCend).count() / (double)ClusterNum;
    std::cout << "路由计算时间： " << duration_second << "秒, " << "簇内： " << intraCduration_second << "秒, 簇间： " << interCduration_second << "秒" << std::endl;
    // double duration_millisecond = std::chrono::duration<double, std::milli>(end - start).count();
    // std::cout << "路由收敛时间： " << duration_millisecond << "毫秒" << std::endl;
    // double duration_microsecond = std::chrono::duration<double, std::micro>(end - start).count();
    // std::cout << "路由收敛时间： " << duration_microsecond << "微秒" << std::endl;   
    // double duration_nanosecond = std::chrono::duration<double, std::nano>(end - start).count();
    // std::cout << "路由收敛时间： " << duration_nanosecond << "纳秒" << std::endl;
    total_duration_second += duration_second;
    total_intraCduration_second += intraCduration_second;
    total_interCduration_second += interCduration_second;

    std::cout << "初始簇内簇间路由协同成功！" << std::endl;
    RouCmpTimes++;

    // 删去抽象节点间的连接关系及IP
    DeleteIPforAbstractNode(AbstractClusterNodes);
}

void
SatRouting::UpdateSatRouter(NodeContainer& totalsates, std::vector<NodeContainer>& clusters, const vector<Ptr<LinkUtilizationMonitor>> &monitors, bool consType, uint32_t orbitNums, uint32_t satPerOrbits)
{
    uint32_t ClusterNum = clusters.size();
    std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode(ClusterNum, std::vector<std::vector<std::pair<uint32_t, uint32_t>>>(ClusterNum));
        
    uint32_t i = 0;
    while(ClusterNum > AbstractClusterNodes.GetN())
    {        
        AbstractClusterNodes.Add(AbstractClusterNodesBackup.Get(i));
        i++;
    }

    // 获取开始时间
    auto start = std::chrono::steady_clock::now();

    // 分簇后更新簇结构信息
    UpdateClusterInfo(totalsates, clusters, AbstractClusterNodes, BoundaryNode, monitors);
    auto UpdateClusterInfoCend = std::chrono::steady_clock::now();

    // 计算簇内路由表
    RecomputeIntraCRouter(clusters, BoundaryNode);
    auto intraCend = std::chrono::steady_clock::now();

    // 计算簇间路由表
    RecomputeInterCRouter(AbstractClusterNodes, BoundaryNode);
    auto interCend = std::chrono::steady_clock::now();

    // 协同簇内簇间路由
    CoordinateIntraInterCRouter(clusters, totalsates, AbstractClusterNodes, BoundaryNode, consType, orbitNums, satPerOrbits);   

    // 获取结束时间
    auto end = std::chrono::steady_clock::now();

    // 添加簇内备份路由
    AddIntraCBackupRouter(clusters, totalsates, consType, orbitNums, satPerOrbits);
 
    // 计算并输出函数执行时间
    double duration_second = std::chrono::duration<double>(end - start).count();
    double intraCduration_second = std::chrono::duration<double>(intraCend - UpdateClusterInfoCend).count() / (double)ClusterNum;
    double interCduration_second = std::chrono::duration<double>(interCend - intraCend).count() + std::chrono::duration<double>(UpdateClusterInfoCend - start).count() + std::chrono::duration<double>(end - interCend).count() / (double)ClusterNum;
    std::cout << "路由计算时间： " << duration_second << "秒, " << "簇内： " << intraCduration_second << "秒, 簇间： " << interCduration_second << "秒" << std::endl;
    // std::cout << "-----更新簇结构信息： " << std::chrono::duration<double>(UpdateClusterInfoCend - start).count() << "秒" << std::endl;
    // std::cout << "-----计算簇内路由表： " << std::chrono::duration<double>(intraCend - UpdateClusterInfoCend).count() << "秒" << std::endl;
    // std::cout << "-----计算簇间路由表： " << std::chrono::duration<double>(interCend - intraCend).count() << "秒" << std::endl;
    // std::cout << "-----协同簇内簇间路由： " << std::chrono::duration<double>(end - interCend).count() << "秒" << std::endl;

    total_duration_second += duration_second;
    total_intraCduration_second += intraCduration_second;
    total_interCduration_second += interCduration_second;

    std::cout << "更新簇内簇间路由协同成功！" << std::endl;
    RouCmpTimes++;
    
    // 删去抽象节点间的连接关系及IP
    DeleteIPforAbstractNode(AbstractClusterNodes);

    CalAvgQueueLen(totalsates);

    double currentTime = Simulator::Now().GetSeconds(); // 单位为s
    if(currentTime == (totalTimeStep - clusterUpdateStep))
    {
        avgRouCompt_second = total_duration_second / (double)RouCmpTimes;
        avgintraCRouCompt_second = total_intraCduration_second / (double)RouCmpTimes;
        avginterCRouCompt_second = total_interCduration_second / (double)RouCmpTimes;
        std::cout << "\nAverage Routing Computing Time: " << avgRouCompt_second << "s, " << "intra Cluster: " << avgintraCRouCompt_second << "s, inter Cluster: " << avginterCRouCompt_second << "s." << std::endl;
    }    
}

void
SatRouting::UpdateSatRouterSim(NodeContainer& totalsates, std::vector<NodeContainer>& clusters, bool consType, uint32_t orbitNums, uint32_t satPerOrbits)
{
    uint32_t ClusterNum = clusters.size();
    std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode(ClusterNum, std::vector<std::vector<std::pair<uint32_t, uint32_t>>>(ClusterNum));
        
    uint32_t i = 0;
    while(ClusterNum > AbstractClusterNodes.GetN())
    {        
        AbstractClusterNodes.Add(AbstractClusterNodesBackup.Get(i));
        i++;
    }

    // 分簇后更新簇结构信息
    UpdateClusterInfoSim(totalsates, clusters, AbstractClusterNodes, BoundaryNode);

    // 计算簇内路由表
    RecomputeIntraCRouter(clusters, BoundaryNode);

    // 计算簇间路由表
    RecomputeInterCRouter(AbstractClusterNodes, BoundaryNode);

    // 协同簇内簇间路由
    CoordinateIntraInterCRouter(clusters, totalsates, AbstractClusterNodes, BoundaryNode, consType, orbitNums, satPerOrbits);   

    // 添加簇内备份路由
    AddIntraCBackupRouter(clusters, totalsates, consType, orbitNums, satPerOrbits);
    
    // 删去抽象节点间的连接关系及IP
    DeleteIPforAbstractNode(AbstractClusterNodes);
}

void
SatRouting::DeleteSatRoutesSim(Ptr<Node> nodeSrc, Ptr<Node> nodeDst)
{
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteRoutesForNodes (nodeSrc, nodeDst);
}

void
SatRouting::UpdateSatRouterForConvergence(NodeContainer& totalsates, std::vector<NodeContainer>& clusters, const vector<Ptr<LinkUtilizationMonitor>> &monitors, bool consType, uint32_t orbitNums, uint32_t satPerOrbits)
{
    uint32_t ClusterNum = clusters.size();
    std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode(ClusterNum, std::vector<std::vector<std::pair<uint32_t, uint32_t>>>(ClusterNum));
    
    // 获取开始时间
    auto start = std::chrono::steady_clock::now();

    // 分簇后更新簇结构信息
    UpdateClusterInfo(totalsates, clusters, AbstractClusterNodes, BoundaryNode, monitors);
    auto UpdateClusterInfoCend = std::chrono::steady_clock::now();

    // 计算簇内路由表
    RecomputeIntraCRouter(clusters, BoundaryNode);
    auto intraCend = std::chrono::steady_clock::now();

    // 计算簇间路由表
    auto interCstart = std::chrono::steady_clock::now();
    RecomputeInterCRouter(AbstractClusterNodes, BoundaryNode);
    auto interCend = std::chrono::steady_clock::now();

    // 协同簇内簇间路由
    auto coordinateCstart = std::chrono::steady_clock::now();
    CoordinateIntraInterCRouter(clusters, totalsates, AbstractClusterNodes, BoundaryNode, consType, orbitNums, satPerOrbits);   
    std::cout << "更新簇内簇间路由协同成功！" << std::endl;

    // 获取结束时间
    auto end = std::chrono::steady_clock::now();

    // 添加簇内备份路由
    AddIntraCBackupRouter(clusters, totalsates, consType, orbitNums, satPerOrbits);
 
    // 计算并输出函数执行时间
    // double compute_duration_second = std::chrono::duration<double>(end - start).count() / (double)sates_num;
    double intraCcompute_second = std::chrono::duration<double>(intraCend - start).count() / (double)ClusterNum;
    double interCcompute_second = std::chrono::duration<double>(UpdateClusterInfoCend - start).count() 
                                    + std::chrono::duration<double>(interCend - interCstart).count() 
                                    + std::chrono::duration<double>(end - coordinateCstart).count() / (double)ClusterNum;
    
    double maxClusterDiameter = 0.0;
    int networkRadius = (orbitNums + satPerOrbits) / 2;
    for (size_t i = 0; i < m_clusterDiameter.size(); ++i) 
    {
        if(m_clusterDiameter[i] > maxClusterDiameter)
        {
            maxClusterDiameter = m_clusterDiameter[i];
        }
    }

    double linkdelay = 0.0;
    if(_isSate == 1)    linkdelay = (0.01556+0.00893) / 2.0;
    else if(_isSate == 2)   linkdelay = (0.01716+0.01179) / 2.0;
    else if(_isSate == 3)   linkdelay = (0.00785+0.00542) / 2.0;
    else if(_isSate == 4)   linkdelay = (0.00871+0.00561) / 2.0;        // TODO

    double intraCconverge_second = intraCcompute_second + 2 * linkdelay * maxClusterDiameter;
    double interCconverge_second = interCcompute_second + 2 * linkdelay * (double)networkRadius;
    std::cout << "簇内路由收敛时间：" << intraCconverge_second << "秒, 簇间路由收敛时间：" << interCconverge_second << "秒(计算-" << interCcompute_second << ")."<< std::endl;
    
    // 删去抽象节点间的连接关系及IP
    DeleteIPforAbstractNode(AbstractClusterNodes);
}

// //为地面网络节点添加路由
// void 
// SatRouting::AddGroundCRouter(NodeContainer& nodes, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
// {
//     SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(nodes, 0);
//     SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutes(nodes, BoundaryNode, 0, m_RlinkUtilization, m_clusterNetworkInf);
// }

//为簇内节点添加路由
void 
SatRouting::AddIntraCRouter(std::vector<NodeContainer>& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
    for(uint32_t i = 0; i < clusters.size(); ++i)
    {
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(clusters[i], 0);
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutes(clusters[i], BoundaryNode, 0, m_RlinkUtilization, m_clusterNetworkInf);
    }
}

//为簇间节点添加路由
void 
SatRouting::AddInterCRouter(NodeContainer& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(clusters, 1);
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutes(clusters, BoundaryNode, 1, m_RlinkUtilization, m_clusterNetworkInf);
}

//为簇内节点更新路由
void 
SatRouting::RecomputeIntraCRouter(std::vector<NodeContainer>& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
    for(uint32_t i = 0; i < clusters.size(); ++i)
    {
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteClusteringGlobalRoutes (clusters[i]);
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(clusters[i], 0);
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutes(clusters[i], BoundaryNode, 0, m_RlinkUtilization, m_clusterNetworkInf);
    }
}

//为簇间节点更新路由
void 
SatRouting::RecomputeInterCRouter(NodeContainer& clusters, std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode)
{
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteClusteringGlobalRoutes (clusters);
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(clusters, 1);
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutes(clusters, BoundaryNode, 1, m_RlinkUtilization, m_clusterNetworkInf);
}

// 协同簇内簇间路由
void 
SatRouting::CoordinateIntraInterCRouter(std::vector<NodeContainer>& clusters, 
                                        NodeContainer& nodes, 
                                        NodeContainer& AbstractClusterNodes, 
                                        std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>> BoundaryNode,
                                        bool ConsType,
                                        uint32_t Orbit, 
                                        uint32_t SatPerOrbit)
{
    uint32_t n = clusters.size();
    for(uint32_t i = 0; i < n; ++i)
    {
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->AddClusterRoutingTables(clusters[i], nodes, AbstractClusterNodes, BoundaryNode, ConsType, Orbit, SatPerOrbit);
    }
    // std::cout << "簇内簇间路由协同成功！" << std::endl;
}

// 为簇内节点添加备份路由
void 
SatRouting::AddIntraCBackupRouter(  std::vector<NodeContainer>& clusters, 
                                    NodeContainer& nodes, 
                                    bool ConsType,
                                    uint32_t Orbit, 
                                    uint32_t SatPerOrbit)
{
    for(uint32_t i = 0; i < clusters.size(); ++i)
    {
        SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringAddBackupRoutes(clusters[i], nodes, ConsType, Orbit, SatPerOrbit);
    }
}

// 删去抽象节点间连接关系
void 
SatRouting::DeleteIPforAbstractNode(NodeContainer& AbstractClusterNodes)
{
    for(int i = 0; i < int(AbstractClusterNodes.GetN()); i++)
    {
        Ptr<Node> node = AbstractClusterNodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if(!ipv4) continue;
        Ptr<Ipv4Interface> interface;
        for(uint32_t j = 1; j < ipv4->GetNInterfaces(); ++j)
        {
            if(ipv4->GetNAddresses(j) == 0) continue;
            // Ipv4Address ipaddress = ipv4->GetAddress(j, 0).GetLocal();    // 获取到某节点的接口IP地址
            // std::cout << "Remove IP " << j << ": " << ipaddress;
            ipv4->RemoveAddress(j, 0);
            ipv4->SetDown(j);

            // std::cout << "  Remove IP success!" << std::endl;
        }
    }  
}

// 计算平均缓冲队列长度
void 
SatRouting::CalAvgQueueLen(NodeContainer& nodes)
{
    uint32_t deviceNum = 0, totalQueueSize = 0;
    static double avgTimeQueueSize = 0.0;
    static int timeStep = 0;
    double avgQueueSize = 0.0;
    for(int i = 0; i < int(nodes.GetN()); i++)
    {
        Ptr<Node> node = nodes.Get(i);
        for(uint32_t j = 0; j < node->GetNDevices(); j++)
        {
            Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(j));
            if(p2pNetDevice != nullptr)
            {
                Ptr<Queue<Packet>> dQueue = p2pNetDevice->GetQueue();
                uint32_t curPackets = dQueue->GetNPackets();
                totalQueueSize += curPackets;
            }
            deviceNum++;
        }
    }
    avgQueueSize = (double)totalQueueSize / (double)deviceNum;
    avgTimeQueueSize += avgQueueSize;
    timeStep++;
    double currentTime = Simulator::Now().GetSeconds(); // 单位为s
    std::cout << "currentTime: " << currentTime << ", avgQueueSize: " << avgQueueSize << ", deviceNum: " << deviceNum << std::endl;
    
    if(currentTime == (totalTimeStep - clusterUpdateStep))
    {
        avgTimeQueueSize = avgTimeQueueSize / (double)timeStep;
        std::cout << "Average Queue Length: " << avgTimeQueueSize << std::endl;
    }
}

void 
SatRouting::RecomputeRouteInfo(void)
{
    // 路由重计算未完成添加？
    // SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteClusteringGlobalRoutes (clusters[i]);
    // SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(clusters[i], 0);
    // SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutes(clusters[i], BoundaryNode, 0, m_RlinkUtilization, m_clusterNetworkInf);

    // SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->AddClusterRoutingTables(clusters[i], nodes, AbstractClusterNodes, BoundaryNode, ConsType, Orbit, SatPerOrbit);
}

NodeContainer
SatRouting::GetMainPathNodesSim(Ptr<Node> ScrNode, Ptr<Node> DestNode, NodeContainer& nodes)
{
    //std::cout<<"src: "<<ScrNode->GetId() << "dst: "<<DestNode->GetId()<<std::endl;
    
    NodeContainer MainPathNodes, FaultNodes;
    Ipv4RoutingTableEntry* table = GetNextFromTable(ScrNode, DestNode);
    if (table == nullptr)    return MainPathNodes;
    Ipv4Address nextaddress = table->GetGateway();
    uint32_t id = GetIdFromIp(nextaddress);
    // std::cout << "nextId: " << id << std::endl;
    uint32_t index = nodes.Get(0)->GetId();
    // std::cout << index<< " nextId: " << id << std::endl;
    Ptr<Node> next = nodes.Get(id-index);
    MainPathNodes.Add(ScrNode);
    uint32_t i = 0;
    while(next->GetId() != DestNode->GetId())
    {
        MainPathNodes.Add(next);
        table = GetNextFromTable(next, DestNode);
        if (table == nullptr)    return FaultNodes;
        nextaddress = table->GetGateway();
        id = GetIdFromIp(nextaddress);
        //std::cout << "nextId: " << id << std::endl;
        next = nodes.Get(id-index);
        i++;
        if(i > ((sate_num + orbit_num) / 2 + 2) ) //直径阈值
        {
            // 算不出路径
            return FaultNodes;
        }
    }
    MainPathNodes.Add(DestNode);
    return MainPathNodes;
}

NodeContainer
SatRouting::GetBackupPathNodesSim(Ptr<Node> ScrNode, Ptr<Node> DestNode, NodeContainer& mainPathNodes, NodeContainer& nodes)
{
    NodeContainer BackupPathNodes, FaultNodes;
    BackupPathNodes.Add(ScrNode);
    uint32_t index = nodes.Get(0)->GetId();
    uint32_t srcID = ScrNode->GetId() - index;  // 卫星从0开始
    uint32_t dstID = DestNode->GetId() - index;
    
    if(mainPathNodes.GetN() == 2)
    { // 源目的卫星一跳可达
      if((srcID/sate_num) == (dstID/sate_num))
      {// 同轨
        if(orbit_num == 1)
        {
            return FaultNodes;
        }
        if((srcID/sate_num) == (orbit_num - 1))
        {
            BackupPathNodes.Add(nodes.Get(srcID - sate_num));
            BackupPathNodes.Add(nodes.Get(dstID - sate_num));
        }
        else
        {
            BackupPathNodes.Add(nodes.Get(srcID + sate_num));
            BackupPathNodes.Add(nodes.Get(dstID + sate_num));
        }
      }
      else if (((srcID + 1)%sate_num) != 0)
      {// 异轨
        BackupPathNodes.Add(nodes.Get(srcID + 1));
        BackupPathNodes.Add(nodes.Get(dstID + 1));
      }
      else
      {// 异轨 
        BackupPathNodes.Add(nodes.Get(srcID - (sate_num - 1)));
        BackupPathNodes.Add(nodes.Get(dstID - (sate_num - 1)));  
      }
    }
    else if((srcID/sate_num) == (dstID/sate_num))
    { // 源目的卫星位于同一轨道
        if(orbit_num == 1)
        {
            return FaultNodes;
        }

      if((srcID/sate_num) == (orbit_num - 1))
      {
        NodeContainer tmpPathNodes = GetMainPathNodesSim(nodes.Get(srcID - sate_num), 
                                                         nodes.Get(dstID - sate_num), 
                                                         nodes);
        BackupPathNodes.Add(tmpPathNodes);
      }
      else
      {
        NodeContainer tmpPathNodes = GetMainPathNodesSim(nodes.Get(srcID + sate_num), 
                                                         nodes.Get(dstID + sate_num), 
                                                         nodes);
        BackupPathNodes.Add(tmpPathNodes);
      }
    }
    else if((srcID%sate_num) == (dstID%sate_num))
    { // 源目的卫星位于异轨同一序列
      if((srcID%sate_num) == (sate_num - 1))
      {
        NodeContainer tmpPathNodes = GetMainPathNodesSim(nodes.Get(srcID - (sate_num-1)), 
                                                         nodes.Get(dstID - (sate_num-1)), 
                                                         nodes);
        BackupPathNodes.Add(tmpPathNodes);
      }
      else
      {
        NodeContainer tmpPathNodes = GetMainPathNodesSim(nodes.Get(srcID + 1), 
                                                         nodes.Get(dstID + 1), 
                                                         nodes);
        BackupPathNodes.Add(tmpPathNodes);
      }
    }
    else
    {
        Ipv4RoutingTableEntry* table = GetBackupNextFromTable(ScrNode, DestNode, mainPathNodes);
        if (table == nullptr)    return FaultNodes;
        Ipv4Address nextaddress = table->GetGateway();
        uint32_t id = GetIdFromIp(nextaddress);
        // std::cout << "backupNextId: " << id << std::endl;
        uint32_t index = nodes.Get(0)->GetId();
        Ptr<Node> next = nodes.Get(id-index);
        uint32_t i = 0;
        while(next->GetId() != DestNode->GetId())
        {
            BackupPathNodes.Add(next);
            table = GetNextFromTable(next, DestNode);
            if (table == nullptr)    return FaultNodes;
            nextaddress = table->GetGateway();
            // std::cout << "backupNextaddress: " << nextaddress << std::endl;
            id = GetIdFromIp(nextaddress);
            next = nodes.Get(id-index);
            i++;
            if(i > nodes.GetN())
            {
                // 算不出路径
                return FaultNodes;
            }
        }
        
    }
    BackupPathNodes.Add(DestNode);
    return BackupPathNodes;
}

uint32_t
SatRouting::GetIdFromIp(Ipv4Address ip)
{
    //std::cout << "输入的IP为：" << ip << std::endl;
    for (auto i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
        Ptr<Node> node = *i;
        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        uint32_t interfacenumber = ippp->GetNInterfaces();      // 得到该节点的接口数目           
        // std::cout << "下一跳ID号为：" << node->GetId()<< "  " << interfacenumber << std::endl;
        for (uint32_t iterate = 1; iterate < interfacenumber; iterate++)     // 遍历该节点的每一个接口
        {
            if(ippp->GetNAddresses(iterate) == 0) continue;
            Ipv4Address ipaddress = ippp->GetAddress(iterate, 0).GetLocal();    // 获取到某节点的接口IP地址
            // std::cout << "遍历中的IP为：" << ipaddress << std::endl;
            // std::cout << "输入的IP为：" << ip << std::endl;
            
            if (ipaddress == ip) return node->GetId();            // 将获取的所有接口IP与目的节点IP比较, 若相同则返回节点序号  
        }
    }
    // logMsg("ok");
    //std::cout << "GetIdFromIp:: 没办法通过IP地址找到节点" << std::endl;
    return -1;
}

Ipv4RoutingTableEntry*
SatRouting::GetNextFromTable(Ptr<Node> ScrNode, Ptr<Node> DestNode)
{
    Ipv4Address test_ip("0.0.0.0");
    Ptr<GlobalRouter> router = ScrNode->GetObject<GlobalRouter>();
    if(!router) std::cout << "无效的GlobalRouter" <<std::endl;
    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
    Ipv4Address nextaddress;
    Ipv4RoutingTableEntry* table;
    // std::cout << "NRoutes ：" << gr->GetNRoutes() <<std::endl;
    for(uint32_t i = 0; i < gr->GetNRoutes(); ++i)
    {
        table = gr->GetRoute(i);
        // std::cout << "table->GetDest()：" << table->GetDest() << "\n";
        if(table->GetDest() == test_ip) continue;
        if(GetIdFromIp(table->GetDest()) == DestNode->GetId())
        {
            // std::cout << "找到下一跳地址了" << std::endl;
            nextaddress = table->GetGateway();
            // std::cout << "下一跳地址为:" << nextaddress << std::endl;
            return table;
        }

    }
    NS_ASSERT(table);
    return nullptr;
}

Ipv4RoutingTableEntry*
SatRouting::GetBackupNextFromTable(Ptr<Node> ScrNode, Ptr<Node> DestNode, NodeContainer& mainPathNodes)
{
    Ipv4Address test_ip("0.0.0.0");
    Ptr<GlobalRouter> router = ScrNode->GetObject<GlobalRouter>();
    if(!router) std::cout << "无效的GlobalRouter" <<std::endl;
    Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
    Ipv4Address nextaddress;
    Ipv4RoutingTableEntry* table;
    uint32_t id;
    // std::cout << "NRoutes ：" << gr->GetNRoutes() <<std::endl;
    for(uint32_t i = 0; i < gr->GetNRoutes(); ++i)
    {
        table = gr->GetRoute(i);
        // std::cout << "table->GetDest()：" << table->GetDest() << "\n";
        if(table->GetDest() == test_ip) continue;
        if(GetIdFromIp(table->GetDest()) == DestNode->GetId())
        {
            // std::cout << "找到下一跳地址了" << std::endl;
            // std::cout << "table->GetDest()：" << table->GetDest() << "\n";
            nextaddress = table->GetGateway();
            // std::cout << "下一跳地址为:" << nextaddress << std::endl;
            id = GetIdFromIp(nextaddress);
            if(mainPathNodes.Contains(id)) 
            {
                continue;
            }
            // std::cout << "下一跳地址为:" << nextaddress << std::endl;
            return table;
        }

    }
    NS_ASSERT(table);
    return nullptr;
}

void 
SatRouting::GetMainPathSim(NodeContainer& nodes)
{
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteClusteringGlobalRoutes (nodes);
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(nodes, 0);
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutesSim(nodes);
}

void 
SatRouting::GetBackupPathSim(NodeContainer& nodes, NodeContainer& deletenodes, NodeContainer& remainnodes)
{
    remainnodes = DeleteNode(nodes, deletenodes);
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->DeleteClusteringGlobalRoutes (remainnodes);
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringBuildGlobalRoutingDatabase(remainnodes, 0);
    SimulationSingleton<SatGlobalRouteManagerImpl>::Get()->ClusteringInitializeRoutesSim(remainnodes);
}

NodeContainer
SatRouting::DeleteNode(NodeContainer& nodes, NodeContainer& deletenodes)
{
    NodeContainer RemainNode;
    for(uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        if(!deletenodes.Contains(node->GetId())) RemainNode.Add(node);
    }
    return RemainNode;
}