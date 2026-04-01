#include "all-node.h"

#include "para.h"
#include "topo.h"
#include "satrouting.h"
#include <cstdint>
#include <ns3/ff-mac-common.h>
#include <ns3/openflow-interface.h>
#include <ns3/openflow-packet.h>
#include <ns3/openflow-sdn-controller.h>
#include <ns3/ptr.h>

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample-node");

// 从控充当主控，附加信息，不需要预先设置
bool m_toMasterflag = false; 

namespace ns3 {

std::string GetStateString(State s) {
  switch(s) {
  case satellite:
    return "卫星节点";
  case slaveController:
    return "从控制器";
  case masterController:
    return "主控制器";
  case mutilController:
    return "星上临时主控制器";
  case breakNode:
    return "失效节点";
  }
  return "其他";
}

SinglePacketTcpClient::SinglePacketTcpClient()
    : m_socket(0), m_peerAddress(), m_peerPort(0)
{
}

SinglePacketTcpClient::~SinglePacketTcpClient()
{
    m_socket = 0;
}

void SinglePacketTcpClient::Setup(Address address, uint16_t port, Ptr<Packet> data)
{
    m_peerAddress = address;
    m_peerPort = port;
    m_data = data;
}

void SinglePacketTcpClient::HandleConnect(Ptr<Socket> socket)
{
    // 创建并发送数据包
    socket->Send(m_data);
    // NS_LOG_UNCOND("Client: Sent packet with size " << m_data->GetSize() << " and data: " << m_data);
}

void SinglePacketTcpClient::HandleConnectError(Ptr<Socket> socket)
{
    NS_LOG_ERROR("Client: Connection failed");
}

void SinglePacketTcpClient::StartApplication()
{
    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);
        m_socket->SetConnectCallback(
            MakeCallback(&SinglePacketTcpClient::HandleConnect, this),
            MakeCallback(&SinglePacketTcpClient::HandleConnectError, this));
        m_socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
    }
}

void SinglePacketTcpClient::StopApplication()
{
    if (m_socket)
    {
        m_socket->Close();
    }
}

SinglePacketTcpServer::SinglePacketTcpServer()
    : m_socket(0), m_port(8080)
{
}

SinglePacketTcpServer::~SinglePacketTcpServer()
{
    m_socket = 0;
}

void SinglePacketTcpServer::Setup(uint16_t port)
{
    m_port = port;
}

bool SinglePacketTcpServer::HandleAccept(Ptr<Socket> socket, const Address& from)
{
    socket->SetRecvCallback(MakeCallback(&SinglePacketTcpServer::HandleRead, this));
    // NS_LOG_UNCOND("Server: Accepted connection from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
    // NS_LOG_UNCOND("Server: Setting receive callback for socket " << socket);
    return true; // 接受连接
}

void SinglePacketTcpServer::NewConnectionCreated(Ptr<Socket> socket, const Address &address)
{
  // std::cout << "New connection created with: " << InetSocketAddress::ConvertFrom(address).GetIpv4() << std::endl;
  // 为新连接设置接收数据的回调函数
  socket->SetRecvCallback(MakeCallback(&SinglePacketTcpServer::HandleRead, this));
}


void SinglePacketTcpServer::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    // NS_LOG_UNCOND("Server: HandleRead triggered, trying to receive packet...");
    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() > 0)
        {
          // NS_LOG_UNCOND("Server: Received packet with size " << packet->GetSize());
            // 调用外部设置的回调函数
            if (!m_packetReceiveCallback.IsNull())
            {
                m_packetReceiveCallback(packet, from);
            }
            else
            {
              // 默认处理：打印数据
              uint8_t* buffer = new uint8_t[packet->GetSize()];
              packet->CopyData(buffer, packet->GetSize());
              std::string data((char*)buffer, packet->GetSize());
              // NS_LOG_UNCOND("Server: Received packet with size " << packet->GetSize() << " and data: " << data);
              delete[] buffer;
            }
        }
        else
        {
          break;
        }
    }
}

void SinglePacketTcpServer::StartApplication()
{
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
    m_socket = Socket::CreateSocket(GetNode(), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
    if (m_socket->Bind(local) == -1)
    {
        // NS_LOG_ERROR("Server: Failed to bind socket");
        return;
    }
    m_socket->Listen();
    m_socket->SetRecvCallback (MakeCallback (&SinglePacketTcpServer::HandleRead, this));
    m_socket->SetAcceptCallback(
        MakeCallback(&SinglePacketTcpServer::HandleAccept, this),
        MakeCallback(&SinglePacketTcpServer::NewConnectionCreated, this));
}

void SinglePacketTcpServer::StopApplication()
{
    if (m_socket)
    {
      m_socket->Close();
    }
}


void satelliteNode::SetToMasterController(double interval,        ///< 周期 
                            // Ptr<ofi::MasterController> master,   ///< 主控制器指针 
                            uint32_t masterID,            ///< 主控制器ID
                            NodeContainer master_nodes,   ///< 主控制器节点集合
                            NodeContainer slavesnodes    ///< 从控制器节点集合
                            ){ 
  m_state = masterController;
  m_interval = interval;
  m_master = nullptr; // master;
  m_masterID = masterID;
  m_master_nodes = master_nodes;
  m_slavesnodes = slavesnodes;
  if(_CtrlInfoOutput && masterID == this->GetId()) cout << "node " << this->GetId() << " 设置为主控制器！" << endl;
}
  
void satelliteNode::SetToSlaveController(double interval,        ///< 周期 
                            // Ptr<ofi::SlaveController> slave,   ///< 从控制器指针 
                            NodeContainer master_nodes,   ///< 主控制器节点集合
                            NodeContainer slavesnodes,    ///< 从控制器节点集合
                            NodeContainer intraNodes      ///< 簇内节点集合
                            ){
  m_state = slaveController;
  m_interval = interval;
  m_controller = nullptr; // slave;
  m_master_nodes = master_nodes;
  m_slavesnodes = slavesnodes;
  m_intraNodes = intraNodes;
  m_checkFlag = false;
  HeartBeatShedule();
  if(_CtrlInfoOutput) cout << "node " << this->GetId() << " 设置为从控制器！" << endl;
}

void satelliteNode::SetToNormal(double interval,        ///< 周期
                    // Ptr<ofi::SlaveController> slave,   ///< 从控制器指针 
                    NodeContainer master_nodes,   ///< 主控制器节点集合
                    NodeContainer slavesnodes,    ///< 从控制器节点集合
                    NodeContainer intraNodes)   ///< 簇内节点集合
                    {
  // m_state = satellite;
  m_state = satellite;
  m_interval = interval;
  m_controller = nullptr; //slave;
  m_master_nodes = master_nodes;
  m_slavesnodes = slavesnodes;
  m_intraNodes = intraNodes;
  m_masterID = 0;
  m_checkFlag = false;
  HeartBeatShedule();
  if(_CtrlInfoOutput) cout << "node " << this->GetId() << " 设置为普通节点！" << endl;
}

bool
satelliteNode::SendPacket(Ptr<Packet> packet, Ipv4Address srcAddress, Ipv4Address destAddress, uint32_t inter, uint32_t tag){
  if(!_tranProc){
    // UDP
    Ptr<Socket> socket = Socket::CreateSocket(this, UdpSocketFactory::GetTypeId());
    socket->SetIpTos(0xE0);   // CS7最高优先级

    DTag ttag;
    ttag.SetPrio(0);
    ttag.SetTimestamp(Simulator::Now ());
    ttag.SetSize(packet->GetSize());
    ttag.SetTag(tag);
    packet->AddPacketTag(ttag);

    uint16_t srcPort = 0; // 自动选择源端口
    socket->Bind(InetSocketAddress(srcAddress, srcPort));
    uint16_t destPort = 11100 + inter;
    socket->Connect(InetSocketAddress(destAddress, destPort));
    ctlPktSend += packet->GetSize(); // 统计发送字节
    return socket->Send(packet);
  }
  else{
    // TCP
    DTag ttag;
    ttag.SetPrio(0);
    ttag.SetTimestamp(Simulator::Now ());
    ttag.SetSize(packet->GetSize());
    ttag.SetTag(tag);
    packet->AddPacketTag(ttag);

    Ptr<SinglePacketTcpClient> clientApp = CreateObject<SinglePacketTcpClient>();
    uint16_t destPort = 11100 + inter;
    clientApp->Setup(destAddress, destPort, packet);
    this->AddApplication(clientApp);
    clientApp->SetStartTime(Seconds(1.0));
    clientApp->SetStopTime(Seconds(10.0));
  }
  return false;
}

void satelliteNode::SendCheckStatus(){
  if(m_state == breakNode) return ;
  //状态检测包--0：簇首从控制器向簇内节点发送心跳包，1：地面从控制器向簇首发送心跳包
  for(uint32_t i=0; i<m_intraNodes.GetN(); i++){

    if(!_slaveMode){
      if(i==0) continue; // 如果从控制器位于簇首，则簇首不需要向自己发送心跳包
    } 
    // 创建心跳包
    ofi::HeartbeatPacket hPacket;
    // 设置心跳包的字段
    hPacket.controller_id = this->GetId();
    hPacket.subcontroller_id = m_intraNodes.Get(i)->GetId();
    Time now = Simulator::Now ();
    uint8_t nowSec = now.GetSeconds(); // 转换为秒
    hPacket.timestamp = nowSec;
    hPacket.sequence_number = ++m_heartseq;
    uint8_t info = 0;
    hPacket.status_info = info;

    // 获取目的地址
    Ptr<NetDevice> dev = m_intraNodes.Get(i)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    Ptr<Ipv4> ipv4 = m_intraNodes.Get(i)->GetObject<Ipv4>();
    uint32_t interfaceIndex = dev->GetIfIndex();
    Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

    // index是本机交换机的编号
    uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
    Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
    Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

    ofi::SDNPacket Spacket;
    Spacket.type = ofi::pktType::HEARTBEAT;
    Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
    Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
    Spacket.packet = new uint8_t[ofi::packetSize];
    std::memcpy(Spacket.packet, &hPacket, ofi::packetSize);

    const size_t len = sizeof(ofi::SDNPacket);
    uint8_t buffer[len];
    Spacket.Serialize(buffer, len);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(buffer, len);

    delete[] Spacket.packet;
    Spacket.packet = nullptr;

    if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 2)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"从控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  // << " ------heartbeat------> " << std::endl
                  // << "从控制器：" << static_cast<unsigned int>(m_SlavesControllers[i]->m_id) << std::endl
                  // << "destIP:"<< dstAddress 
                  << std::endl;
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(hPacket.controller_id)
                  << "\t目的节点ID： " << static_cast<unsigned int>(hPacket.subcontroller_id) 
                  << "\t序列号：" << static_cast<unsigned int>(hPacket.sequence_number) 
                  << "\t时间戳：" << static_cast<unsigned int>(hPacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
    }
  }
  if(m_state != breakNode) m_sendHeartbeatEvent = Simulator::Schedule(Seconds(m_interval), &satelliteNode::SendCheckStatus, this);
}

void satelliteNode::SendStatus(ofi::CheckStatusPacket csPacket){
  if(m_state == breakNode) return;
  //获取当前节点信息、流量信息
  // assert(csPacket.send_controller_id == m_intraNodes.Get(0)->GetId());
  // Ptr<Node> dest = m_intraNodes.Get(csPacket.dest_node_id);
  ofi::StatusPacket spacket;
  spacket.packet_type = (uint8_t)ofi::pktType::Status;
  spacket.src_id = this->GetId();
  spacket.dest_id = m_intraNodes.Get(0)->GetId(); //普通节点向从控制器发送
  Time now = Simulator::Now ();
  double nowSec = now.GetSeconds(); // 转换为秒
  spacket.timestamp=nowSec;

  // 获取目的地址
  Ptr<NetDevice> dev = m_intraNodes.Get(0)->GetDevice(1);
  // 获取网卡的 IPv4 接口列表
  Ptr<Ipv4> ipv4 = m_intraNodes.Get(0)->GetObject<Ipv4>();
  uint32_t interfaceIndex = dev->GetIfIndex();
  Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

  // index是本机交换机的编号
  uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
  Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
  Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

  ofi::SDNPacket Spacket;
  Spacket.type = ofi::pktType::Status;
  Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
  Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
  Spacket.packet = new uint8_t[sizeof(ofi::StatusPacket)];
  std::memcpy(Spacket.packet, &spacket, sizeof(ofi::StatusPacket));
  
  delete[] Spacket.packet;
  Spacket.packet = nullptr;

  const size_t len = sizeof(ofi::SDNPacket);
  uint8_t buffer[len];
  Spacket.Serialize(buffer, len);

  // 创建数据包并发送
  Ptr<Packet> packet = Create<Packet>(buffer, len);

  if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 2)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"普通节点：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  << std::endl;
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(spacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(spacket.dest_id)  
                << "\t时间戳：" << static_cast<unsigned int>(spacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
  }
}

void satelliteNode::SendBreakNodeInfo_normal(ofi::HeartbeatPacket csPacket){
  if(m_state != breakNode) return;
  if(!_slaveMode && this->GetId() == m_intraNodes.Get(0)->GetId()) return ;   // 当从控制器位于簇首时，从控制器故障，不发送自身故障信息包
  if(breakInfoSend == true) return;
  breakInfoSend = true;

 // 普通节点故障，将自身故障信息发送给从控制器
  ofi::BreakInfoPacket bpacket;
  bpacket.packet_type = (uint8_t)ofi::pktType::BreakInfo;
  bpacket.src_id = this->GetId();

  if(!_slaveMode)
    bpacket.dest_id = m_intraNodes.Get(0)->GetId(); // 故障信息发送给簇首从控制器
  else
    bpacket.dest_id = m_slavesnodes.Get(0)->GetId(); // 故障信息发送给地面从控制器，在地面从控制器中任选一个
  
  bpacket.break_id = this->GetId();
  Time now = Simulator::Now ();
  uint8_t nowSec = now.GetSeconds(); // 转换为秒
  bpacket.timestamp=nowSec;
  m_breakTime = nowSec;

  // 获取目的地址
   Ptr<NetDevice> dev = nullptr;
   Ptr<Ipv4> ipv4 = nullptr;
   uint32_t interfaceIndex = 0;
   Ipv4Address dstAddress;
  if(!_slaveMode){
    dev = m_intraNodes.Get(0)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    ipv4 = m_intraNodes.Get(0)->GetObject<Ipv4>();
    interfaceIndex = dev->GetIfIndex();
    dstAddress = ipv4->GetAddress(1, 0).GetLocal();
  }
  else{
    dev = m_slavesnodes.Get(0)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    ipv4 = m_slavesnodes.Get(0)->GetObject<Ipv4>();
    interfaceIndex = dev->GetIfIndex();
    dstAddress = ipv4->GetAddress(1, 0).GetLocal();
  }

  // index是本机交换机的编号
  uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
  Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
  Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

  ofi::SDNPacket Spacket;
  Spacket.type = ofi::pktType::BreakInfo;
  Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
  Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
  Spacket.packet = new uint8_t[sizeof(ofi::BreakInfoPacket)];
  std::memcpy(Spacket.packet, &bpacket, sizeof(ofi::BreakInfoPacket));

  const size_t len = sizeof(ofi::SDNPacket);
  uint8_t buffer[len];
  Spacket.Serialize(buffer, len);

  // 创建数据包并发送
  Ptr<Packet> packet = Create<Packet>(buffer, len);

  delete[] Spacket.packet;
  Spacket.packet = nullptr;

  if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 2)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"普通节点：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  << std::endl;
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(bpacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(bpacket.dest_id)  
                << "\t故障节点ID： " << static_cast<unsigned int>(bpacket.break_id)
                << "\t时间戳：" << static_cast<unsigned int>(bpacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
  }
}

void satelliteNode::DealWithHeartBeat(ofi::HeartbeatPacket packet, Ipv4Address srcAddress){
//   Ptr<ofi::SlaveController> temp = DynamicCast<ofi::Controller>(m_controller);    // 本节点绑定的控制器指针

  Time now = Simulator::Now ();
  m_HeartBeat = now.GetSeconds();
  m_disconnect.clear();
  
  if(_slaveMode && m_state == satellite){ // 当从控制器位于地面站时，簇首节点首先接收心跳包，然后转发给簇内成员节点
    if(this->GetId() == m_intraNodes.Get(0)->GetId()){ // 判断簇首是否接收到心跳包
      ForwardHeartbeatToCluster(packet);
    }
  }
  // if(_CtrlInfoOutput) std::cout << "node " << this->GetId() << " DealWithHeartBeat\tm_HeartBeat："<< m_HeartBeat << std::endl;
  if(m_checkFlag == false && (m_state == slaveController || m_state == mutilController)){
    m_checkFlag = true;
    Simulator::Schedule(Seconds(m_interval), &satelliteNode::SendCheckStatus, this);
  }

  // std::cout << "id:" << this->GetId() << "\tpacket_ID:" << static_cast<unsigned int>(packet.controller_id) 
  //           << "\tmaster_ID:" << m_masterID
  //           << std::endl;
  // if((packet.controller_id == 0 || packet.controller_id == 1) 
  //     && (m_masterID != 0 && m_masterID != 1)){
    
  //   // if(m_MasterHeartBeat.size() != 0) std::cout << "last time:" << now.GetSeconds() - m_MasterHeartBeat[m_MasterHeartBeat.size()-1] << std::endl;
  //   // std::cout << "heartbeat size:" << m_MasterHeartBeat.size() << std::endl;
  //   if(m_TimeHeartBeat.size() == 0 || now.GetSeconds() - m_TimeHeartBeat[m_TimeHeartBeat.size()-1] <= m_interval*1.1){
  //     m_TimeHeartBeat.push_back(now.GetSeconds());
  //   }
  //   else m_TimeHeartBeat.clear();
  //   std::cout << "id:" << this->GetId() << "\tm_TimeHeartBeatSize:" << m_TimeHeartBeat.size() 
  //           << std::endl;

  //   if(m_TimeHeartBeat.size() >= 3){
  //     // std::cout << "id:" << m_node->GetId() << " 尝试发送恢复通告信息" << std::endl;
  //     SendRecovery();
  //     m_masterID = packet.controller_id;
  //     m_TimeHeartBeat.clear();
  //   }
  // }

}

void satelliteNode::DealWithIdentity(ofi::IdentityPacket packet, Ipv4Address srcAddress){
  // 获取当前仿真时间
  Time currentTime = Simulator::Now();
  // 将时间转换为秒
  double currentTimeInSeconds = currentTime.GetSeconds();
  if(currentTimeInSeconds <= packet.timestamp + packet.expiration_period){
    // 替换主控制器信息
    m_masterID = packet.new_master_controller_id;
    if(_CtrlInfoOutput) std::cout << "从控制器 id:" << static_cast<unsigned int>(this->GetId()) 
                                  << "\t更新主控制器信息，主控制器id更新为：" << static_cast<unsigned int>(m_masterID) << std::endl;
  }
}

void satelliteNode::DealWithDisconnect(ofi::DisconnectPacket packet, Ipv4Address srcAddress){
  if(_CtrlInfoOutput) std::cout << "从控制器 id:" << static_cast<unsigned int>(this->GetId())
                                << "\ttoMasterFlag:" << m_toMasterflag 
                                << "\tdisconectsize:" << m_disconnect.size()
                                << endl; 
  if(m_toMasterflag == true){
    m_disconnect.clear();
    return;
  }
  m_disconnect.insert(packet.send_controller_id);
  if(m_disconnect.size() > m_slavesnodes.GetN()/2){
    m_toMasterflag = true;
    // 收到一半以上从控制器的失联通告
    // 选举簇内卫星最少的从控制器成为主控
    // 这里简化将本节点设置成为主控
    // 主控制器节点
    m_disconnect.clear();
    m_state = mutilController;
    // m_master = CreateObject<ns3::ofi::MasterController> ();
    // for(uint32_t i=1; i<this->GetNDevices(); i++){
    //   Ptr<OpenFlowSwitchNetDevice> temp = DynamicCast<OpenFlowSwitchNetDevice>(this->GetDevice(i));
    //   temp->SetMasterController(m_master);
    // }

    // for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
    //   if(m_slavesnodes.Get(i)->GetId() != this->GetId()){
    //     for(uint32_t j=1; j<m_slavesnodes.Get(i)->GetNDevices(); j++){
    //       Ptr<OpenFlowSwitchNetDevice> temp = DynamicCast<OpenFlowSwitchNetDevice>(m_slavesnodes.Get(i)->GetDevice(j));
    //       temp->SetMasterController(m_master);
    //     }
    //   }
    // }

    this->StartRecvPacket_master();
    this->SendIdentity();
    this->SendHeartbeat();
    std::cout << "迁移临时主控制器，统计路由收敛时间" << std::endl;
    rouReconvergence();
  }
}

void 
satelliteNode::DealWithRecovery_slave(){
  // 销毁m_master
  if(m_state == mutilController){
    // while(!m_master->m_complete) ;
    if(_CtrlInfoOutput) std::cout << "主控制器" << static_cast<unsigned int>(this->GetId()) << "销毁！" << std::endl;
    m_master->~MasterController();
    m_heartseq = 0;
    m_sendHeartbeatEvent.Cancel();   // 取消心跳包发送句柄
    for(auto iter = m_RecvPktsocket_master.begin(); iter != m_RecvPktsocket_master.end(); iter ++){
      (*iter)->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());  // 监听socket设置为空回调
    }
    m_master = nullptr;
    m_state = slaveController;
  }

  // for(uint32_t i=1; i<this->GetNDevices(); i++){
  //   Ptr<OpenFlowSwitchNetDevice> temp = DynamicCast<OpenFlowSwitchNetDevice>(this->GetDevice(i));
  //   temp->DeleteMasterController();
  // }
}

// void 
// satelliteNode::StartRecvPacket_slave(){
//   HeartBeatShedule();

//   for(uint32_t i=1; i<this->GetNDevices(); i++){
//     Ptr<Socket> socket = Socket::CreateSocket(this, UdpSocketFactory::GetTypeId());
//     uint16_t port = 11100 + i;
//     socket->Bind(InetSocketAddress(this->GetObject<Ipv4>()->GetAddress(i, 0).GetLocal(), port));
//     socket->SetRecvCallback(MakeCallback(&satelliteNode::RecvPacketAction, this));
//     m_RecvPktsocket_slave.push_back(socket);
//   }
// }

void satelliteNode::StartRecvPacket_sate(){
  // HeartBeatShedule();
  for(uint32_t i=1; i<this->GetNDevices(); i++){
    if(!_tranProc){
      // UDP
      Ptr<Socket> socket = Socket::CreateSocket(this, UdpSocketFactory::GetTypeId());
      uint16_t port = 11100 + i;
      socket->Bind(InetSocketAddress(this->GetObject<Ipv4>()->GetAddress(i, 0).GetLocal(), port));
      socket->SetRecvCallback(MakeCallback(&satelliteNode::RecvPacketAction, this));
      m_RecvPktsocket_master.push_back(socket);
    }
    else{
      // TCP
      uint16_t port = 11100 + i;
      Ptr<SinglePacketTcpServer> serverApp = CreateObject<SinglePacketTcpServer>();
      serverApp->Setup(port);
      serverApp->SetPacketReceiveCallback(MakeCallback(&satelliteNode::RecvPacketActionTCP, this));
      this->AddApplication(serverApp);
      serverApp->SetStartTime(Seconds(0.0));
      serverApp->SetStopTime(Seconds(totalTimeStep));
    }
  }
}

void satelliteNode::HeartBeatShedule(){
  int numcall = 10;
  // Time now = Simulator::Now ();
  // m_HeartBeat = now.GetSeconds();
  for(int i=1; i<=numcall; i++){
    Time next = Seconds(i*m_interval*3);
    Simulator::Schedule(next, &satelliteNode::checkHeartBeat, this);
  }
}

void satelliteNode::SyncToMaster() {
  if(m_slaveBreakInfo == true) return;
  m_slaveBreakInfo = false;

  // 向主控制器发送故障信息
  ofi::BreakInfoPacket bpacket;
  bpacket.packet_type = (uint8_t)ofi::pktType::BreakInfo;
  bpacket.src_id = this->GetId();
  if(m_master_nodes.GetN() <= m_masterID){
    cout << "node " << this->GetId() << "\tmasterID " << m_masterID << " 无法获取主控制器信息！" << endl;
    return ;
  }
  bpacket.dest_id = m_master_nodes.Get(m_masterID)->GetId();
  bpacket.break_id = m_intraNodes.Get(0)->GetId(); 
  // if(_slaveMode){
  //   // 故障节点为地面从控制器
  //   bpacket.break_id = m_slavesnodes.Get(0)->GetId();
  // }
  // else{
  //   // 故障节点为簇首从控制器
  //   bpacket.break_id = m_intraNodes.Get(0)->GetId();  
  // }
  Time now = Simulator::Now ();
  uint8_t nowSec = now.GetSeconds(); // 转换为秒
  bpacket.timestamp=nowSec;
  m_breakTime = nowSec;

  // 获取目的地址
  Ptr<NetDevice> dev = m_master_nodes.Get(m_masterID)->GetDevice(1);
  // 获取网卡的 IPv4 接口列表
  Ptr<Ipv4> ipv4 = m_master_nodes.Get(m_masterID)->GetObject<Ipv4>();
  uint32_t interfaceIndex = dev->GetIfIndex();
  Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

  // index是本机交换机的编号
  uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
  Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
  Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

  ofi::SDNPacket Spacket;
  Spacket.type = ofi::pktType::BreakInfo;
  Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
  Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
  Spacket.packet = new uint8_t[sizeof(ofi::BreakInfoPacket)];
  std::memcpy(Spacket.packet, &bpacket, sizeof(ofi::BreakInfoPacket));

  const size_t len = sizeof(ofi::SDNPacket);
  uint8_t buffer[len];
  Spacket.Serialize(buffer, len);

  // 创建数据包并发送
  Ptr<Packet> packet = Create<Packet>(buffer, len);

  delete[] Spacket.packet;
  Spacket.packet = nullptr;

  if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 2)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"普通节点：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  << std::endl;
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(bpacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(bpacket.dest_id)  
                << "\t故障节点ID： " << static_cast<unsigned int>(bpacket.break_id)
                << "\t时间戳：" << static_cast<unsigned int>(bpacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
  }

}

void satelliteNode::ForwardHeartbeatToCluster(ofi::HeartbeatPacket hPacket){
  if(m_state == breakNode) return;
    // 簇首节点进行转发到簇内成员节点，都为普通卫星节点
    for(uint32_t i=1; i<m_intraNodes.GetN(); i++){ //索引从0开始不向簇首发送
      // 获取目的地址
      Ptr<NetDevice> dev = m_intraNodes.Get(i)->GetDevice(1);
      // 获取网卡的 IPv4 接口列表
      Ptr<Ipv4> ipv4 = m_intraNodes.Get(i)->GetObject<Ipv4>();
      uint32_t interfaceIndex = dev->GetIfIndex();
      Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

      // index是本机交换机的编号
      uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
      Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
      Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

      ofi::SDNPacket Spacket;
      Spacket.type = ofi::pktType::HEARTBEAT;
      Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
      Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
      Spacket.packet = new uint8_t[ofi::packetSize];
      std::memcpy(Spacket.packet, &hPacket, ofi::packetSize);

      const size_t len = sizeof(ofi::SDNPacket);
      uint8_t buffer[len];
      Spacket.Serialize(buffer, len);

      // 创建数据包并发送
      Ptr<Packet> packet = Create<Packet>(buffer, len);

      delete[] Spacket.packet;
      Spacket.packet = nullptr;

      if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 2)){  
        if(_CtrlInfoOutput) {
          std::cout << "************************ " << std::endl 
                    <<"簇首节点：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                    // << " ------heartbeat------> " << std::endl
                    // << "从控制器：" << static_cast<unsigned int>(m_SlavesControllers[i]->m_id) << std::endl
                    // << "destIP:"<< dstAddress 
                    << std::endl;
          std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                    << "\t从控制器ID：" << static_cast<unsigned int>(hPacket.controller_id)
                    << "\t目的节点ID： " << static_cast<unsigned int>(hPacket.subcontroller_id) 
                    << "\t序列号：" << static_cast<unsigned int>(hPacket.sequence_number) 
                    << "\t时间戳：" << static_cast<unsigned int>(hPacket.timestamp) << std::endl
                    << "************************ " << std::endl;
        }
      }
    }
}
void satelliteNode::checkHeartBeat(){
  Time now = Simulator::Now ();
  double curr = now.GetSeconds();

  if((curr - m_HeartBeat) >= m_interval * 3){
    // 超过三个周期未收到心跳包
    if(_CtrlInfoOutput) std::cout << "node " << this->GetId() << " 三个周期内未收到心跳包，curr:" << curr << "\tm_HeartBeat："<< m_HeartBeat << std::endl;
    // *m_toMasterflag = false;
    if(this->m_state != satellite) SendDisconnect(); // 由于地面链路稳定，地面从控制器不会出现收不到心跳包
    else SyncToMaster(); // 普通节点将故障从控制器节点发送给主控制器--1：地面站为从控制器时，簇首和簇成员节点为普通节点 0：簇首为从控制器时，簇成员节点为普通节点
  }
}

void satelliteNode::SendDisconnect(){
  for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
    // if(m_slaves_controller_info[i].global_id == m_id) continue;
    if(m_slavesnodes.Get(i)->GetId() == this->GetId()) continue;
    // 创建失联通告包--向其他从控制器发送失联通告包
    ofi::DisconnectPacket dPacket;
    // 设置字段
    dPacket.packet_type = (uint8_t)ofi::pktType::Disconnect;
    dPacket.send_controller_id = this->GetId();
    dPacket.dest_controller_id = m_slavesnodes.Get(i)->GetId();
    Time now = Simulator::Now ();
    uint8_t nowSec = now.GetSeconds(); // 转换为秒
    dPacket.timestamp = nowSec;
    dPacket.fau_info = 0;

    // 获取目的地址
    Ptr<NetDevice> dev = m_slavesnodes.Get(i)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    Ptr<Ipv4> ipv4 = m_slavesnodes.Get(i)->GetObject<Ipv4>();
    uint32_t interfaceIndex = dev->GetIfIndex();
    Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

    // index是本机交换机的编号
    uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
    Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
    Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

    ofi::SDNPacket Spacket;
    Spacket.type = ofi::pktType::Disconnect;
    Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
    Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
    Spacket.packet = new uint8_t[ofi::packetSize];
    std::memcpy(Spacket.packet, &dPacket, ofi::packetSize);

    const size_t len = sizeof(ofi::SDNPacket);
    uint8_t buffer[len];
    Spacket.Serialize(buffer, len);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(buffer, len);

    
    delete[] Spacket.packet;
    Spacket.packet = nullptr;

    if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 1)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"从控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  // << " ------heartbeat------> " << std::endl
                  // << "从控制器：" << static_cast<unsigned int>(m_SlavesControllers[i]->m_id) << std::endl
                  // << "destIP:"<< dstAddress 
                  << std::endl;
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(dPacket.send_controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(dPacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(dPacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(dPacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
    }
  }
}

void satelliteNode::SendRecovery(){
    // 创建失联恢复包
    ofi::RecoveryPacket rPacket;
    // 设置字段
    rPacket.packet_type = (uint8_t)ofi::pktType::Recovery;
    rPacket.send_controller_id = this->GetId();
    rPacket.dest_controller_id = m_masterID;
    Time now = Simulator::Now ();
    uint8_t nowSec = now.GetSeconds(); // 转换为秒
    rPacket.timestamp = nowSec;
    rPacket.fau_info = 0;

    Ptr<Node> master;
    if(m_masterID < m_master_nodes.GetN()) master = m_master_nodes.Get(m_masterID);
    else{
      for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
        if(m_masterID == m_slavesnodes.Get(i)->GetId()){
          master = m_slavesnodes.Get(i);
          break;
        }
      }
    }

    // 获取目的地址
    Ptr<NetDevice> dev = master->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    Ptr<Ipv4> ipv4 = master->GetObject<Ipv4>();
    uint32_t interfaceIndex = dev->GetIfIndex();
    Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

    // index是本机交换机的编号
    uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
    Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
    Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

    ofi::SDNPacket Spacket;
    Spacket.type = ofi::pktType::Recovery;
    Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
    Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
    Spacket.packet = new uint8_t[ofi::packetSize];
    std::memcpy(Spacket.packet, &rPacket, ofi::packetSize);

    const size_t len = sizeof(ofi::SDNPacket);
    uint8_t buffer[len];
    Spacket.Serialize(buffer, len);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(buffer, len);

    delete[] Spacket.packet;
    Spacket.packet = nullptr;

    if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 1)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"从控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  // << " ------heartbeat------> " << std::endl
                  // << "从控制器：" << static_cast<unsigned int>(m_SlavesControllers[i]->m_id) << std::endl
                  // << "destIP:"<< dstAddress 
                  << std::endl;
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(rPacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(rPacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(rPacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(rPacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
      // Time now = Simulator::Now ();
      // double curr = now.GetSeconds() + 3;
      Simulator::Schedule(Seconds (m_interval), &satelliteNode::DealWithRecovery_slave, this);
      // DealWithRecovery();
    }
}

void 
satelliteNode::DealWithActivation(ofi::ActivationPacket packet, Ipv4Address srcAddress){
  // 备份主控制器收到激活包
  // 激活包是由主控发送给备份主控的
  // 可能还会有其他情况，由从控制器发送给主控，暂时不考虑
  if(packet.send_controller_id == m_master_nodes.Get(m_masterID)->GetId()){
    // 主控发送给备份主控
    // 本控制器切换为主控制器
    ToMaster();
  }

}

void satelliteNode::DealWithRecovery_master(ofi::RecoveryPacket packet, Ipv4Address srcAddress){
  m_RecoveryNum ++;
  if(m_RecoveryNum >= m_slavesnodes.GetN()){
    m_RecoveryNum = 0;

    if(_CtrlInfoOutput) std::cout << "临时主控制器" << static_cast<unsigned int>(this->GetId()) << "收到所有恢复通告！" << std::endl;
    m_sendHeartbeatEvent.Cancel();   // 取消心跳包发送句柄
    for(auto iter = m_RecvPktsocket_master.begin(); iter != m_RecvPktsocket_master.end(); iter ++){
      (*iter)->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());  // 监听socket设置为空回调
    }
    // 向地面主控制器发送失联期间数据
  }
}

void 
satelliteNode::DealWithBreakInfo(ofi::BreakInfoPacket packet){
  Time now = Simulator::Now ();
  double nowSec = now.GetSeconds(); // 转换为秒
  uint32_t breakId = static_cast<unsigned int>(packet.break_id);
  double senceTime = nowSec;
  if(m_breakInfo[breakId] <= 0) m_breakInfo[breakId] = senceTime;
  // std::cout << "breakID:" << breakId << "\tsenceTime:" << senceTime << "\ttotal:" << m_breakInfo[breakId] << std::endl;
  if(_BreakDetect) std::cout << "breakID:" << breakId << "\tsenceTime:" << senceTime << std::endl;
  m_breakPktNum ++; 
  
  // 判断该节点是否是从控制器
  if(breakSID == (int)breakId) return ;
  for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
    if(m_slavesnodes.Get(i)->GetId() == breakId){
      // std::lock_guard<std::mutex> lock(smutex);  // 锁定互斥量
      // cout << "DealWithBreakInfo 尝试锁定mutex" << endl;
      smutex.lock();
      breakSID = breakId;
      smutex.unlock();
      // cout << "DealWithBreakInfo 解锁" << endl;
    }
  }
}

void 
satelliteNode::SendBreakNodeInfo(uint32_t nodeID){
  // if(m_slavesnodes.Get(idx)->GetId() != nodeID) return;

  ofi::BreakInfoPacket bpacket;
  bpacket.packet_type = (uint8_t)ofi::pktType::BreakInfo;
  bpacket.src_id = this->GetId();
  bpacket.dest_id = m_masterID;
  bpacket.break_id = nodeID;
  Time now = Simulator::Now ();
  double nowSec = now.GetSeconds(); // 转换为秒
  bpacket.timestamp=nowSec;

    Ptr<Node> master;
    if(m_masterID < m_master_nodes.GetN()) master = m_master_nodes.Get(m_masterID);
    else{
      for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
        if(m_masterID == m_slavesnodes.Get(i)->GetId()){
          master = m_slavesnodes.Get(i);
          break;
        }
      }
    }
  
  // cout << "nodeID:" << this->GetId() << "\tbreakNodeID:" << nodeID << endl;
  // cout << "masterID:" << m_masterID << "\tmasterSize:" << m_master_nodes.GetN() << "\tslaveSize:" << m_slavesnodes.GetN() << endl;
  // 获取目的地址
  Ptr<NetDevice> dev = master->GetDevice(1);
  // 获取网卡的 IPv4 接口列表
  Ptr<Ipv4> ipv4 = master->GetObject<Ipv4>();
  uint32_t interfaceIndex = dev->GetIfIndex();
  Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

  // index是本机交换机的编号
  uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
  Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
  Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

  ofi::SDNPacket Spacket;
  Spacket.type = ofi::pktType::BreakInfo;
  Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
  Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
  Spacket.packet = new uint8_t[sizeof(ofi::BreakInfoPacket)];
  std::memcpy(Spacket.packet, &bpacket, sizeof(ofi::BreakInfoPacket));

  const size_t len = sizeof(ofi::SDNPacket);
  uint8_t buffer[len];
  Spacket.Serialize(buffer, len);

  // 创建数据包并发送
  Ptr<Packet> packet = Create<Packet>(buffer, len);

  delete[] Spacket.packet;
  Spacket.packet = nullptr;

  if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 0)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"从控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  << std::endl;
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(bpacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(bpacket.dest_id)  
                << "\t故障节点ID： " << static_cast<unsigned int>(bpacket.break_id)
                << "\t时间戳：" << static_cast<unsigned int>(bpacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
  }
}

// void 
// satelliteNode::DealWithStatus(ofi::StatusPacket spacket){
//   uint32_t src_id = static_cast<unsigned int>(spacket.src_id);
//   if(m_statusSets.find(src_id) == m_statusSets.end()){
//     m_statusSets.insert(src_id);
//   }
//   else{
//     // 找到没有收到状态包的节点，向主控制器汇报信息
//     for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
//       if(m_slavesnodes.Get(i)->GetId() == this->GetId()) continue;
//       uint32_t src = m_slavesnodes.Get(i)->GetId();
//       if(m_statusSets.find(src) == m_statusSets.end()){
//         // 没找到状态包，说明该节点故障，向主控制器汇报
//         SendBreakNodeInfo(i, src, spacket.timestamp);
//       }
//     }
//     m_statusSets.clear();
//     m_statusSets.insert(src_id);
//   } 
// }

void 
satelliteNode::StartRecvPacket_master(){
  for(uint32_t i=1; i<this->GetNDevices(); i++){
    if(!_tranProc){
      // UDP
      Ptr<Socket> socket = Socket::CreateSocket(this, UdpSocketFactory::GetTypeId());
      uint16_t port;
      if(this->GetId() == 0 || this->GetId() == 1) port = 11100 + i;
      else port = 12000 + i;
      socket->Bind(InetSocketAddress(this->GetObject<Ipv4>()->GetAddress(i, 0).GetLocal(), port));
      socket->SetRecvCallback(MakeCallback(&satelliteNode::RecvPacketAction, this));
      m_RecvPktsocket_master.push_back(socket);
    }
    else{
      // TCP
      uint16_t port;
      if(this->GetId() == 0 || this->GetId() == 1) port = 11100 + i;
      else port = 12000 + i;
      Ptr<SinglePacketTcpServer> serverApp = CreateObject<SinglePacketTcpServer>();
      serverApp->Setup(port);
      serverApp->SetPacketReceiveCallback(MakeCallback(&satelliteNode::RecvPacketActionTCP, this));
      this->AddApplication(serverApp);
      serverApp->SetStartTime(Seconds(0.0));
      serverApp->SetStopTime(Seconds(totalTimeStep));
    }
  }
}

void satelliteNode::ToMaster(){
  // 其余设置需要时添加
  m_state = masterController;
  m_masterID = this->GetId();
  // m_backup_master_controller_info.type = ofi::node_type::BackupMasterController;
  // 发送身份包，通告自身身份
  // 需要保证本控制器已经知道从控制器的ip等信息
  SendIdentity();
  // 发送完身份包后，开始根据周期发送心跳包
  SendHeartbeat();
}

void satelliteNode::ToBackup(int masterID){
  // 其余设置需要时添加
  m_masterID = masterID;
  // 取消心跳包发送事件
  m_sendHeartbeatEvent.Cancel();
  // 向备份主控制器发送激活包
  this->SendActivation();
}


void satelliteNode::SendIdentity(){
  for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
    // 创建身份包
    ofi::IdentityPacket identityPacket;
    // 设置字段
    identityPacket.packet_type = (uint8_t)ofi::pktType::IDENTITY;
    identityPacket.new_master_controller_id = this->GetId();
    identityPacket.dest_controller_id = m_slavesnodes.Get(i)->GetId();
    Time now = Simulator::Now ();
    uint8_t nowSec = now.GetSeconds(); // 转换为秒
    identityPacket.timestamp = nowSec;
    identityPacket.expiration_period = 20; // 20s

    // 获取目的地址
    Ptr<NetDevice> dev = m_slavesnodes.Get(i)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    Ptr<Ipv4> ipv4 = m_slavesnodes.Get(i)->GetObject<Ipv4>();
    uint32_t interfaceIndex = dev->GetIfIndex();
    Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

    // index是本机交换机的编号
    uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
    Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
    Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

    ofi::SDNPacket Spacket;
    Spacket.type = ofi::pktType::IDENTITY;
    Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
    Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
    Spacket.packet = new uint8_t[ofi::packetSize];
    std::memcpy(Spacket.packet, &identityPacket, ofi::packetSize);

    const size_t len = sizeof(ofi::SDNPacket);
    uint8_t buffer[len];
    Spacket.Serialize(buffer, len);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(buffer, len);

    delete[] Spacket.packet;
    Spacket.packet = nullptr;

    if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 0)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"主控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  // << " ------heartbeat------> " << std::endl
                  // << "从控制器：" << static_cast<unsigned int>(m_SlavesControllers[i]->m_id) << std::endl
                  // << "destIP:"<< dstAddress 
                  << std::endl;
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                  << "\t新主控制器ID：" << static_cast<unsigned int>(identityPacket.new_master_controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(identityPacket.dest_controller_id) 
                  << "\t生存时间：" << static_cast<unsigned int>(identityPacket.expiration_period) 
                  << "\t时间戳：" << static_cast<unsigned int>(identityPacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
    }
  }
}

void
satelliteNode::SendHeartbeat(){
  for(uint32_t i=0; i<m_slavesnodes.GetN(); i++){
   // 创建心跳包
    ofi::HeartbeatPacket heartbeatPacket;
    // 设置心跳包的字段
    heartbeatPacket.controller_id = this->GetId();
    heartbeatPacket.subcontroller_id = m_slavesnodes.Get(i)->GetId();
    Time now = Simulator::Now ();
    uint8_t nowSec = now.GetSeconds(); // 转换为秒
    heartbeatPacket.timestamp = nowSec;
    heartbeatPacket.sequence_number = ++m_heartseq;
    uint8_t info = 0;
    heartbeatPacket.status_info = info;

    // 获取目的地址
    Ptr<NetDevice> dev = m_slavesnodes.Get(i)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    Ptr<Ipv4> ipv4 = m_slavesnodes.Get(i)->GetObject<Ipv4>();
    uint32_t interfaceIndex = dev->GetIfIndex();
    Ipv4Address dstAddress = ipv4->GetAddress(1, 0).GetLocal();

    // index是本机交换机的编号
    uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);
    Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
    Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

    ofi::SDNPacket Spacket;
    Spacket.type = ofi::pktType::HEARTBEAT;
    // Spacket.packet_size = sizeof(HeartbeatPacket);
    Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
    Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
    Spacket.packet = new uint8_t[ofi::packetSize];
    std::memcpy(Spacket.packet, &heartbeatPacket, ofi::packetSize);

    const size_t len = sizeof(ofi::SDNPacket);
    uint8_t buffer[len];
    Spacket.Serialize(buffer, len);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(buffer, len);

    delete[] Spacket.packet;
    Spacket.packet = nullptr;


    if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 0)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"主控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  // << " ------heartbeat------> " << std::endl
                  // << "从控制器：" << static_cast<unsigned int>(m_SlavesControllers[i]->m_id) << std::endl
                  // << "destIP:"<< dstAddress 
                  << std::endl;
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                  << "\t主控制器ID：" << static_cast<unsigned int>(heartbeatPacket.controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(heartbeatPacket.subcontroller_id) 
                  << "\t序列号：" << static_cast<unsigned int>(heartbeatPacket.sequence_number) 
                  << "\t时间戳：" << static_cast<unsigned int>(heartbeatPacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
    }
  }

  m_sendHeartbeatEvent = Simulator::Schedule(Seconds(m_interval), &satelliteNode::SendHeartbeat, this);
}

void satelliteNode::SendActivation(){
    // 创建激活包
    ofi::ActivationPacket aPacket;
    // 设置字段
    aPacket.packet_type = (uint8_t)ofi::pktType::ACTIVATION;
    aPacket.send_controller_id = this->GetId();
    aPacket.dest_controller_id = m_masterID;
    Time now = Simulator::Now ();
    uint8_t nowSec = now.GetSeconds(); // 转换为秒
    aPacket.timestamp = nowSec;
    aPacket.fau_info = 0;

    // 获取目的地址
    Ptr<NetDevice> dev = m_master_nodes.Get(m_masterID)->GetDevice(1);
    // 获取网卡的 IPv4 接口列表
    Ptr<Ipv4> ipv4 = m_master_nodes.Get(m_masterID)->GetObject<Ipv4>();
    uint32_t interfaceIndex = dev->GetIfIndex();
    // 获取 IPv4 地址
    Ipv4Address dstAddress = ipv4->GetAddress(interfaceIndex, 0).GetLocal();
    // std::cout << "interface:" << interfaceIndex << "\tIPv4 Address: " << dstAddress << std::endl;
    // Mac48Address mac = Mac48Address::ConvertFrom(dev->GetAddress());
    // std::cout << "mac addr:" << mac << " mac size:" << sizeof(mac)<< std::endl;

    // index是本机交换机的编号
    uint32_t index = 1; //Ipv4GlobalRoutingHelper::SDSNCaculateFlow(this->GetId(), dstAddress);

    Ptr<Ipv4> srcipv4 = this->GetObject<Ipv4>();
    Ipv4Address srcaddr = srcipv4->GetAddress(index, 0).GetLocal();

    ofi::SDNPacket Spacket;
    Spacket.type = ofi::pktType::ACTIVATION;
    Spacket.src = Mac48Address::ConvertFrom(this->GetDevice(index)->GetAddress());
    Spacket.dest = Mac48Address::ConvertFrom(dev->GetAddress());
    Spacket.packet = new uint8_t[ofi::packetSize];
    std::memcpy(Spacket.packet, &aPacket, ofi::packetSize);
    const size_t len = sizeof(ofi::SDNPacket);
    uint8_t buffer[len];
    Spacket.Serialize(buffer, len);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(buffer, len);

    delete[] Spacket.packet;
    Spacket.packet = nullptr;

    // if(SendPacket(packet, m_node->GetObject<Ipv4>()->GetAddress(m_node->GetNDevices()-2, 0).GetLocal(), dstAddress, interfaceIndex)){
    if(SendPacket(packet, srcaddr, dstAddress, interfaceIndex, 0)){
      if(_CtrlInfoOutput) {
        std::cout << "************************ " << std::endl 
                  <<"主控制器：" << static_cast<unsigned int>(this->GetId()) << " Sent packet "
                  << std::endl;
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(Spacket.type) 
                  << "\t主控制器ID：" << static_cast<unsigned int>(aPacket.send_controller_id)
                  << "\t备份主控制器ID： " << static_cast<unsigned int>(aPacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(aPacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(aPacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
    }
}

void 
satelliteNode::RecvPacketAction(Ptr<Socket> socket) {
  Ptr<Packet> packet;

  Address from;
  // Address from;
  if ((packet = socket->RecvFrom(from))) {
    // 获取源地址
    InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
    Ipv4Address sourceAddress = inetFrom.GetIpv4();
    // std::cout << "SlaveController Received packet from " << sourceAddress << std::endl;
    uint8_t buffer[packet->GetSize()];
    packet->CopyData(buffer, packet->GetSize());

    DTag tag;
    packet->RemovePacketTag(tag);
    uint32_t ttag = tag.GetTag();
    // 本节点接收到的控制信息累加
    ctlPktRecv += packet->GetSize();    // 单位Byte，字节
    if(tag.GetHops() != 0) ctlPktRecvOver += packet->GetSize()*tag.GetHops();
    else ctlPktRecvOver += packet->GetSize()*2.25;
    if(ttag == 0){
      ctlPktRecv0 += packet->GetSize();    // 单位Byte，字节
      if(tag.GetHops() != 0) ctlPktRecvOver0 += packet->GetSize()*tag.GetHops();
      else ctlPktRecvOver0 += packet->GetSize()*2.25;
    }
    else if(ttag == 1){
      ctlPktRecv1 += packet->GetSize();    // 单位Byte，字节
      if(tag.GetHops() != 0) ctlPktRecvOver1 += packet->GetSize()*tag.GetHops();
      else ctlPktRecvOver1 += packet->GetSize()*2.25;
    }
    else if(ttag == 2){
      ctlPktRecv2 += packet->GetSize();    // 单位Byte，字节
      if(tag.GetHops() != 0) ctlPktRecvOver2 += packet->GetSize()*tag.GetHops();
      else ctlPktRecvOver2 += packet->GetSize()*2.25;
    }
    ofi::pktType type; 
    std::memcpy(&type, buffer, sizeof(type));

    if(_CtrlInfoOutput) 
      std::cout //<< "Received packet from " << Mac48Address::ConvertFrom (src) << " to " << myaddr << " 协议类型："<< protocol
                << "************************ " << std::endl 
                << GetStateString(m_state) << "id:" << static_cast<unsigned int>(this->GetId()) << " Received packet" << std::endl;
                // << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) << std::endl;
                // << "************" << std::endl;
    if(type == ofi::pktType::HEARTBEAT){
      ofi::HeartbeatPacket hpacket;
      std::memcpy(&hpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::HeartbeatPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type)
                  << "\t主控制器ID：" << static_cast<unsigned int>(hpacket.controller_id) 
                  << "\t从控制器ID： " << static_cast<unsigned int>(hpacket.subcontroller_id) 
                  << "\t序列号：" << static_cast<unsigned int>(hpacket.sequence_number) 
                  << "\t时间戳：" << static_cast<unsigned int>(hpacket.timestamp) << std::endl
                  << "************************" << std::endl;
      // if(m_state == slaveController || m_state == mutilController) 
      DealWithHeartBeat(hpacket, sourceAddress);  // 从控制器
      if(m_state == breakNode) SendBreakNodeInfo_normal(hpacket);  // 普通故障卫星
    }
    else if(type == ofi::pktType::IDENTITY){
      ofi::IdentityPacket ipacket;
      std::memcpy(&ipacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::IdentityPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type)
                  << "\t新主控制器ID：" << static_cast<unsigned int>(ipacket.new_master_controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(ipacket.dest_controller_id) 
                  << "\t生存时间：" << static_cast<unsigned int>(ipacket.expiration_period) 
                  << "\t时间戳：" << static_cast<unsigned int>(ipacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == slaveController || m_state == mutilController) DealWithIdentity(ipacket, sourceAddress); // 从控制器接收新主控制器发送的身份包
    }
    else if(type == ofi::pktType::Disconnect){
      ofi::DisconnectPacket dpacket;
      std::memcpy(&dpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::DisconnectPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(dpacket.send_controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(dpacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(dpacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(dpacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == slaveController || m_state == mutilController) DealWithDisconnect(dpacket, sourceAddress);
    }
    else if(type == ofi::pktType::ACTIVATION){
      ofi::ActivationPacket apacket;
      std::memcpy(&apacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::ActivationPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(apacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(apacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(apacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(apacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == masterController) DealWithActivation(apacket, sourceAddress);
    }
    else if(type == ofi::pktType::Recovery){
      ofi::RecoveryPacket rpacket;
      std::memcpy(&rpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::RecoveryPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(rpacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(rpacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(rpacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(rpacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == mutilController) DealWithRecovery_master(rpacket, sourceAddress);
    }
    else if(type == ofi::pktType::CheckSatus){
      ofi::CheckStatusPacket checkspacket;
      std::memcpy(&checkspacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::CheckStatusPacket));
      if(_CtrlInfoOutput){
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(checkspacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(checkspacket.dest_node_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(checkspacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(checkspacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
      // if(m_state == satellite) SendStatus(checkspacket);
      // if(m_state == breakNode) SendBreakNodeInfo_normal(checkspacket);
    }
    else if(type == ofi::pktType::Status){
      ofi::StatusPacket spacket;
      std::memcpy(&spacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::StatusPacket));
      if(_CtrlInfoOutput){
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(spacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(spacket.dest_id)  
                << "\t时间戳：" << static_cast<unsigned int>(spacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
      // if(m_state == slaveController || m_state == mutilController) DealWithStatus(spacket);
    }
    else if(type == ofi::pktType::BreakInfo){
      ofi::BreakInfoPacket bpacket;
      std::memcpy(&bpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::BreakInfoPacket));
      if(_CtrlInfoOutput){
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(bpacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(bpacket.dest_id)  
                << "\t故障节点ID： " << static_cast<unsigned int>(bpacket.break_id)
                << "\t时间戳：" << static_cast<unsigned int>(bpacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
      if(m_state == masterController) DealWithBreakInfo(bpacket);  
      else if(m_state == slaveController || m_state == mutilController) 
      {
        SendBreakNodeInfo(static_cast<unsigned int>(bpacket.break_id));
        // + 从控制器检测到簇内节点故障后，重计算簇内路由表+协同路由表 行为
        // RecomputeRouteInfo();
      }
    }
  }
}

void 
satelliteNode::RecvPacketActionTCP(Ptr<Packet> packet, Address from) {
  // Ptr<Packet> packet;

  // Address from;
  // // Address from;
  // std::cout << "id:" << this->GetId() << "\tsocket:" << socket << std::endl;
  // while ((packet = socket->Recv())) {
  //   // 获取源地址
    InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
    Ipv4Address sourceAddress = inetFrom.GetIpv4();
  //   // std::cout << "SlaveController Received packet from " << sourceAddress << std::endl;
    uint8_t buffer[packet->GetSize()];
    packet->CopyData(buffer, packet->GetSize());

    DTag tag;
    packet->RemovePacketTag(tag);
    uint32_t ttag = tag.GetTag();
    // 本节点接收到的控制信息累加
    ctlPktRecv += packet->GetSize();    // 单位Byte，字节
    if(tag.GetHops() != 0) ctlPktRecvOver += packet->GetSize()*tag.GetHops();
    else ctlPktRecvOver += packet->GetSize()*2.25;
    if(ttag == 0){
      ctlPktRecv0 += packet->GetSize();    // 单位Byte，字节
      if(tag.GetHops() != 0) ctlPktRecvOver0 += packet->GetSize()*tag.GetHops();
      else ctlPktRecvOver0 += packet->GetSize()*2.25;
    }
    else if(ttag == 1){
      ctlPktRecv1 += packet->GetSize();    // 单位Byte，字节
      if(tag.GetHops() != 0) ctlPktRecvOver1 += packet->GetSize()*tag.GetHops();
      else ctlPktRecvOver1 += packet->GetSize()*2.25;
    }
    else if(ttag == 2){
      ctlPktRecv2 += packet->GetSize();    // 单位Byte，字节
      if(tag.GetHops() != 0) ctlPktRecvOver2 += packet->GetSize()*tag.GetHops();
      else ctlPktRecvOver2 += packet->GetSize()*2.25;
    }
    ofi::pktType type; 
    std::memcpy(&type, buffer, sizeof(type));

    if(_CtrlInfoOutput) 
      std::cout //<< "Received packet from " << Mac48Address::ConvertFrom (src) << " to " << myaddr << " 协议类型："<< protocol
                << "************************ " << std::endl 
                << GetStateString(m_state) << "id:" << static_cast<unsigned int>(this->GetId()) << " Received packet" << std::endl;
                // << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) << std::endl;
                // << "************" << std::endl;
    if(type == ofi::pktType::HEARTBEAT){
      ofi::HeartbeatPacket hpacket;
      std::memcpy(&hpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::HeartbeatPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type)
                  << "\t主控制器ID：" << static_cast<unsigned int>(hpacket.controller_id) 
                  << "\t从控制器ID： " << static_cast<unsigned int>(hpacket.subcontroller_id) 
                  << "\t序列号：" << static_cast<unsigned int>(hpacket.sequence_number) 
                  << "\t时间戳：" << static_cast<unsigned int>(hpacket.timestamp) << std::endl
                  << "************************" << std::endl;
      // if(m_state == slaveController || m_state == mutilController) 
      DealWithHeartBeat(hpacket, sourceAddress);
      if(m_state == breakNode && _mode == 3) SendBreakNodeInfo_normal(hpacket);
    }
    else if(type == ofi::pktType::IDENTITY){
      ofi::IdentityPacket ipacket;
      std::memcpy(&ipacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::IdentityPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type)
                  << "\t新主控制器ID：" << static_cast<unsigned int>(ipacket.new_master_controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(ipacket.dest_controller_id) 
                  << "\t生存时间：" << static_cast<unsigned int>(ipacket.expiration_period) 
                  << "\t时间戳：" << static_cast<unsigned int>(ipacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == slaveController || m_state == mutilController) DealWithIdentity(ipacket, sourceAddress);
    }
    else if(type == ofi::pktType::Disconnect){
      ofi::DisconnectPacket dpacket;
      std::memcpy(&dpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::DisconnectPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(dpacket.send_controller_id)
                  << "\t从控制器ID： " << static_cast<unsigned int>(dpacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(dpacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(dpacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == slaveController || m_state == mutilController) DealWithDisconnect(dpacket, sourceAddress);
    }
    else if(type == ofi::pktType::ACTIVATION){
      ofi::ActivationPacket apacket;
      std::memcpy(&apacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::ActivationPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(apacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(apacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(apacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(apacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == masterController) DealWithActivation(apacket, sourceAddress);
    }
    else if(type == ofi::pktType::Recovery){
      ofi::RecoveryPacket rpacket;
      std::memcpy(&rpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::RecoveryPacket));
      if(_CtrlInfoOutput) 
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(rpacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(rpacket.dest_controller_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(rpacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(rpacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      if(m_state == mutilController) DealWithRecovery_master(rpacket, sourceAddress);
    }
    else if(type == ofi::pktType::CheckSatus){ // 从 -》簇内节点
      ofi::CheckStatusPacket checkspacket;
      std::memcpy(&checkspacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::CheckStatusPacket));
      if(_CtrlInfoOutput){
        std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                  << "\t从控制器ID：" << static_cast<unsigned int>(checkspacket.send_controller_id)
                  << "\t主控制器ID： " << static_cast<unsigned int>(checkspacket.dest_node_id) 
                  << "\t故障信息：" << static_cast<unsigned int>(checkspacket.fau_info) 
                  << "\t时间戳：" << static_cast<unsigned int>(checkspacket.timestamp) << std::endl
                  << "************************ " << std::endl;
      }
      // if(m_state == satellite) SendStatus(checkspacket);
      // if(m_state == breakNode) SendBreakNodeInfo_normal(checkspacket);
    }
    else if(type == ofi::pktType::Status){
      ofi::StatusPacket spacket;
      std::memcpy(&spacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::StatusPacket));
      if(_CtrlInfoOutput){
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(spacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(spacket.dest_id)  
                << "\t时间戳：" << static_cast<unsigned int>(spacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
      // if(m_state == slaveController || m_state == mutilController) DealWithStatus(spacket);
    }
    else if(type == ofi::pktType::BreakInfo){
      if(_mode != 3) return ;
      ofi::BreakInfoPacket bpacket;
      std::memcpy(&bpacket, buffer + sizeof(type) + sizeof(Mac48Address) + sizeof(Mac48Address), sizeof(ofi::BreakInfoPacket));
      if(_CtrlInfoOutput){
            std::cout << "数据包类型：" << ofi::SDNPacket::GetStatusString(type) 
                << "\t发送节点ID：" << static_cast<unsigned int>(bpacket.src_id)
                << "\t目的节点ID： " << static_cast<unsigned int>(bpacket.dest_id)  
                << "\t故障节点ID： " << static_cast<unsigned int>(bpacket.break_id)
                << "\t时间戳：" << static_cast<unsigned int>(bpacket.timestamp)
                << std::endl
                << "************************ " << std::endl;
      }
      if(m_state == masterController) DealWithBreakInfo(bpacket);
      else if(m_state == slaveController || m_state == mutilController) 
      {
        if(_mode == 3) SendBreakNodeInfo(static_cast<unsigned int>(bpacket.break_id));
        // + 从控制器检测到簇内节点故障后，重计算簇内路由表+协同路由表 行为
        // RecomputeRouteInfo();
      }
    }
  // }
}

}


