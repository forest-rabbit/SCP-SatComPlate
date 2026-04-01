# include "para.h"
#include <cstdint>

namespace ns3{
  /* 主要修改参数 */
  int _SDNRoute = 1;                    // 路由方案， 0:OSPF  1:本方案路由
  int _isSate = 1;                      // 星座构型， 1表示60星座，2表示108星座，3表示500星座, 4表示432星座
  bool _consType = 0;                   // 星座构型， 0: Walker Star    1: Walker Delta
  double clusterUpdateStep = 5;         // 时间步参数： 簇更新频率(s) 
  const double routeUpdateStep = 5;     // 时间步参数： 路由更新频率(s) 
  const double totalTimeStep = 110;      // 时间步参数： 仿真时间步总长(s) 
  double offeredload = 2.0;             // 流量参数：   负载率，范围 0.5-6.0
  bool _trafficMode = 0;                // 流量参数：   流量模式，0表示区域热点流量，1表示均匀流量
  int _tranProc = 0;                    // 传输协议，0:UDP，1:TCP
  const double linkAvailability = 1.0;  // 链路可用度, 范围 0.8~1.0
  bool _sim = 1;                        // 定义仿真模式，0表示完整方案仿真，1表示与XW仿真中心交互
  bool _scenario = 0;                   // 定义仿真场景，0表示正常场景，1表示最少异轨链路场景
  bool _slaveMode = 0;                  // 定义从控制器模式，0表示从控制器位于簇首，1表示从控制器位于地面中心附近

  /* 管控 */
  int _mode = 0;            // 定义管控场景模式，0表示正常管控场景，1表示地面主控到地面备份主控的迁移场景，
                            // 2表示星地链路断连场景，3表示从控制器失效场景
  
  // 新加参数，区分从控制器部署于星上和地面的场景

  /* 星座参数 */
  uint32_t sates_num = _isSate == 1 ? 60 : (_isSate == 2 ? 108 : (_isSate == 3 ? 500 : 432));  // N: LEO卫星总数
  uint32_t orbit_num = _isSate == 1 ? 6 : (_isSate == 2 ? 12 : (_isSate == 3 ? 25 : 24));      // No: 轨道数
  uint32_t sate_num;        // Ns:每个轨道上卫星数量
  int gwsnum = 5;           // 地面站数量
  int mcsnum = 2;     // 主控制器数量
  const int scsnum = 1;     // 从控制器数量
  uint32_t sateBegID = _slaveMode == 0 ? mcsnum + gwsnum : mcsnum + scsnum + gwsnum; // 起始卫星ID  
  const uint32_t ISLNum = 4;            // 卫星节点星间链路条数
  
  /* 其他固定参数 */
  const double timeStepSize = 1;   // 时间步长度（s）
  // long int linkBandwidth = 10000000000;   // 链路带宽 10Gbps
  long int linkBandwidth = 100000000;   // 链路带宽 100Mbps
  const uint8_t CUSTOM_PROTOCOL_NUMBER = 253; // 自定义协议编号
  
  /*编号映射关系*/
  std::map<uint32_t,uint32_t> node_id; // key: ns3编号 value: 请求编号
  std::map<uint32_t,uint32_t> id_node; // key: 请求编号 value: ns3编号
  bool first_flag = false;
}