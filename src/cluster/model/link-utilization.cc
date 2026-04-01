#include "link-utilization.h"

NS_LOG_COMPONENT_DEFINE ("LinkUtilizationMonitor");

TypeId 
LinkUtilizationMonitor::GetTypeId(void)
{
    static ns3::TypeId tid = ns3::TypeId("LinkUtilizationMonitor")
                                .SetParent(ns3::Object::GetTypeId())
                                .SetGroupName("LinkUtilizationMonitoring");
    return tid;
}

void
LinkUtilizationMonitor::SetLinkCapacity(uint32_t l)
{
    link_capacity = l;
}

void
LinkUtilizationMonitor::SetStopTime(double t)
{
    m_time = t;
}

void 
LinkUtilizationMonitor::StartMonitoring(Ptr<Node> node)
{
    m_node = node; 
    // 获取节点上的所有设备 Get the node's all network device
    for (uint32_t deviceId = 0; deviceId < m_node->GetNDevices(); ++deviceId) 
    {
        Ptr<NetDevice> device = node->GetDevice(deviceId);
        Ptr<PointToPointNetDevice> devices = DynamicCast<PointToPointNetDevice>(device);
        if (devices == nullptr) 
        {
            NS_LOG_ERROR("Device cast failed for device " << devices);
            continue;
        }
        devices->TraceConnectWithoutContext("DeviceTx", MakeCallback(&LinkUtilizationMonitor::TrackTxBytes, this));
    }
}

void 
LinkUtilizationMonitor::TrackTxBytes(Ptr<const Packet> p, Ptr<const PointToPointNetDevice> p2p) 
{
    uint32_t p2pIF = p2p->GetIfIndex();
    Ptr<Node> node = p2p->GetNode();
    Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
    Ipv4Address ipaddress = ippp->GetAddress(p2pIF,0).GetLocal();
    
    // 累加发送的字节数（格式：map(卫星IP地址，字节数)）
    m_device1[ipaddress] += p->GetSize();
    
    // 累加发送的字节数（格式：map(MAC地址，字节数)）
    m_device[ipaddress] += p->GetSize();

    // 累加发送的字节数（格式：map(接口号，字节数)）
    m_totalDevice[p2pIF] += p->GetSize();
}

double 
LinkUtilizationMonitor::CalculateLinkUtilization(uint32_t bytes, uint32_t capacity) 
{
    //calculate the link utilization(bps)
    return (bytes * 8.0) / capacity; 
}
void 
LinkUtilizationMonitor::CheckAndUpdateMaxUtilization() 
{  
    if (Simulator::Now() > Seconds(m_time)) //****
    {
        return; // finish the recursive
    }
    max_utilization = 0.0;
    for (auto &entry : m_device) 
    {
        double current_utilization = 0.0;
        // Address deviceId = entry.first;
        current_utilization = CalculateLinkUtilization(entry.second, link_capacity);//***m_link
        // judge the max link utilization
        if(max_utilization < current_utilization)
        {
            max_utilization = current_utilization;
        }
        // cout <<"Time: "<<Simulator::Now().GetSeconds()<<" Node " << m_node->GetId() << ", Device " << deviceId
        //                     << ", Max Link Utilization: " << current_utilization << endl;
    }
    m_device.clear();
    Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateMaxUtilization, this);
}

void 
LinkUtilizationMonitor::CheckAndUpdateEachLinkUtilization() 
{  
    if (Simulator::Now() > Seconds(m_time)) 
    {
        return; // finish the recursive
    }
    eachD_utilization.clear();

    for (auto &entry : m_device1) 
    {
        double current_utilization = 0.0;
        // Ipv4Address deviceIP = entry.first;
        current_utilization = CalculateLinkUtilization(entry.second, link_capacity);//***m_link

        eachD_utilization[entry.first] = current_utilization;
    }
    m_device1.clear();
    Simulator::Schedule(Seconds(1), &LinkUtilizationMonitor::CheckAndUpdateEachLinkUtilization, this);
}