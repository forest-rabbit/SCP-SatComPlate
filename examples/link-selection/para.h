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

#define _NodeUiliz 0        // 输出节点链路利用率信息开关
#define _trafficDown    0   // 屏蔽远距离流量

namespace ns3 {
  extern bool _useJsonTopo;    // true: 使用input/topology JSON作为拓扑真值；false: 使用传统星座/CSV逻辑
  extern bool _jsonTopoPatchMode; // true: 后续时间片读取patch_<time>s.json增量；false: 读取全量快照

  extern const double totalTimeStep; // 仿真总时长(s)，JSON模式仍有效
  extern double offeredload;         // 业务负载率
  extern bool _trafficMode;          // 业务流量模式：0区域热点，1均匀流量
  extern int _tranProc;              // 传输协议：0 UDP，1 TCP
  extern long int linkBandwidth;     // 默认链路带宽；JSON链路未写带宽时作为兜底值
  extern bool writeRoutingTables;    // 是否输出调试用路由表文件

  extern int _SDNRoute;              // 路由方案：0 OSPF，1 簇内/簇间路由
  extern int _mode;                  // 管控场景：0正常，1主备迁移，2星地断连，3控制器失效
  extern uint32_t _clusterMode;      // 传统动态分簇模式：0双层，1链路利用率，2轨道，3连通性

  extern int _isSate;                // 仅传统模式使用；JSON模式下不决定拓扑
  extern bool _consType;             // 仅传统模式使用；0 Walker Star，1 Walker Delta
  extern bool _scenario;             // 仅传统mesh模式使用
  extern bool _isMesh;               // 仅传统拓扑模式使用
  extern const double linkAvailability; // 仅传统动态链路逻辑使用；JSON模式下关闭
  extern double clusterUpdateStep;   // 仅传统动态分簇逻辑使用；JSON模式下关闭
  extern const double routeUpdateStep; // 仅传统动态路由逻辑使用

  extern uint32_t sates_num;         // JSON模式初始化后由nodes_0s.json覆盖
  extern uint32_t orbit_num;         // JSON模式只作为旧算法兼容值
  extern uint32_t sate_num;          // JSON模式只作为旧算法兼容值
  extern uint32_t sateBegID;         // 卫星节点开始编号
  extern const uint32_t ISLNum;      // 传统1星4连假设；JSON模式按文件链路为准
  extern const double timeStepSize;  // 时间步长度(s)
  extern const uint8_t CUSTOM_PROTOCOL_NUMBER;
}

# endif
