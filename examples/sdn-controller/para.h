#ifndef PARA_H
#define PARA_H

#include <iostream>
#include <fstream>
#include <string>
#include <map>

// #define _isSate1 1          // 采用60星座的标志，0表示采用108星座
// #define _isUDP 1            // 采用UDP标志，0表示采用TCP
// #define _SDNRoute 1         // 采用本方案路由方案的标志，0表示采用OSPF
#define _DynamicCluster 1   // 采用动态分簇方案的标志，0表示采用静态分簇
#define _TrafficBurst 0     // 流量突发标志，为1表示每一对源/目的节点有25%的概率发送原先两倍的流量，为0表示无流量突发

#define _BreakDetect 0      // 测量主控制器感知卫星故障时延的标志
#define _CtrlInfoOutput 0   // 输出控制信息开关
#define _NodeUiliz 0        // 输出节点链路利用率信息开关

#define _linkErrorModel 0   // 链路丢包率开关
#define _trafficDown    0   // 屏蔽远距离流量

namespace ns3 {
  extern int _SDNRoute;        // 采用本方案路由方案的标志，0表示采用OSPF
  extern int _isSate;          // 星座构型，1表示60星座，2表示108星座，3表示500星座
  extern bool _consType;       // 定义星座轨道类型（影响路由策略）0: Walker Star    1: Walker Delta
  extern int _mode;            // 定义管控场景模式，0表示正常管控场景，1表示地面主控到地面备份主控的迁移场景，
                               // 2表示星地链路断连场景，3表示从控制器失效场景
  extern int _tranProc;        // 传输协议，0表示UDP，1表示TCP
  extern bool _trafficMode;    // 流量模式，0表示区域热点流量，1表示均匀流量
  extern bool _sim;            // 定义仿真模式，0表示完整方案仿真，1表示与XW仿真中心交互
  extern bool _scenario;       // 定义仿真场景，0表示正常场景，1表示最少异轨链路场景
  extern bool _slaveMode;      // 定义从控制器模式，0表示从控制器位于簇首，1表示从控制器位于地面站

  // 仿真中心对接编号映射关系
  extern bool first_flag; //首次post_link
  extern std::map<uint32_t,uint32_t> node_id; // key: ns3编号 value: 请求编号
  extern std::map<uint32_t,uint32_t> id_node; // key: 请求编号 value: ns3编号

  extern uint32_t sates_num;   // N: LEO卫星总数
  extern uint32_t orbit_num;   // No: 轨道数
  extern uint32_t sate_num;    // Ns:每个轨道上卫星数量
  extern int gwsnum;         // 地面站数量
  extern int mcsnum;         // 主控制器数量
  extern const int scsnum;         // 从控制器数量
  extern uint32_t sateBegID;      // 卫星节点开始编号
  extern const uint32_t ISLNum;     // 卫星节点星间链路条数
  extern const double timeStepSize;   // 时间步长度（s）
  extern const double totalTimeStep;    // 仿真时间步总长(s)
  extern double clusterUpdateStep;   // 簇更新频率(s)
  extern const double routeUpdateStep;     // 路由更新频率(s)  
  extern long int linkBandwidth;   // 链路带宽
  extern double offeredload;  // 负载率，范围1-10
  extern const double linkAvailability;     // 链路可用度, 范围0.8~1.0
  extern const uint8_t CUSTOM_PROTOCOL_NUMBER;
}

# endif