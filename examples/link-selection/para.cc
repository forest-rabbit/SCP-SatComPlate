# include "para.h"
#include <cstdint>

namespace ns3{
  /* 主要修改参数 */
  int _SDNRoute = 0;                    // 路由方案， 0:OSPF  1: 簇内簇间路由
  int _isSate = 1;                      // 星座构型， 1：324颗卫星
  bool _consType = 0;                   // 星座构型， 0: Walker Star    1: Walker Delta
  double clusterUpdateStep = 5;         // 时间步参数： 簇更新频率(s) 
  const double routeUpdateStep = 5;     // 时间步参数： 路由更新频率(s) 
  const double totalTimeStep = 110;      // 时间步参数： 仿真时间步总长(s) 
  double offeredload = 0.1;             // 流量参数：   负载率，范围 0.5-6.0
  bool _trafficMode = 0;                // 流量参数：   流量模式，0表示区域热点流量，1表示均匀流量
  int _tranProc = 0;                    // 传输协议，0:UDP，1:TCP
  const double linkAvailability = 1.0;  // 链路可用度, 范围 0.8~1.0
  bool _scenario = 0;                   // 定义仿真场景，0表示正常场景，1表示激光链路分配场景
  //int link_change = 2;                  // 链路切换模式： 0表示无罚函数 1表示罚函数 2表示NSGAII
  bool _isMesh = false;                 // 是否为mesh拓扑，true表示mesh拓扑，false表示非mesh拓扑

  /* 管控 */
  int _mode = 0;            // 定义管控场景模式，0表示正常管控场景，1表示地面主控到地面备份主控的迁移场景，
                            // 2表示星地链路断连场景，3表示从控制器失效场景
  uint32_t _clusterMode = 2; // 0: 双层分簇 1: 链路利用率分簇 2: 轨道分簇 3: 连通性分簇
  

  /* 星座参数 */
  uint32_t sates_num = _isSate == 1 ? 324 : (_isSate == 2 ? 351 : (_isSate == 3 ? 500 : 432));  // N: LEO卫星总数 324/18
  uint32_t orbit_num = _isSate == 1 ? 18 : (_isSate == 2 ? 27 : (_isSate == 3 ? 25 : 24));      // No: 轨道数
  uint32_t sate_num;        // Ns:每个轨道上卫星数量
  uint32_t sateBegID = 0; //卫星起始ID
  const uint32_t ISLNum = 4;            // 卫星节点星间链路条数
  
  /* 其他固定参数 */
  const double timeStepSize = 1;   // 时间步长度（s）
  long int linkBandwidth = 10000000000;   // 链路带宽 10Gbps
  //long int linkBandwidth = 100000000;   // 链路带宽 100Mbps
  const uint8_t CUSTOM_PROTOCOL_NUMBER = 253; // 自定义协议编号

}