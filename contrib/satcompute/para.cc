/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// 集中定义 SatCompute 的默认运行参数；所有参数均可由同名 CLI 选项覆盖。

#include "para.h"

namespace ns3 {

SatComputeConfig
GetDefaultSatComputeConfig()
{
  SatComputeConfig config;

  // --topologyDir：节点、链路分离的 JSON 全量快照目录。目录必须包含时间匹配的
  // nodes_<time>s.json 和 topology_<time>s.json；可填写仓库相对路径或绝对路径。
  config.topologyDirectory =
    "contrib/satcompute/input/topology/json/examples/xw-66sat";

  // --trafficMatrix：临时保留的 legacy CSV 业务输入。N 颗卫星要求 100*N 行、
  // 每行 N 列，数值表示 Gbps；仅 legacy 模式且 offeredLoad>0 时读取。
  config.trafficMatrix =
    "contrib/satcompute/input/traffic/csv/traffic_matrix(66).csv";

  // --transferTrace：NetworkTransfer JSON 路径。空字符串表示使用 legacy 模式；
  // 非空时启用 schema_version=0.1 的逐流输入，并要求 offeredLoad=0、transport=udp。
  config.transferTrace = "";

  // --computeProfile：topology/resources 下的静态计算能力 JSON。
  // 必须与 taskTrace 同时提供；空字符串表示不启用任务模式。
  config.computeProfile = "";

  // --taskTrace：traffic/json/task 下的任务到达 JSON。
  // 必须与 computeProfile 同时提供，且不能与 transferTrace 或 offeredLoad 混用。
  config.taskTrace = "";

  // --outputDir：结构化指标输出目录；可填写仓库相对路径或绝对路径。
  config.outputDirectory = "contrib/satcompute/output";

  // --transport：legacy 流量的传输协议，可填 "udp" 或 "tcp"。
  // NetworkTransfer 当前只支持 "udp"。
  config.transport = "udp";

  // --routingMode：
  // "global-first" 使用 ns-3 Ipv4GlobalRouting 的默认路由选择；
  // "global-hash-per-flow" 对等价最短路执行确定性的五元组逐流 hash。
  config.routingMode = "global-hash-per-flow";

  // --transferLogMode：
  // "summary" 输出聚合信息与少量样本，"verbose" 输出每条 transfer，
  // "silent" 关闭运行、拓扑和 transfer 日志；三种模式都写出指标文件。
  config.transferLogMode = "summary";

  // --taskLogMode：
  // "summary" 输出任务输入聚合和样本，"verbose" 输出每个节点与任务，
  // "silent" 关闭任务输入日志；三种模式都不改变任务行为。
  config.taskLogMode = "summary";

  // --taskCompletionPolicy：
  // "strict" 在任务未全部完成时写出指标并以非零状态退出；
  // "report" 保留相同仿真与指标语义，但将部分完成视为可报告结果并正常退出。
  config.taskCompletionPolicy = "strict";

  // --diagnosticMode：
  // "off" 关闭失败诊断采集与诊断文件，只保留基础指标；
  // "failure" 在任务未全部完成时写出未完成对象、队列 Drop 和链路集中度。
  config.diagnosticMode = "off";

  // --transferChunkMode：
  // "fixed" 让所有 transfer 使用 transferPayloadBytes 作为 UDP payload 上限；
  // "size-aware" 对 <=1 MiB、>1 MiB 且 <=64 MiB、>64 MiB 的逻辑传输
  // 分别使用 1024、8192、64000-byte payload。
  config.transferChunkMode = "fixed";

  // --transferPayloadBytes：fixed 模式的 UDP 应用 payload 上限，范围 1..65507
  // bytes；payload 加 28-byte UDP/IPv4 header 后必须不超过 islMtuBytes。
  // size-aware 模式不使用该值分包，但当前仍要求它处于合法范围。
  config.transferPayloadBytes = 1024;

  // --islMtuBytes：每个 ISL PointToPointNetDevice 的 MTU，范围 68..65535
  // bytes。size-aware 模式需要至少 64028 bytes 才能容纳最大分包及 UDP/IPv4 header。
  config.islMtuBytes = 1500;

  // --islQueueBytes：每个 ISL DropTail 队列的总字节容量，必须大于 0。
  // 调小会更早产生竞争丢包，调大可以容纳更多排队数据。
  config.islQueueBytes = 1500000;

  // --receiverRcvBufBytes：每个 NetworkTransfer UDP 接收 socket 的缓冲区，
  // 单位为 bytes，必须大于 0。默认值与 ns-3 UdpSocket 一致。
  config.receiverRcvBufBytes = 131072;

  // --ecmpHashSeed：逐流 ECMP 的 uint64 hash seed；任意 uint64 值均可。
  // 相同 seed 和输入保持相同选路，改变 seed 会重新映射等价路径。
  config.ecmpHashSeed = 1;

  // --simulationDuration：仿真停止时间，单位为秒，必须是有限正数。
  config.simulationDurationSeconds = 110.0;

  // --offeredLoad：legacy CSV 业务需求的非负倍率；0 表示不安装 client flow。
  // NetworkTransfer 模式必须保持为 0。
  config.offeredLoad = 0.0;
  return config;
}

} // namespace ns3
