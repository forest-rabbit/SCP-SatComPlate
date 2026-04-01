#include "fault-judgment.h"

pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>>
GetDevicesFromNodes(Ptr<Node> node1, Ptr<Node> node2)
{
    Ptr<PointToPointNetDevice> p2p_device1;
    Ptr<PointToPointNetDevice> p2p_device2;
    for(uint32_t i = 0; i < node1->GetNDevices(); ++i)
    {
        p2p_device1 = DynamicCast<PointToPointNetDevice>(node1->GetDevice(i));
        if(p2p_device1 != nullptr)
        {
            Ptr<Channel> channel = p2p_device1->GetChannel ();
            // 获取通道中连接的所有设备
            Ptr<NetDevice> other_device;
            if (channel->GetNDevices() == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == p2p_device1) 
                {
                    other_device = channel->GetDevice (1);
                } 
                else 
                {
                    other_device = channel->GetDevice (0);
                }
            }
            Ptr<Node> connect_node = other_device->GetNode();
            if(connect_node->GetId() == node2->GetId())
            {
                p2p_device2 = DynamicCast<PointToPointNetDevice>(other_device);
                break;
            }
        }
    }
    return {p2p_device1, p2p_device2};
}

NodeContainer 
FindNeibors(Ptr<Node> node)
{
    NodeContainer neighbor;
    for(uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<PointToPointNetDevice> p2p_device = DynamicCast<PointToPointNetDevice>(node->GetDevice(i));
        if(p2p_device != nullptr && p2p_device->IsLinkUp())
        {
            Ptr<Channel> channel = p2p_device->GetChannel();
            Ptr<NetDevice> other_device;
            if (channel->GetNDevices () == 2) // P2P 通道应该只有两个设备
            { 
                if (channel->GetDevice (0) == p2p_device) 
                {
                    other_device = channel->GetDevice (1);
                } 
                else 
                {
                    other_device = channel->GetDevice (0);
                }
            }
            Ptr<Node> connect_node = other_device->GetNode();
            neighbor.Add(connect_node);
        }
    }
    return neighbor;
}

bool
IsConnect(Ptr<Node> node1, Ptr<Node> node2)
{
    NodeContainer neighbor = FindNeibors(node1);
    for(uint32_t i = 0; i < neighbor.GetN(); ++i)
    {
        if(neighbor.Get(i)->GetId() == node2->GetId()) 
            return true;
    }
    return false;
}

void 
SetLinkFault(NodeContainer node)
{
    // 初始化随机数生成器
    srand(time(NULL));
    uint32_t node_num  = node.GetN();
    for(uint32_t i = 0; i < node_num; i++)
    {
        Ptr<Node> node1 = node.Get(i);
        for(uint32_t j = 0; j < node_num; j++)
        {
            if(i != j)
            {
                Ptr<Node> node2 = node.Get(j);
                if(IsConnect(node1, node2))
                {
                    pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> p2p_devices = GetDevicesFromNodes(node1, node2);
                    if(1 == rand() % 100)
                    {
                        p2p_devices.first->DownTheLink();
                        p2p_devices.second->DownTheLink();
                    } 
                }
            }
        }
    }   
}

std::unordered_map<uint32_t, uint32_t>
JudgeFault(NodeContainer node)
{
    //存储故障节点和故障类型 -- key:故障节点ID value:故障类型--存储
    std::unordered_map<uint32_t,uint32_t> fault;

    uint32_t node_num = node.GetN();
    for(uint32_t i = 0; i < node_num; i++)
    {
        uint32_t count = 0;
        for(uint32_t j = 0; j < node.Get(i)->GetNDevices(); j++)
        {
            Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node.Get(i)->GetDevice(j));
            if(p2pNetDevice != nullptr)
            {
                if(!p2pNetDevice->IsLinkUp())
                {
                    //cout <<"Node ID: "<<i<<" Device ID: "<< j <<" Dis" <<endl;
                    count++;
                } 
            }
        }
        if(count == 4)
        {
            cout <<"Node ID: "<< i << " Node Fault" << endl;
            fault[node.Get(i)->GetId()] = 0; 
        }
        if(count >= 1 && count <= 3)
        {
            cout<<"Node ID: "<< i <<" Link Fault" <<endl;
            fault[node.Get(i)->GetId()] = 1; 
        }
    }
    return fault;
}