# include "para.h"
#include <cstdint>

namespace ns3{
  /*
   * JSON拓扑模式开关。
   *
   * true:
   *   - 节点、链路、簇归属、簇首、运行期拓扑变化均来自 examples/link-selection/Topodata。
   *   - _isSate、orbit_num、sate_num、_isMesh、_scenario、linkAvailability 等传统拓扑参数不再决定拓扑。
   *   - 仿真结束时间仍由totalTimeStep控制；最后一个JSON时间片之后拓扑保持最后状态继续运行。
   * false:
   *   - 使用原有星座参数、CSV拓扑和内置动态更新逻辑。
   */
  bool _useJsonTopo = true;
  bool _jsonTopoPatchMode = false;       // false=后续时间片为全量快照；true=后续时间片为patch增量

  /* JSON模式仍然建议修改的实验参数：这些控制仿真和业务，不控制拓扑结构 */
  const double totalTimeStep = 110;     // 仿真总时长(s)，JSON模式仍有效
  double offeredload = 0.0001;             // 业务负载率，范围 0.5-6.0
  bool _trafficMode = 0;                // 业务流量模式：0区域热点，1均匀流量
  int _tranProc = 0;                    // 传输协议：0 UDP，1 TCP
  long int linkBandwidth = 10000000000; // 默认链路带宽；JSON链路未写带宽时作为兜底值

  /* 路由/管控参数；当前默认使用OSPF，SDN路由逻辑仍保留 */
  int _SDNRoute = 0;                    // 路由方案：0 OSPF，1 簇内/簇间路由
  int _mode = 0;                        // 管控场景：0正常，1主备迁移，2星地断连，3控制器失效
  uint32_t _clusterMode = 2;            // 传统动态分簇模式：0双层，1链路利用率，2轨道，3连通性
  
  /* 仅传统拓扑模式使用；JSON模式下不作为拓扑真值 */
  int _isSate = 1;                      // 传统星座构型：1=324，2=351，3=500，4=432
  bool _consType = 0;                   // 传统星座轨道类型：0 Walker Star，1 Walker Delta
  bool _scenario = 0;                   // 传统mesh场景：0正常，1激光链路分配场景
  bool _isMesh = false;                 // 传统拓扑模式：true mesh，false CSV/JSON非mesh
  const double linkAvailability = 1.0;  // 传统动态链路可用度；JSON模式下动态链路更新关闭
  double clusterUpdateStep = 5;         // 传统动态分簇频率(s)；JSON模式下动态分簇关闭
  const double routeUpdateStep = 5;     // 传统路由更新频率(s)

  /* 兼容旧代码的派生状态；JSON模式初始化后会由nodes_0s.json覆盖 */
  uint32_t sates_num = _isSate == 1 ? 324 : (_isSate == 2 ? 351 : (_isSate == 3 ? 500 : 432));
  uint32_t orbit_num = _isSate == 1 ? 18 : (_isSate == 2 ? 27 : (_isSate == 3 ? 25 : 24));
  uint32_t sate_num = sates_num / orbit_num; // 传统每轨卫星数；JSON模式初始化后由节点文件覆盖/修正
  uint32_t sateBegID = 0;               // 卫星节点起始编号
  const uint32_t ISLNum = 4;            // 传统1星4连假设；JSON模式按文件链路为准
  
  /* 其他固定参数 */
  const double timeStepSize = 1;        // 时间步长度(s)
  const uint8_t CUSTOM_PROTOCOL_NUMBER = 253;

}
