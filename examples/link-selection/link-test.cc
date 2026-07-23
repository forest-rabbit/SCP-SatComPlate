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

// Network topology
//
//        subcontroller0          subcontroller1
//        |                           |
//       -------------------------------
//        |     mastercontroller      |
//       -------------------------------
//        |                           |
//        subcontroller2     subcontroller3
//
//
// - CBR/UDP flows from n0 to n1 and from n3 to n0
// - DropTail queues
// - Tracing of queues and packet receptions to file "openflow-switch.tr"
// - If order of adding nodes and netdevices is kept:
//      n0 = 00:00:00;00:00:01, n1 = 00:00:00:00:00:03, n3 = 00:00:00:00:00:07
//	and port number corresponds to node number, so port 0 is connected to n0, for example.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ns3/nstime.h>
#include <ns3/simulator.h>
#include <ns3/time-series-adaptor.h>

#include "cluster.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/node-container.h"
#include "ns3/cluster-module.h"
#include "ns3/log.h"
#include "ns3/flow-monitor-module.h"
#include "metrics/metrics.h"
#include "para.h"
#include "topo.h"

#include <numeric>
#include <random>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample");

ns3::Time timeout = ns3::Seconds (0);

std::string trafficMatrixFile =
  "examples/link-selection/input/traffic/traffic_matrix(324).csv";
std::string metricsOutputDirectory = "examples/link-selection/output";
std::vector<Ptr<UdpServer>> udpServers;
std::vector<Ptr<PacketSink>> tcpSinks;

TaskApplicationMetrics
CollectTaskApplicationMetrics()
{
  TaskApplicationMetrics metrics = {};
  metrics.server_applications = udpServers.size() + tcpSinks.size();
  for (const auto& server : udpServers)
  {
    metrics.udp_packets_received += server->GetReceived();
  }
  for (const auto& sink : tcpSinks)
  {
    metrics.tcp_bytes_received += sink->GetTotalRx();
  }
  return metrics;
}


void GetData(vector<vector<double>>& data, const int destNum, std::string name)
{
  // destNum来自当前实际创建的卫星数量。JSON 66星模式下这里会按66列读取，
  // 即使当前文件名仍是traffic_matrix(324).csv；其他规模可通过--trafficMatrix指定。
  vector<vector<double>> temp_data(destNum*100, vector<double>(destNum, 0.0));
	std::ifstream inFile(name, std::ios::in);
	std::string lineStr;
  std::cout << "[TRAFFIC] 读取流量矩阵" << std::endl
            << "  file      : " << name << std::endl
            << "  sateNum   : " << destNum << std::endl
            << std::endl;
  int i = 0;
  int j = 0;
	while (getline(inFile, lineStr))
	{
    if(i >= (int)temp_data.size()) break;
		std::stringstream ss(lineStr);
		std::string str;
		while (getline(ss, str, ','))
		{
			double num = stod(str);
			temp_data[i][j] = offeredload * num;     // Gbps
      //cout<<temp_data[i][j]<<"\t";
      j++;
      if( j == destNum) j = 0;
		}
        i++;
	}
  inFile.close();

  //流量矩阵累加 m:行 n:列 流量发送时间:100s
  if(_trafficMode == 0)
  {
    #ifdef _trafficDown
    int satePerOrbit;
    int networkR;
    if((_isSate == 2) && (_tranProc == 0))
    {
        satePerOrbit = (int)sates_num / orbit_num;
        networkR = (satePerOrbit + orbit_num) / 6;
    }
    #endif

    for(int m = 0; m < destNum; m++)
    {
        #ifdef _trafficDown
        int srcSatOrbit = m / satePerOrbit;
        int srcSatNumber = m % satePerOrbit;
        #endif
        for(int n = 0; n < 100; n++)
        {
            int index = m + n*destNum;
            if(index < destNum*100)
            {
                for(int k = 0;k < destNum; k++)
                {
                #ifdef _trafficDown
                int dstSatOrbit = k / satePerOrbit;
                int dstSatNumber = k % satePerOrbit;

                int maxPathHop = (std::min(std::abs(dstSatOrbit - srcSatOrbit), (int)orbit_num - std::abs(dstSatOrbit - srcSatOrbit)) 
                                    + std::min(std::abs(dstSatNumber - srcSatNumber), satePerOrbit - std::abs(dstSatNumber - srcSatNumber))) / 2;

                if((_isSate == 2) && (maxPathHop > networkR) && (_tranProc == 0))
                {
                    // std::cout << "!src: " << m << ", dst: " << k << ", PathHop: " << maxPathHop << ", networkR: " << networkR << std::endl;
                    continue;
                }
                #endif
                data[m][k] += temp_data[index][k];
                // cout<<"data[" <<m<<"]["<<k << "]\t"<< data[m][k] <<endl;
                }
            }
        }
    }
  }

  // for(int m = 0; m < destNum; m++)
  // {
  //   for(int n = 0; n < (int)(totalTimeStep - 10); n++)
  //   {
  //       int index = m + n*destNum;
  //       if(index < destNum*(int)(totalTimeStep - 10))
  //       {
  //           for(int k = 0; k < destNum; k++)
  //           {
  //               data[m][k] += temp_data[index][k];
  //           }
  //       }
  //   }
  // }
  // cout<<"行：" <<data.size()<<"列："<<data[0].size()<<endl;
}

void installClient(const vector<vector<double>>& data, uint32_t numNodes, uint16_t servicePort){
//   double currentTime = Simulator::Now().GetSeconds(); // 单位为s
//   if(currentTime >= totalTimeStep) return;

  // 周期性为每个节点配置客户端应用程序，使其向其他节点发送数据包
  for (uint32_t i = 0; i < numNodes; ++i)
  {
    Ptr<Node> clientNode = sates.Get(i);
    // 循环目的地址
    for(uint32_t j = 0; j < numNodes; j++){
    //   int row = currentTime*numNodes + i;
      if(data[i][j] == 0.0) continue;

      uint32_t packetSize = 1024;   // 字节
      double dataRate = (double) data[i][j] * 1024.0 * 1024.0 * 1024.0;

      Ipv4Address ip_address = GetNodeServiceAddress(sates.Get(j));

      if(_tranProc){
        OnOffHelper client("ns3::TcpSocketFactory", Address(InetSocketAddress(ip_address, servicePort)));
        if(_isSate == 4)
        {// 流量表格过大，软件调整
          dataRate /= 10.0;
        }
        client.SetConstantRate(DataRate(dataRate), packetSize);  // 设置速率并指定包大小
        
        ApplicationContainer apps = client.Install(clientNode);
        apps.Start(Seconds(0));
        apps.Stop(Seconds(totalTimeStep));
      }
      else if( !_tranProc ){
        int maxPacketCount = 0;
        bool hasPacketRule = false;
        if ((_isSate == 1) && (_trafficMode == 0))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 10000.0;
          //cout<<maxPacketCount<<endl;
        }
        else if ((_isSate == 1) && (_trafficMode == 1))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 15.0;
        }
        else if ((_isSate == 2) && (_trafficMode == 0))
        {
          hasPacketRule = true;
          //cout<<data[i][j]<<endl;
          maxPacketCount = (data[i][j] * 1024.0 * 1024.0 *1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 20.0;
          //cout<<maxPacketCount<<endl;
        }
        else if ((_isSate == 2) && (_trafficMode == 1))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount = maxPacketCount / 30.0;
        }        
        else if((_isSate == 3) && (_trafficMode == 0))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 20.0;
        }
        else if((_isSate == 3) && (_trafficMode == 1))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 200.0;
        }
        else if((_isSate == 4) && (_trafficMode == 0))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 200.0;
        }
        else if((_isSate == 4) && (_trafficMode == 1))
        {
          hasPacketRule = true;
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 200.0;
        }
        if (!hasPacketRule)
        {
          continue;
        }
        if (maxPacketCount <= 0)
        {
          maxPacketCount = 1;
        }
        // cout<<maxPacketCount<<endl;
        double interPacketInterval = (double)(100.0)/maxPacketCount;   // 数据包间隔 100s的流量
        // double interPacketInterval = 0.00004;
        UdpClientHelper client(ip_address, servicePort);

        client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
        client.SetAttribute("Interval", TimeValue(Seconds(interPacketInterval)));
        client.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer apps = client.Install(clientNode);
        apps.Start(Seconds(0));
        apps.Stop(Seconds(totalTimeStep - 10)); //停止发送的时间
      }
    }
  }
}

// 为每个节点创建应用 client加server
void buildApp(){
  // if(_BreakDetect) return ;
  uint16_t servicePort = 9;
  uint32_t numNodes = sates.GetN();
  udpServers.clear();
  tcpSinks.clear();

  // 安装服务器应用程序到每个节点
  if(_tranProc){
    // tcp
    for (uint32_t i = 0; i < numNodes; ++i)
    {
      PacketSinkHelper server("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), servicePort));
      ApplicationContainer apps = server.Install(sates.Get(i));
      tcpSinks.push_back(DynamicCast<PacketSink>(apps.Get(0)));
      apps.Start(Seconds(0));
      apps.Stop(Seconds(totalTimeStep));
    }
  }else{ //udp
    for (uint32_t i = 0; i < numNodes; ++i)
    {
      UdpServerHelper server(servicePort);
      ApplicationContainer apps = server.Install(sates.Get(i));
      udpServers.push_back(DynamicCast<UdpServer>(apps.Get(0)));
      apps.Start(Seconds(0));
      apps.Stop(Seconds(totalTimeStep));
    }
  }

  if (offeredload == 0.0)
  {
    std::cout << "[TRAFFIC] offeredload=0，跳过流量矩阵和客户端创建" << std::endl
              << std::endl;
    return;
  }

  // GetData和installClient只按源/目的卫星索引访问该矩阵，不需要随仿真时长扩展行数。
  vector<vector<double>> data(numNodes, vector<double>(numNodes, 0));
  if(_trafficMode == 0)
  {// 区域热点流量
    // 默认复用324星流量矩阵，并按实际卫星数截取；可通过--trafficMatrix覆盖。
    if(_isSate == 1){
      GetData(data, numNodes, trafficMatrixFile);
    }
    else if(_isSate == 2)
    {
      cout<<"2"<<endl;
    }
    else if(_isSate == 3)
    {
      cout<<"3"<<endl;
    }
    else if(_isSate == 4)
    {   // TODO
      cout<<"4"<<endl;
    }

  }
  else if(_trafficMode == 1)
  {// 均匀流量
    if(_isSate == 1)
    {
      cout<<"1"<<endl;
    }
    else if(_isSate == 2)
    {
      cout<<"1"<<endl;
    }
    else if(_isSate == 3)
    {
      cout<<"1"<<endl;
    }
    else if(_isSate == 4)
    {
      cout<<"1"<<endl;
    }
  }

  installClient(data, numNodes, servicePort);
}



int
main (int argc, char *argv[])
{
  // #ifdef NS3_OPENFLOW

  // offeredload = 6.0;

  double requestedSimulationDuration = 0.0;
  CommandLine cmd;
  cmd.AddValue ("offeredload", "范围：0.5-6.0", offeredload);

  cmd.AddValue ("isSate", "传统拓扑模式使用：1=324, 2=351, 3=500, 4=432；JSON模式不决定节点数量", _isSate);
  cmd.AddValue ("consType", "传统拓扑模式使用：0=Walker Star, 1=Walker Delta", _consType);
  cmd.AddValue ("linkBandwidth", "默认链路带宽；JSON链路未写带宽时作为兜底值", linkBandwidth);
  cmd.AddValue("routingMode", "路由模式：0=OSPF，1=簇内/簇间路由", _SDNRoute);
  cmd.AddValue("tranProtocol", "0:UDP, 1:TCP", _tranProc);
  cmd.AddValue("trafficMatrix", "业务流量矩阵CSV文件", trafficMatrixFile);
  cmd.AddValue("outputDir", "仿真指标输出目录", metricsOutputDirectory);
  cmd.AddValue("writeRoutingTables", "是否输出调试用路由表文件", writeRoutingTables);
  cmd.AddValue("useJsonTopo", "是否使用 examples/link-selection/input/topology/json 中的JSON拓扑", _useJsonTopo);
  cmd.AddValue("jsonTopoPatchMode", "JSON模式后续时间片：false=全量快照，true=patch增量", _jsonTopoPatchMode);
  cmd.AddValue("nodesJson", "可选：初始节点JSON文件；默认input/topology/json/nodes_0s.json", nodesJsonFile);
  cmd.AddValue("topologyJson", "可选：初始链路JSON文件；默认input/topology/json/topology_0s.json", topologyJsonFile);
  cmd.AddValue("timeSlicesJson", "可选：时间片索引JSON文件；默认按input/topology/json文件名扫描", timeSlicesJsonFile);
  cmd.AddValue("linkOutputDir", "甲方时间序列JSON目录；设置后启用link_output模式", linkOutputDir);
  cmd.AddValue("simulationDuration", "仿真时长(s)；link_output模式下必须为正数", requestedSimulationDuration);
  cmd.Parse (argc, argv);

  if (!std::isfinite(offeredload) || offeredload < 0.0)
  {
    std::cerr << "[RUN:Error] offeredload必须是有限的非负数" << std::endl;
    return EXIT_FAILURE;
  }
  if (!std::isfinite(requestedSimulationDuration) || requestedSimulationDuration < 0.0)
  {
    std::cerr << "[RUN:Error] simulationDuration必须是有限的非负数" << std::endl;
    return EXIT_FAILURE;
  }
  if (!linkOutputDir.empty())
  {
    if (!_useJsonTopo)
    {
      std::cerr << "[RUN:Error] linkOutputDir要求useJsonTopo=true" << std::endl;
      return EXIT_FAILURE;
    }
    if (requestedSimulationDuration <= 0.0)
    {
      std::cerr << "[RUN:Error] link_output模式必须填写正数simulationDuration" << std::endl;
      return EXIT_FAILURE;
    }
    totalTimeStep = requestedSimulationDuration;
  }
  else
  {
    if (requestedSimulationDuration > 0.0)
    {
      totalTimeStep = requestedSimulationDuration;
    }
  }

  std::cout << "[RUN] 实验参数" << std::endl
            << "  offeredLoad   : " << offeredload << std::endl
            << "  duration      : " << totalTimeStep << " s" << std::endl
            << "  linkBandwidth : " << linkBandwidth << std::endl
            << "  routingMode   : " << _SDNRoute << std::endl
            << "  tranProc      : " << (_tranProc == 1 ? "TCP" : "UDP") << std::endl
            << "  trafficMatrix : " << trafficMatrixFile << std::endl
            << "  outputDir     : " << metricsOutputDirectory << std::endl
            << "  routeTables   : " << (writeRoutingTables ? "enabled" : "disabled") << std::endl
            << "  useJsonTopo   : " << (_useJsonTopo ? "true" : "false") << std::endl
            << "  jsonTopoMode  : " << (_jsonTopoPatchMode ? "patch" : "snapshot") << std::endl
            << "  linkOutputDir : " << (linkOutputDir.empty() ? "disabled" : linkOutputDir) << std::endl
            << std::endl;

  if (!_useJsonTopo)
  {
    sates_num = _isSate == 1 ? 324 : (_isSate == 2 ? 351 : (_isSate == 3 ? 500 : 432));
    orbit_num = _isSate == 1 ? 18 : (_isSate == 2 ? 27 : (_isSate == 3 ? 25 : 24));
    sate_num = sates_num / orbit_num;
  }
  
  std::chrono::steady_clock::time_point wallClockStart = std::chrono::steady_clock::now();

  if(_tranProc == 1){
    // 设置初始拥塞窗口大小为 2-10-20
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // 设置数据段大小（分段的最大传输单元，通常等同于 packet size）1024+20
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1024));

    // 设置快速重传的重复 ACK 计数阈值为 1（默认是 3）
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));
    // 启用选择性确认 (SACK)，提高快速恢复性能
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    // 设置初始慢启动阈值为 65535 字节 65535
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue(100000));

    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));

    // 设置发送和接受缓冲区
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(65535 * 10)); // 增加发送缓冲区
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(65535 * 10)); // 增加接收缓冲区
  }

  NS_LOG_INFO ("Create nodes.");

  initTopo();

  // 若未创建链路监控器，则为每个节点安装一个
  if (monitors.empty())
  {
    for (uint32_t i = 0; i < sates.GetN(); ++i)
    {
      Ptr<LinkUtilizationMonitor> monitor = CreateObject<LinkUtilizationMonitor>();
      monitor->SetLinkCapacity(linkBandwidth);
      monitor->SetStopTime(totalTimeStep);
      monitor->StartMonitoring(sates.Get(i));
      monitors.push_back(monitor);
    }
  }

  std::cout << "[TRAFFIC] 开始创建业务流" << std::endl;
  
  // 构建并启动应用
  buildApp();

  // ============ 新增部分 ============
  //Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();
  // //打开输出文件
  // std::stringstream filename;
  // filename << "max_hop_count_nsgaII" 
  //          << (_isSate == 1 ? "66" : (_isSate == 2 ? "324" : (_isSate == 3 ? "500" : "432")))
  //          << "_load" << offeredload << ".csv";
  // maxHopFile.open(filename.str());
  // maxHopFile << "Time(s),MaxHopCount" << std::endl;  // CSV 表头
  
  // // 调度第一次统计(从10秒开始,避开初始化阶段)
  // Simulator::Schedule(Seconds(1.0), &RecordMaxHopCount, monitor);
  // ==================================


  NS_LOG_INFO ("Run Simulation.");

  Ptr<FlowMonitor> monitor = InstallSimulationFlowMonitor();

  Simulator::Stop (Seconds (totalTimeStep)); // 设置仿真停止时间
  Simulator::Run ();
  double wallClockSeconds =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - wallClockStart).count();
  std::cout << "[RUN] Simulation wall-clock cost: " << wallClockSeconds << " s" << std::endl
            << std::endl;

  MetricsRecorder metricsRecorder(monitor,
                                  wallClockSeconds,
                                  CollectTaskApplicationMetrics(),
                                  metricsOutputDirectory);
  metricsRecorder.Record();
  Simulator::Destroy ();

  NS_LOG_INFO ("Done.");

  // #else
  // NS_LOG_INFO ("NS-3 OpenFlow is not enabled. Cannot run simulation.");
  // #endif // NS3_OPENFLOW
}
