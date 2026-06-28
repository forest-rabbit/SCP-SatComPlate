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
#include <cstdint>
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
#include "para.h"
#include "topo.h"

#include <numeric>
#include <random>
#include <ctime>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample");

ns3::Time timeout = ns3::Seconds (0);

FlowMonitorHelper flowmonHelper;


void GetData(vector<vector<double>>& data, const int destNum, std::string name)
{
  // destNum来自当前实际创建的卫星数量。JSON 66星模式下这里会按66列读取，
  // 即使当前文件名仍是traffic_matrix(324).csv；后续若提供66星流量文件，应在buildApp中切换路径。
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

void installClient(vector<vector<double>> data, uint32_t numNodes, uint16_t portStart){
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

      Ptr<Ipv4> destIp = sates.Get(j)->GetObject<Ipv4>(); //获取目的节点
      Ipv4Address ip_address = destIp->GetAddress(1, 0).GetLocal(); //设置为第一个网卡的IP 

      if(_tranProc){
        OnOffHelper client("ns3::TcpSocketFactory", Address(InetSocketAddress(ip_address, portStart)));
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
        UdpClientHelper client(ip_address, portStart);

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
  vector<vector<double>> data(sates.GetN()*totalTimeStep, vector<double>(sates.GetN(), 0));
  // 获取excel数据
  // GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Iridium).csv");
  if(_trafficMode == 0)
  {// 区域热点流量
    // 目前JSON模式仍复用原324星流量矩阵文件，并按实际卫星数截取读取。
    // 如果甲方提供traffic_matrix(66).csv，这里应优先改为读取66星文件。
    if(_isSate == 1){
      GetData(data, sates.GetN(), "examples/link-selection/Trafficdata/traffic_matrix(324).csv");
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

  // 预设端口号范围
  uint16_t portStart = 9; // well-known echo port number
  uint32_t numNodes = sates.GetN();

  // 为每个节点分配一个唯一的端口号
  std::map<Ptr<Node>, uint16_t> nodePortMap;
  for (uint32_t i = 0; i < sates_num; ++i)
  {
    uint16_t port = portStart + i; // 假设端口号从9开始，每个节点增加1
    nodePortMap[sates.Get(i)] = port;
  }

  // 安装服务器应用程序到每个节点
  if(_tranProc){
    // tcp
    for (uint32_t i = 0; i < numNodes; ++i)
    {
      PacketSinkHelper server("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), nodePortMap[sates.Get(i)]));
      ApplicationContainer apps = server.Install(sates.Get(i));
      apps.Start(Seconds(0));
      apps.Stop(Seconds(totalTimeStep));
    }
  }else{ //udp
    for (uint32_t i = 0; i < numNodes; ++i)
    {
      UdpServerHelper server(nodePortMap[sates.Get(i)]);
      ApplicationContainer apps = server.Install(sates.Get(i));
      apps.Start(Seconds(0));
      apps.Stop(Seconds(totalTimeStep));
    }
  }
  
  installClient(data, numNodes, portStart);
}



void dealSimInfo(Ptr<FlowMonitor> monitor, double ctlPkt){
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();


  double SumDelayTime = 0;        //!<总时延.
  double SumJitterTime = 0;       //!<总抖动.
  double Latency = 0.0;           //!<平均端到端时延.  
  double Jitter = 0.0;            //!<平均抖动.
  double Throughput = 0.0;        //!< 吞吐量.


  double SumDelayTimeCtl = 0;        //!<总时延.
  double SumJitterTimeCtl = 0;       //!<总抖动.
  double LatencyCtl = 0.0;           //!<平均端到端时延.           
  double JitterCtl = 0.0;            //!<平均抖动.
  double ThroughputCtl = 0.0;        //!< 吞吐量.

  uint32_t lostPackets = 0;       //!< 丢失数据包数.
  uint32_t TxPackets = 0;         //!< 发送数据包数.
  uint32_t RxPackets = 0;         //!< 接收数据包数.
  uint32_t lostPacketsCtl = 0;       //!< 丢失数据包数.
  uint32_t TxPacketsCtl = 0;         //!< 发送数据包数.
  uint32_t RxPacketsCtl = 0;         //!< 接收数据包数.
  uint32_t TxBytes = 0;         //!< 发送字节数.
  uint32_t TxBytesCtl = 0;         //!< 发送字节数.

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
  {
    if(i->second.packetPrio == 0)  // 控制信息
    {
      TxPacketsCtl += i->second.txPackets;
      RxPacketsCtl += i->second.rxPackets;
      lostPacketsCtl += i->second.lostPackets;
      TxBytesCtl += i->second.txBytes;
      SumDelayTimeCtl += i->second.delaySum.GetSeconds();
      SumJitterTimeCtl += i->second.jitterSum.GetSeconds();
      ThroughputCtl += i->second.rxBytes *8.0 / (totalTimeStep - 10.0) / 1000 / 1000;
    }else if(i->second.packetPrio == 1){
      // t = classifier->FindFlow(i->first);
      // std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
      //               << t.destinationAddress << ")\n";
      TxPackets += i->second.txPackets;
      RxPackets += i->second.rxPackets;
      lostPackets += i->second.lostPackets;
      TxBytes += i->second.txBytes;
      SumDelayTime += i->second.delaySum.GetSeconds();
      SumJitterTime += i->second.jitterSum.GetSeconds();
      Throughput += i->second.rxBytes *8.0 / (totalTimeStep - 10.0) / 1000 / 1000;

    }
  }

  Latency = SumDelayTime / double(RxPackets);    // s
  Jitter = SumJitterTime / double(RxPackets);    // s

  LatencyCtl = SumDelayTimeCtl / double(RxPacketsCtl);    // s
  JitterCtl = SumJitterTimeCtl / double(RxPacketsCtl);    // s



  // vector<long long> txBytes;
  // vector<double> linkUtil;
  // for(uint32_t i=0; i<monitors.size(); i++){
  //   for(auto& iter : monitors[i]->m_totalDevice )
  //     txBytes.push_back(iter.second);
  //     //cout<<"txBytes size:"<<txBytes.size()<<endl;
  // }
  // for(uint32_t i=0; i<txBytes.size(); i++){
  //   linkUtil.push_back((double)txBytes[i]*8.0 / totalTimeStep / linkBandwidth * 300.0);
  //   // linkUtil.push_back((double)txBytes[i]*8.0 / (totalTimeStep) / linkBandwidth);
  // }
  // // 计算平均和最大链路利用率
  // double sumLinkUtil = std::accumulate(linkUtil.begin(), linkUtil.end(), 0.0);
  // double AvgLinkUtil = sumLinkUtil / linkUtil.size();
  // double MaxLinkUtil = *std::max_element(linkUtil.begin(), linkUtil.end());




    std::cout << "--------------业务数据性能--------------" << std::endl;
    std::cout << "  Tx Packets: " << TxPackets << " p\n"
                 "  Tx Bytes: " << TxBytes << " bytes\n"
                 "  Rx Packets: " << RxPackets << " p\n"
                 "  lostPackets: " << lostPackets << " p\n"
                 "  Latency: " << Latency*1000 << " ms\n"
                 "  Throughput: "<< Throughput << " Gbps\n"
                 "  Jitter: " << Jitter*1000 << " ms\n"
                 "  Loss Packet Ratio: " << (double)lostPackets * 100 / TxPackets << " %\n\n"

                 ;

    std::cout << "--------------控制信息性能--------------" << std::endl;
    std::cout << "  Tx Packets: " << TxPacketsCtl << " p\n"
                 "  Tx Bytes: " << TxBytesCtl << " bytes\n"
                 "  Rx Packets: " << RxPacketsCtl << " p\n"
                 "  lostPackets: " << lostPacketsCtl << " p\n"
                 "  Latency: " << LatencyCtl*1000 << " ms\n"
                 "  Throughput: "<< ThroughputCtl/1000 << " Gbps\n"
                 "  Jitter: " << JitterCtl*1000 << " ms\n"
                 "  Loss Packet Ratio: " << (double)lostPacketsCtl * 100 / TxPacketsCtl << " %\n"
                 ;

    // std::cout << "--------------Performance--------------" << std::endl;
    // std::cout << "  Average Link Utilization: " << AvgLinkUtil * 100 << " %\n"
    //             "  Maximum Link Utilization: " << MaxLinkUtil * 100 << " %\n"
    //              "  Control Overhead: " << ctlPkt << " \n"
    //              ;
}

int
main (int argc, char *argv[])
{
  // #ifdef NS3_OPENFLOW

  // offeredload = 6.0;

  CommandLine cmd;
  cmd.AddValue ("offeredload", "范围：0.5-6.0", offeredload);

  cmd.AddValue ("isSate", "传统拓扑模式使用：1=324, 2=351, 3=500, 4=432；JSON模式不决定节点数量", _isSate);
  cmd.AddValue ("consType", "传统拓扑模式使用：0=Walker Star, 1=Walker Delta", _consType);
  cmd.AddValue ("linkBandwidth", "默认链路带宽；JSON链路未写带宽时作为兜底值", linkBandwidth);
  cmd.AddValue("tranProtocol", "0:UDP, 1:TCP", _tranProc);
  cmd.AddValue("useJsonTopo", "是否使用 examples/link-selection/Topodata/json 中的JSON拓扑", _useJsonTopo);
  cmd.AddValue("jsonTopoPatchMode", "JSON模式后续时间片：false=全量快照，true=patch增量", _jsonTopoPatchMode);
  cmd.AddValue("nodesJson", "可选：初始节点JSON文件；默认Topodata/json/nodes_0s.json", nodesJsonFile);
  cmd.AddValue("topologyJson", "可选：初始链路JSON文件；默认Topodata/json/topology_0s.json", topologyJsonFile);
  cmd.AddValue("timeSlicesJson", "可选：时间片索引JSON文件；默认按Topodata/json文件名扫描", timeSlicesJsonFile);
  cmd.Parse (argc, argv);
  std::cout << "[RUN] 实验参数" << std::endl
            << "  offeredLoad   : " << offeredload << std::endl
            << "  linkBandwidth : " << linkBandwidth << std::endl
            << "  tranProc      : " << (_tranProc == 1 ? "TCP" : "UDP") << std::endl
            << "  useJsonTopo   : " << (_useJsonTopo ? "true" : "false") << std::endl
            << "  jsonTopoMode  : " << (_jsonTopoPatchMode ? "patch" : "snapshot") << std::endl
            << std::endl;

  if (!_useJsonTopo)
  {
    sates_num = _isSate == 1 ? 324 : (_isSate == 2 ? 351 : (_isSate == 3 ? 500 : 432));
    orbit_num = _isSate == 1 ? 18 : (_isSate == 2 ? 27 : (_isSate == 3 ? 25 : 24));
    sate_num = sates_num / orbit_num;
  }
  
  // 记录开始时间
  clock_t start = clock();

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

  Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

  Simulator::Stop (Seconds (totalTimeStep)); // 设置仿真停止时间
  Simulator::Run ();
    // 记录结束时间
    clock_t end = clock();

    // 计算时间差并转换为秒
    double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
    std::cout << "Simulation real - time cost: " << duration << " s" << std::endl
              << std::endl;
  Simulator::Destroy ();

  // 在 Simulator::Run() 之后
  dealSimInfo(monitor, 0);

  NS_LOG_INFO ("Done.");

  // #else
  // NS_LOG_INFO ("NS-3 OpenFlow is not enabled. Cannot run simulation.");
  // #endif // NS3_OPENFLOW
}
