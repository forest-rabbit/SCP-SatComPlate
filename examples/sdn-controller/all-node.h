#ifndef ALL_NODE_H
#define ALL_NODE_H

#include "ns3/boolean.h"
#include "ns3/integer.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include <cstdint>
#include <ns3/ipv4-address.h>
#include <ns3/node.h>
#include <ns3/openflow-interface.h>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/openflow-sdn-controller.h"
#include "ns3/openflow-switch-net-device.h"
#include "ns3/cluster-module.h"
#include "ns3/dtag.h"
#include "para.h"
#include <mutex>

extern bool m_toMasterflag;  // 从控充当主控，附加信息，不需要预先设置
extern bool m_slaveBreakInfo;
extern int breakSID;   // 故障的从控制器ID
extern std::mutex smutex;             // 和管控架构公用的信号量 

namespace ns3{

enum State{
  satellite, 
  slaveController,
  masterController,
  mutilController,      // 特殊情况，星上临时主控，即同时充当主控/从控的情况
  breakNode,                // 节点失效，不发包
};

std::string GetStateString(State s);

typedef Callback<void, Ptr<Packet>, Address> PacketReceiveCallback;

class SinglePacketTcpClient : public Application
{
public:
    SinglePacketTcpClient();
    virtual ~SinglePacketTcpClient();

    void Setup(Address address, uint16_t port, Ptr<Packet> data);

protected:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

private:
    void HandleConnect(Ptr<Socket> socket);
    void HandleConnectError(Ptr<Socket> socket);

    Ptr<Socket> m_socket;
    Address m_peerAddress;
    uint16_t m_peerPort;
    Ptr<Packet> m_data;
};

// 自定义服务器类，接收并处理数据包
class SinglePacketTcpServer : public Application
{
public:
    SinglePacketTcpServer();
    virtual ~SinglePacketTcpServer();

    void Setup(uint16_t port);

    // 设置接收数据包的回调函数
    void SetPacketReceiveCallback(PacketReceiveCallback cb)
    {
        m_packetReceiveCallback = cb;
    }

protected:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

private:
    bool HandleAccept(Ptr<Socket> socket, const Address& from);
    void NewConnectionCreated(Ptr<Socket> socket, const Address& from);
    void HandleRead(Ptr<Socket> socket);
    Ptr<Socket> m_socket;
    uint16_t m_port;

    // 成员变量存储回调函数
    PacketReceiveCallback m_packetReceiveCallback;
};



class satelliteNode : public Node{
public:
  State m_state;

  // 测试需要的变量
  uint32_t ctlPktRecv;    // 本节点接收到的控制数据包总大小，单位Byte，字节
  uint32_t ctlPktRecvOver;    // 本节点控制开销，字节乘以跳数
  uint32_t ctlPktRecv0;    // 主从之间
  uint32_t ctlPktRecvOver0;    // 主从之间
  uint32_t ctlPktRecv1;    // 从从之间
  uint32_t ctlPktRecvOver1;    // 从从之间
  uint32_t ctlPktRecv2;    // 从和普通卫星之间
  uint32_t ctlPktRecvOver2;    // 从和普通卫星之间
  uint32_t ctlPktSend;    // 本节点发送的控制数据包总大小，单位Byte，字节
  std::unordered_map<uint32_t, double> m_breakInfo;     ///< 故障节点信息记录，便于后续统计
  uint32_t  m_breakPktNum = 0;
  double m_breakTime = 0.0;
  satelliteNode ()
  {
    ctlPktRecv = 0;
    ctlPktRecvOver = 0;
    ctlPktRecv0 = 0;   
    ctlPktRecvOver0 = 0;  
    ctlPktRecv1 = 0;  
    ctlPktRecvOver1 = 0;   
    ctlPktRecv2 = 0;
    ctlPktRecvOver2 = 0;
    ctlPktSend = 0;
    m_HeartBeat = 0;
  }

  virtual ~satelliteNode ()
  {
  }

  void SetToMasterController(double interval,        ///< 周期 
                            // Ptr<ofi::MasterController> master,   ///< 主控制器指针，屏蔽
                            uint32_t masterID,            ///< 主控制器ID
                            NodeContainer master_nodes,   ///< 主控制器节点集合
                            NodeContainer slavesnodes    ///< 从控制器节点集合
                            );
  
  void SetToSlaveController(double interval,        ///< 周期 
                            // Ptr<ofi::SlaveController> slave,   ///< 从控制器指针，屏蔽
                            NodeContainer master_nodes,   ///< 主控制器节点集合
                            NodeContainer slavesnodes,    ///< 从控制器节点集合
                            NodeContainer intraNodes      ///< 簇内节点集合
                            );    

  void SetToNormal(double interval,        ///< 周期
                    // Ptr<ofi::SlaveController> slave,   ///< 从控制器指针,屏蔽
                    NodeContainer slavesnodes,    ///< 从控制器节点集合
                    NodeContainer master_nodes,   ///< 主控制器节点集合
                    NodeContainer intraNodes  ///< 簇内节点集合
                    );
  
  // 从控制器行为
  // void StartRecvPacket_slave();

  // 普通卫星行为 + 从控制器
  void StartRecvPacket_sate();

  // 主控制器行为
  void StartRecvPacket_master();
  // 函数功能：发送心跳包
  void SendHeartbeat();
  // 函数功能：发送身份包
  void SendIdentity();
  void ToBackup(int masterID);

protected:

  // 共有信息，需要预先设置
  double    m_interval;                       ///< 本节点发送/接收心跳包等的时间间隔，单位为s 
  Ptr<ofi::SlaveController> m_controller;    // 本节点绑定的控制器指针
  Ptr<ofi::MasterController> m_master;    ///本节点绑定的主控制器指针
  NodeContainer m_master_nodes;               ///< 主控节点信息
  NodeContainer m_slavesnodes;               ///< 从控节点信息

  //子控制器信息，需要预先设置
  NodeContainer m_intraNodes;                 ///< 簇内节点信息
  std::unordered_set<uint32_t> m_statusSets;       ///< 收到簇内节点状态包的节点ID

  // 共有信息，主控的需要预先设置
  uint32_t m_masterID;                 // 主控制器id

  // 从控制器变量，不需要预先设置
  std::vector<Ptr<Socket>> m_RecvPktsocket_slave;   /// 接收socket集合
  bool      m_checkFlag = false;       ///< 判断本节点是否开始发送状态检测包的标志
  std::unordered_set<uint8_t> m_disconnect;
  double    m_HeartBeat;              ///< 记录本节点上次接收心跳包的时间，用于故障检测
  std::vector<double> m_TimeHeartBeat;     ///< 记录本节点前三次接收的心跳包时间

  // // 从控充当主控，附加信息，不需要预先设置
  // bool* m_toMasterflag = new bool(); 

  // 普通卫星节点变量
  bool breakInfoSend = false;

  // 主控制器变量，不需要预先设置
  EventId m_sendHeartbeatEvent;               ///< 心跳包发送事件句柄
  uint8_t   m_heartseq = 0;                   ///< 本节点发送的心跳包序列号
  uint32_t m_RecoveryNum = 0;                 ///< 接收到的恢复包数量
  std::vector<Ptr<Socket>> m_RecvPktsocket_master;   ///< 接收socket集合

  // 参数tag: 0表示主从控制器之间控制开销、1表示从从之间、2表示从和普通卫星之间
  bool SendPacket(Ptr<Packet> packet, Ipv4Address srcAddress, Ipv4Address destAddress, uint32_t inter, uint32_t tag);
  void RecvPacketActionTCP(Ptr<Packet> packet, Address from);
  void RecvPacketAction(Ptr<Socket> socket);

  // 从控制器行为
  void DealWithHeartBeat(ofi::HeartbeatPacket packet, Ipv4Address srcAddress);
  void DealWithIdentity(ofi::IdentityPacket packet, Ipv4Address srcAddress);
  void DealWithDisconnect(ofi::DisconnectPacket packet, Ipv4Address srcAddress);
  void DealWithRecovery_slave();
  void HeartBeatShedule();     // 周期性接收心跳包检查
  void checkHeartBeat();       // 检查函数
  void SendDisconnect();       // 发送失联通告包
  void SendRecovery();
  void SendCheckStatus();
  // void DealWithStatus(ofi::StatusPacket spacket);
  void SendBreakNodeInfo(uint32_t nodeID);

  // 主控制器行为
  void DealWithActivation(ofi::ActivationPacket packet, Ipv4Address srcAddress);
  void DealWithRecovery_master(ofi::RecoveryPacket packet, Ipv4Address srcAddress);
  void DealWithBreakInfo(ofi::BreakInfoPacket packet);
  void ToMaster();
  // 函数功能：发送激活包
  void SendActivation();

  // 普通卫星节点行为
  void SendStatus(ofi::CheckStatusPacket csPacket);
  void SendBreakNodeInfo_normal(ofi::HeartbeatPacket csPacket);
  void SyncToMaster();
  void ForwardHeartbeatToCluster(ofi::HeartbeatPacket packet);


private:

};

}

#endif