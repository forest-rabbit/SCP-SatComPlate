#include "access.h"
#include <cstdlib>
#include <random>

int rows = 0;
int cols = 0;
int d_flag = 0;
int c_flag = 0;
//获取excel数据
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
        //if(i >= 5) break;
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
    d_flag = 1;//更新链路断开
    std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> P2Pdevices = GetDevicesFromNodes(node1, node2);
    Ptr<PointToPointNetDevice> dev1 = P2Pdevices.first;
    Ptr<PointToPointNetDevice> dev2 = P2Pdevices.second;
    dev1->DownTheLink();
    dev2->DownTheLink();
    if(_SDNRoute)
    {
        uint32_t j = dev1->GetIfIndex();
        uint32_t k = dev2->GetIfIndex();
        
        Ptr<Ipv4> ipv4;
        ipv4 = node1->GetObject<Ipv4>();
        if(ipv4)    ipv4->SetDown(j);
        ipv4 = node2->GetObject<Ipv4>();
        if(ipv4)    ipv4->SetDown(k);        
    }
}

void
LinkUp(Ptr<Node> node1, Ptr<Node> node2)
{
    cout<<"时间: "<<Simulator::Now().GetSeconds()<< " "<<node1->GetId()<<" "<<node2->GetId()<<" 两个节点连接"<<endl;
    c_flag = 1;
    std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> P2Pdevices = GetDevicesFromNodes(node1, node2);
    Ptr<PointToPointNetDevice> dev1 = P2Pdevices.first;
    Ptr<PointToPointNetDevice> dev2 = P2Pdevices.second;
    dev1->UpTheLink();
    dev2->UpTheLink();
    if(_SDNRoute)
    {
        uint32_t j = dev1->GetIfIndex();
        uint32_t k = dev2->GetIfIndex();
        
        Ptr<Ipv4> ipv4;
        ipv4 = node1->GetObject<Ipv4>();
        if(ipv4)    ipv4->SetUp(j);
        ipv4 = node2->GetObject<Ipv4>();
        if(ipv4)    ipv4->SetUp(k);    
    }
}

void LinkChangeSim(NodeContainer& nodes, uint32_t holdTime, uint32_t id1, uint32_t id2){
    return;// bug
    Ptr<Node> node1 = nodes.Get(id_node[id1]); 
    Ptr<Node> node2 = nodes.Get(id_node[id2]);
    LinkUp(node1, node2); // LinkUp
    Simulator::Schedule(Seconds(holdTime), &LinkDown, node1, node2); // LinkDown
}

//文件中的节点编号从0开始，输入的是卫星节点容器，打印的节点编号偏移量为7
void 
LinkChange(NodeContainer& nodes,std::string name)
{
    if(_mode == 3) return ;    // 在检测从控制器失效过程中，反向缝会对检测结果造成影响
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

void
SetLinkAvaAvailability(NodeContainer& nodes)
{
    if(abs(linkAvailability-1.0) < 1e-6) return;

    std::random_device rd;  // 用于获得种子
    std::mt19937 gen(rd()); // 使用随机设备种子初始化Mersenne Twister生成器
    std::uniform_int_distribution<> distrib(1, (totalTimeStep-10)); // 定义分布范围[1, 100]

    double MTBI = totalTimeStep <= 10 ? totalTimeStep*10 * linkAvailability : (totalTimeStep-10) * linkAvailability;        // 平均中断间隔时间
    double MTTR = totalTimeStep <= 10 ? totalTimeStep*10 * (1 - linkAvailability) : (totalTimeStep-10) * (1 - linkAvailability);  // 平均恢复时间

    sate_num = sates_num / orbit_num;

    // 轨道内链路 lxy:注释以下内容，不断开同轨链路
    // for(uint32_t i=0; i<orbit_num; i++){
    //     for(uint32_t j=0; j<sate_num; j++){
    //         Ptr<Node> node = nodes.Get(i*sate_num+j);
    //         Ptr<Node> next = nodes.Get(i*sate_num+(j+1)%sate_num);

    //         // if(!IsConnect(node, next))
    //         //     continue;

    //         //如果存在链路
    //         double time1 = distrib(gen);    // 生成随机数
    //         while(time1 > MTBI)
    //         {
    //             time1 = distrib(gen);
    //         }
    //         double time2 = time1 + MTTR;
    //         Simulator::Schedule(Seconds(time1), &LinkDown, node, next);  //LinkDown
    //         Simulator::Schedule(Seconds(time2), &LinkUp, node, next);    //LinkUp;
    //     }
    // }
    // 轨道间链路
    for(uint32_t j=0; j<sate_num; j++){
        for(uint32_t i=0; i<(orbit_num-1); i++){
            Ptr<Node> node = nodes.Get(i*sate_num+j);
            Ptr<Node> next = nodes.Get((i+1)*sate_num+j);

            // if(!IsConnect(node, next))
            //     continue;

            //如果存在链路
            double time1 = distrib(gen);    // 生成随机数
            while(time1 > MTBI)
            {
                time1 = distrib(gen);
            }
            double time2 = time1 + MTTR;
            Simulator::Schedule(Seconds(time1), &LinkDown, node, next);  //LinkDown
            Simulator::Schedule(Seconds(time2), &LinkUp, node, next);    //LinkUp;
        }
    }
}

//判断是否连接
bool 
IsConnect(Ptr<Node> node1, Ptr<Node> node2)
{
    NodeContainer neighbor = FindNeibors(node1);
    for(uint32_t i = 0; i < neighbor.GetN(); ++i)
    {
        if(neighbor.Get(i)->GetId() == node2->GetId()) return true;
    }
    return false;
}

//寻找周围邻居节点
NodeContainer 
FindNeibors(Ptr<Node> node)
{
    NodeContainer neighbor;
    uint32_t end = ISLNum < node->GetNDevices() ? ISLNum : node->GetNDevices() - 1;

    for(uint32_t i = 0; i <= end; ++i)
    {
        Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice>(node->GetDevice(i));

        Ptr<Ipv4> ippp = node->GetObject<Ipv4> ();
        if(!ippp) continue;
        if(ippp->GetNAddresses(i) == 0) continue;

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
            // 获取与给定设备相连的第一个设备所在的节点
            Ptr<Node> connectedNode = otherDevice->GetNode ();
            // 输出结果
            neighbor.Add(connectedNode);
            //std::cout << "与给定设备相连的第一个设备所在的节点ID: " << connectedNode->GetId () << std::endl;
        }
    }
    return neighbor;
}