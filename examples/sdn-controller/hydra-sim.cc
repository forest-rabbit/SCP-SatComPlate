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
#include <ns3/openflow-sdn-controller.h>
#include <ns3/simulator.h>
#include <ns3/time-series-adaptor.h>
#include "ns3/socket.h"
#include "ns3/tcp-socket-factory.h"

#include "all-node.h"
#include "cluster.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/node-container.h"
#include "ns3/openflow-module.h"
#include "ns3/cluster-module.h"
#include "ns3/log.h"
#include "ns3/openflow-switch-helper.h"
#include "ns3/flow-monitor.h"
#include "para.h"
#include "topo.h"
#include "json.hpp"

#include <numeric>
#include <random>
#include <sys/socket.h>
#include <fcntl.h>      // For fcntl
#include <sys/select.h> // For select
#include <cstring>      // For memset
#include <unistd.h>     // For close, read, write
#include <arpa/inet.h>  // For sockaddr_in, inet_ntoa


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("OpenFlowSDNExample");

bool verbose = false;
bool use_drop = false;
ns3::Time timeout = ns3::Seconds (0);

FlowMonitorHelper flowmonHelper;

std::mutex smutex;  // 定义全局的mutex
int breakSID = -1;   // 故障的从控制器ID
// 普通卫星向主控制器发送从控故障标志
bool m_slaveBreakInfo = false;

using json = nlohmann::json;

const int PORT = 8080;
const int HEADER_SIZE = 4;    // 4 字节用于表示消息长度

bool
SetVerbose (std::string value)
{
  verbose = true;
  return true;
}

void tobackup_test(){
  
  // #ifdef NS3_OPENFLOW

  master_migration();
  
  // #endif
}

void MasterRecoveryTest () {
  // MasterController->SendHeartbeat();
  Ptr<satelliteNode> master_node = DynamicCast<satelliteNode>(mcs.Get(masterID));
  master_node->SendIdentity();
  master_node->SendHeartbeat();
}

void SlaveRecovery(){
  if(breakSID == -1){
    Simulator::Schedule(Seconds(3), &SlaveRecovery);
    return ;
  }
  // 调用分簇模块重新分簇
  RelectCluster(breakSID, sates, satClusterNodes);
  updateTopo();
  
  smutex.lock();
  breakSID = -1;
  smutex.unlock();
}

void SlaveRecoveryTest () {
  // MasterController->SendHeartbeat();
  Ptr<satelliteNode> node = DynamicCast<satelliteNode>(satClusterNodes[0].Get(0));
  node->m_state = breakNode;
  Time curr = Simulator::Now();
  double currSec = curr.GetSeconds();
  cout << "time "<< currSec << "\t簇0\t从控制器节点" << node->GetId() << "故障" << endl;
  
  Simulator::Schedule(Seconds(3), &SlaveRecovery);
}


void calCtlPktPer(){
  // 输出初始化阶段控制开销
  double totalCtlPkt = 0.0;    // 字节
  double totalCtlPktOver = 0.0;    // 字节
  double totalCtlPkt0 = 0.0;    // 字节
  double totalCtlPktOver0 = 0.0;    // 字
  double totalCtlPkt1 = 0.0;    // 字节
  double totalCtlPktOver1 = 0.0;    // 字节
  double totalCtlPkt2 = 0.0;    // 字节
  double totalCtlPktOver2 = 0.0;    // 字节

		for(uint32_t i=0; i<orbit_num; i++){
			for(uint32_t j=0; j<sate_num; j++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(sateNodes[i].Get(j));
        totalCtlPkt += node->ctlPktRecv;
        totalCtlPktOver += node->ctlPktRecvOver;
        totalCtlPkt0 += node->ctlPktRecv0;
        totalCtlPktOver0 += node->ctlPktRecvOver0;
        totalCtlPkt1 += node->ctlPktRecv1;
        totalCtlPktOver1 += node->ctlPktRecvOver1;
        totalCtlPkt2 += node->ctlPktRecv2;
        totalCtlPktOver2 += node->ctlPktRecvOver2;
			}
		}
  
  
  std::cout << "Time:" << Simulator::Now().GetSeconds() << std::endl;
  std::cout << "总控制开销：" << totalCtlPktOver << "\t总接收字节数：" << totalCtlPkt << std::endl;
  std::cout << "主-从控制器之间控制开销：" << totalCtlPktOver0 << "\t接收字节数：" << totalCtlPkt0 << std::endl;
  std::cout << "从-从控制器之间控制开销：" << totalCtlPktOver1 << "\t接收字节数：" << totalCtlPkt1 << std::endl;
  std::cout << "从控制器-普通卫星之间控制开销：" << totalCtlPktOver2 << "\t接收字节数：" << totalCtlPkt2 << std::endl;
  Simulator::Schedule(Seconds(10), &calCtlPktPer);
}

double calCtlPkt(){
  // 输出初始化阶段控制开销
  double totalCtlPkt = 0.0;    // 字节
  double totalCtlPktOver = 0.0;    // 字节
  double totalCtlPkt0 = 0.0;    // 字节
  double totalCtlPktOver0 = 0.0;    // 字
  double totalCtlPkt1 = 0.0;    // 字节
  double totalCtlPktOver1 = 0.0;    // 字节
  double totalCtlPkt2 = 0.0;    // 字节
  double totalCtlPktOver2 = 0.0;    // 字节

		for(uint32_t i=0; i<orbit_num; i++){
			for(uint32_t j=0; j<sate_num; j++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(sateNodes[i].Get(j));
        totalCtlPkt += node->ctlPktRecv;
        totalCtlPktOver += node->ctlPktRecvOver;
        totalCtlPkt0 += node->ctlPktRecv0;
        totalCtlPktOver0 += node->ctlPktRecvOver0;
        totalCtlPkt1 += node->ctlPktRecv1;
        totalCtlPktOver1 += node->ctlPktRecvOver1;
        totalCtlPkt2 += node->ctlPktRecv2;
        totalCtlPktOver2 += node->ctlPktRecvOver2;
			}
		}
  
  std::cout << "总控制开销：" << totalCtlPktOver << "\t总接收字节数：" << totalCtlPkt << std::endl;
  std::cout << "主-从控制器之间控制开销：" << totalCtlPktOver0 << "\t接收字节数：" << totalCtlPkt0 << std::endl;
  std::cout << "从-从控制器之间控制开销：" << totalCtlPktOver1 << "\t接收字节数：" << totalCtlPkt1 << std::endl;
  std::cout << "从控制器-普通卫星之间控制开销：" << totalCtlPktOver2 << "\t接收字节数：" << totalCtlPkt2 << std::endl;
  return totalCtlPktOver;
}

double calBreakDetectDelay(){
  Ptr<satelliteNode> master = DynamicCast<satelliteNode>(mcs.Get(masterID));
  double maxT = 0.0;
  // double minT = 100.0;

  // double ret = 0.0;
  for(auto iter = master->m_breakInfo.begin(); iter != master->m_breakInfo.end(); iter ++){
    uint32_t nodeID = iter->first - sateBegID;
    Ptr<satelliteNode> node = DynamicCast<satelliteNode>(sates.Get(nodeID));
    if(node->m_state != ns3::breakNode) continue;
    maxT = max(maxT, iter->second - node->m_breakTime);
    // minT = min(minT, iter->second - node->m_breakTime);
    // ret += iter->second - node->m_breakTime;
  }
  // std::cout << "最大探测时间：" << maxT << "\t最小探测时间：" << minT << std::endl; 
  // if(master->m_breakPktNum == 0) return ret;
  // return ret/(master->m_breakPktNum);
  return maxT;
}

// //获取excel数据
// void GetData(vector<vector<float>> &data, const int destNum, std::string name)
// {
// 	std::ifstream inFile(name, std::ios::in);
// 	std::string lineStr;
//   std::cout << "GET DATA FROM " << name << "\tsateNum:" << destNum << std::endl;
//   int i = 0;
//   int j = 0;
// 	while (getline(inFile, lineStr))
// 	{
//     if(i >= (int)data.size()) break;
// 		std::stringstream ss(lineStr);
// 		std::string str;
// 		while (getline(ss, str, ','))
// 		{
// 			float num = stod(str);
//       int d = 1;   // 是否有流量突发
//       if(_TrafficBurst){
//         // 生成一个0到1之间的随机数
//         Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable> ();
//         double randomValue = rand->GetValue ();
//         if (randomValue < 0.25){
//           d = 2;
//           // std::cout << "row:" << i << "\tcol:" << j << "流量突发" << std::endl;
//         }
//       }
// 			data[i][j] = d * offeredload * num;     // Mbps
//       j ++;
//       if( j == destNum) j = 0;
// 		}
//     i ++;
// 	}
//   inFile.close();
// }

void GetData(vector<vector<double>>& data, const int destNum, std::string name)
{
  vector<vector<double>> temp_data(destNum*100, vector<double>(destNum, 0.0));
	std::ifstream inFile(name, std::ios::in);
	std::string lineStr;
  std::cout << "GET DATA FROM " << name << "\tsateNum:" << destNum << std::endl;
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
            j++;
            if( j == destNum) j = 0;
		}
        i++;
	}
  inFile.close();

  //流量矩阵累加 m:行 n:列 流量发送时间:100
  for(int m = 0; m < destNum; m++)
  {
    for(int n = 0; n < 100; n++)
    {
        int index = m + n*destNum;
        if(index < destNum*100)
        {
            for(int k = 0;k < destNum; k++)
            {
                data[m][k] += temp_data[index][k];
                // cout<<"data[" <<m<<"]["<<k << "]\t"<< data[m][k] <<endl;
            }
        }
    }
  }
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
        client.SetConstantRate(DataRate(dataRate), packetSize);  // 设置速率并指定包大小
        
        ApplicationContainer apps = client.Install(clientNode);
        apps.Start(Seconds(0));
        apps.Stop(Seconds(totalTimeStep));
      }
      else if( !_tranProc ){
        int maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
        // cout<<maxPacketCount<<endl;
        double interPacketInterval = (double)100/maxPacketCount;   // 数据包间隔 100s的流量
        // double interPacketInterval = 0.00004;
        UdpClientHelper client(ip_address, portStart);

        client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
        client.SetAttribute("Interval", TimeValue(Seconds(interPacketInterval)));
        client.SetAttribute("PacketSize", UintegerValue(1024));

        // cout << "node:" << clientNode->GetId() 
        //      << "\tmaxPacketCount:" << maxPacketCount
        //      << "\tinterPacketInterval:" << interPacketInterval  
        //      << "\tPacketSize:" << 1024  
        //      << endl;

        ApplicationContainer apps = client.Install(clientNode);
        apps.Start(Seconds(0));
        apps.Stop(Seconds(totalTimeStep));
      }
    }
  }

  // 周期性调度，根据动态分簇结果更新管控架构
  //Simulator::Schedule(Seconds(1), &installClient, data, numNodes, portStart, nodes);
}

// 为每个节点创建应用 client加server
void buildApp(){
  vector<vector<double>> data(sates.GetN()*totalTimeStep, vector<double>(sates.GetN(), 0));
  // 获取excel数据
  // GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Iridium).csv");
  if(_isSate == 1){
    if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1).csv");
    else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1)_100Mbps.csv");
    else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1)_500Mbps.csv");
  }
  else if(_isSate == 3)
  {
    if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3).csv");
    else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3)_100Mbps.csv");
    else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3)_500Mbps.csv");    
  }
  else{
    if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat2).csv");
    else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat2)_100Mbps.csv");
    else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat2)_500Mbps.csv");
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

struct SimInfo{
  uint32_t txPackets;
  uint32_t rxPackets;
};

void dealSimInfo(Ptr<FlowMonitor> monitor, double ctlPkt){
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::map<uint32_t, SimInfo> simInfoMap;

  double SumDelayTime = 0;        //!<总时延.
  double SumJitterTime = 0;       //!<总抖动.
  double Latency = 0.0;           //!<平均端到端时延.           
  double Jitter = 0.0;            //!<平均抖动.
  double Throughput = 0.0;        //!< 吞吐量.
  double clusterStablity = 0;      //!< 簇稳定性.

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

  uint32_t sumChangeClusterNum = 0; //!< 总簇ID更新次数.
  // Ipv4FlowClassifier::FiveTuple t;

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
  {
    if(i->second.packetPrio == 0)  // 控制信息
    {
      TxPacketsCtl += i->second.txPackets;
      RxPacketsCtl += i->second.rxPackets;
      lostPacketsCtl += i->second.lostPackets;
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
      SumDelayTime += i->second.delaySum.GetSeconds();
      SumJitterTime += i->second.jitterSum.GetSeconds();
      Throughput += i->second.rxBytes *8.0 / (totalTimeStep - 10.0) / 1000 / 1000;
    }
  }

  Latency = SumDelayTime / double(RxPackets);    // s
  Jitter = SumJitterTime / double(RxPackets);    // s

  LatencyCtl = SumDelayTimeCtl / double(RxPacketsCtl);    // s
  JitterCtl = SumJitterTimeCtl / double(RxPacketsCtl);    // s

  vector<long long> txBytes;
  vector<double> linkUtil;
  for(uint32_t i=0; i<monitors.size(); i++){
    for(auto& iter : monitors[i]->m_totalDevice )
      txBytes.push_back(iter.second);
  }
  for(uint32_t i=0; i<txBytes.size(); i++){
    linkUtil.push_back((double)txBytes[i]*8.0 / totalTimeStep / linkBandwidth);
    // linkUtil.push_back((double)txBytes[i]*8.0 / (totalTimeStep) / linkBandwidth);
  }
  // 计算平均和最大链路利用率
  double sumLinkUtil = std::accumulate(linkUtil.begin(), linkUtil.end(), 0.0);
  double AvgLinkUtil = sumLinkUtil / linkUtil.size();
  double MaxLinkUtil = *std::max_element(linkUtil.begin(), linkUtil.end());


  for(uint32_t i=0; i<changeClusterID.size(); i++){
    sumChangeClusterNum += changeClusterID[i];
    // std::cout << "node:" << i << "  Cluster Stability: " << 100 - (changeClusterID[i]/totalTimeStep )* 100  << " %\n";
  }

  double mean = (double)sumChangeClusterNum / sates_num;
  // 计算方差
  double variance = 0.0;
  for(uint32_t i=0; i<changeClusterID.size(); i++) {
      variance += std::pow(changeClusterID[i] - mean, 2); // 平方差
  }
  variance /= sates_num; // 平均
  // std::cout << "Cluster Stability方差: " << 100 - (variance/totalTimeStep )* 100  << " %\n";
  std::cout << "Cluster Stability方差: " << (variance/totalTimeStep )  << " %\n";

  clusterStablity = (double)sumChangeClusterNum/((totalTimeStep) * sates_num);

    std::cout << "--------------业务数据性能--------------" << std::endl;
    std::cout << "  Tx Packets: " << TxPackets << " p\n"
                 "  Rx Packets: " << RxPackets << " p\n"
                 "  lostPackets: " << lostPackets << " p\n"
                 "  Latency: " << Latency*1000 << " ms\n"
                 "  Throughput: "<< Throughput/1000 << " Gbps\n"
                 "  Jitter: " << Jitter*1000 << " ms\n"
                 "  Loss Packet Ratio: " << (double)lostPackets * 100 / TxPackets << " %\n"
                 ;

    std::cout << "--------------控制信息性能--------------" << std::endl;
    std::cout << "  Tx Packets: " << TxPacketsCtl << " p\n"
                 "  Rx Packets: " << RxPacketsCtl << " p\n"
                 "  lostPackets: " << lostPacketsCtl << " p\n"
                 "  Latency: " << LatencyCtl*1000 << " ms\n"
                 "  Throughput: "<< ThroughputCtl/1000 << " Gbps\n"
                 "  Jitter: " << JitterCtl*1000 << " ms\n"
                 "  Loss Packet Ratio: " << (double)lostPacketsCtl * 100 / TxPacketsCtl << " %\n"
                 ;

    std::cout << "--------------Performance--------------" << std::endl;
    std::cout << "  Average Link Utilization: " << AvgLinkUtil * 100 << " %\n"
                 "  Maximum Link Utilization: " << MaxLinkUtil * 100 << " %\n"
                 "  Control Overhead: " << ctlPkt << " \n"
                 "  Cluster Stability: " << 100 - clusterStablity * 100  << " %\n"
                 ;
}

// 更新全网节点
void post_nodes(json jsonData){
  // 解析json对象，并确定卫星数量，地面站数量，轨道数量等
  NS_LOG_INFO ("Create nodes.");
  gwsnum = 0;
  sates_num = 0;
  std::vector<uint32_t> gs_nodes; //临时存储 gs node
  std::vector<uint32_t> sat_nodes; //临时存储 sat node
    
  for (const auto& item : jsonData["data"]) {
      if (item["node_type"] == "gs"){
          gwsnum++;
          gs_nodes.push_back(item["node_id"]) ; 
      } else{
          std::string numStr = std::to_string((int)item["node_id"]);
          if(numStr[5]-'0' == 1&&numStr[4]-'0'==0) sate_num++; //只统计第一个轨道卫星数目
          sates_num++;
          sat_nodes.push_back(item["node_id"]) ; 
      } 
  }
    std::sort(gs_nodes.begin(), gs_nodes.end()) ; //从小到大排序
    std::sort(sat_nodes.begin(), sat_nodes.end()) ;

    uint32_t cnt = 0;
    //uint32_t gsBegin = cnt;  // cnt用来记录下标， gsBegin为 gs的起始位置
    for(const auto& i : gs_nodes){
        node_id[cnt] = i ;
        id_node[i] = cnt; 
        cnt++;
    }
    uint32_t satBegin = cnt ; //satBegin为 sat的起始位置
    sateBegID = satBegin;
    for(const auto& i : sat_nodes){
        node_id[cnt] = i;
        id_node[i] = cnt;
        cnt++; 
    }
    //轨道数目
  orbit_num = sates_num / sate_num;

  // 修改： 根据请求的卫星和地面站节点数---更新allnode
  //initTopo();//初始拓扑调用了初始分簇的结果（关掉动态分簇）
}

string post_links(string data){
  string retData = "";

  if(first_flag == false){
    first_flag = true;
    initTopoSim(data);
  }
  updateTopoSim(data, retData);
  
  /* retData 需要替换为更新后的路由 string */

  // retData = data;
  return retData;
}

string post_route(string data){
  string retData = "";

  updateRouteSim(data, retData);
  
  /* retData 需要替换为创建后的路由 string */

  // retData = data;
  return retData;
}

void delete_route(string data){
  deleteRouteSim(data);
}

string post_cluster(){
  string retData = "";

  updateClusterSim(retData);

  return retData;
}

bool set_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return false;
    flags |= O_NONBLOCK;
    return (fcntl(sock, F_SETFL, flags) != -1);
}


// 读取指定数量的字节
bool read_n_bytes(int sock, char* buffer, int n) {
    int total_read = 0;
    while (total_read < n) {
        ssize_t bytes = recv(sock, buffer + total_read, n - total_read, 0);
        if (bytes < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // 无更多数据
                break;
            } else {
                perror("recv error");
                return false;
            }
        } else if (bytes == 0) {
            // // 客户端断开连接
            // std::cout << "客户端断开连接。\n";
            // Simulator::Stop();
            return false;
        }
        total_read += bytes;
    }
    return true;
}

// 发送消息，带有长度前缀
bool send_message(int sock, const std::string& message) {
    uint32_t len = htonl(message.size());
    std::vector<char> buffer(HEADER_SIZE + message.size());
    memcpy(buffer.data(), &len, HEADER_SIZE);
    memcpy(buffer.data() + HEADER_SIZE, message.c_str(), message.size());

    int total_sent = 0;
    int to_send = buffer.size(); 
    while (total_sent < to_send) {                      
        ssize_t bytes = send(sock, buffer.data() + total_sent, to_send - total_sent, 0);
        //cout<<"发送字节数 "<< bytes<<endl;
        // cout<<*buffer.data()<<endl;
        //cout<<total_sent<<endl;
        if (bytes < 0) {
            perror("send error");
            return false;
        }
        total_sent += bytes;
    }
    std::cout << "发送数据: " << message << "\n";
    return true;
}

// 封装接收和处理数据的函数
void receive_and_process(int client_socket, std::string& recv_buffer, bool& running) {
    char buffer[4096];
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes_received < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            perror("recv error");
            running = false;
            return ;
        }
    } else if (bytes_received == 0) {
        std::cout << "客户端断开连接。\n";
        running = false;
        return ;
    } else {
        recv_buffer.append(buffer, bytes_received);
    }

    // 处理接收到的数据
    while (true) {
        if (recv_buffer.size() < HEADER_SIZE) {
            // 不足以读取消息长度
            break;
        }

        // 读取消息长度
        uint32_t msg_len;
        memcpy(&msg_len, recv_buffer.data(), HEADER_SIZE);
        msg_len = ntohl(msg_len);

        if (recv_buffer.size() < HEADER_SIZE + msg_len) {
            // 不足以读取完整的消息
            break;
        }

        // 读取消息体
        std::string message = recv_buffer.substr(HEADER_SIZE, msg_len);
        recv_buffer.erase(0, HEADER_SIZE + msg_len);

        std::cout << "收到数据: " << message << "\n";

        // 解析 JSON 数据
        try {
            json jsonRecvd = json::parse(message);

            // 根据接收到的数据格式，进行对应的处理
            if(jsonRecvd["func"] == "nodes"){
              // 更新全网节点
              post_nodes(jsonRecvd);
            }else if(jsonRecvd["func"] == "links"){
              // 更新全网拓扑
              string retString = post_links(message);
              send_message(client_socket, retString);
            }else if(jsonRecvd["func"] == "route"){
              // 更新面向连接路由
              string retString = post_route(message);
              cout<<
              send_message(client_socket, retString);
            }else if(jsonRecvd["func"] == "delete_route"){
              // 删除路由
              delete_route(message);
            }else if(jsonRecvd["func"] == "cluster"){
              // 获取分簇信息
              string retString = post_cluster();
              send_message(client_socket, retString);
            }else if(jsonRecvd["func"] == "end"){
              // 仿真结束
              Simulator::Stop ();
            }

            // // 根据接收到的数据进行处理
            // if (received_json.contains("command")) {
            //     std::string command = received_json["command"];
            //     json response_json;
            //     if (command == "greet") {
            //         response_json["response"] = "Hello from server!";
            //     } else if (command == "time") {
            //         // 返回服务器当前时间
            //         auto now = std::chrono::system_clock::now();
            //         std::time_t now_time = std::chrono::system_clock::to_time_t(now);
            //         response_json["response"] = std::string(std::ctime(&now_time));
            //     } else if (command == "echo") {
            //         if (received_json.contains("data")) {
            //             response_json["response"] = received_json["data"];
            //         } else {
            //             response_json["response"] = "No data to echo.";
            //         }
            //     } else {
            //         response_json["response"] = "Unknown command.";
            //     }

            //     std::string response_str = response_json.dump();
            //     if (!send_message(client_socket, response_str)) {
            //         running = false;
            //         return;
            //     }
            //     std::cout << "发送数据: " << response_str << "\n";
            // }
        }
        catch (json::parse_error& e) {
            std::cerr << "JSON 解析错误: " << e.what() << "\n";
        }
    }
    running = true;
    Simulator::Schedule(Seconds(1), &receive_and_process, client_socket, recv_buffer, running);
}


int
main (int argc, char *argv[])
{
  // #ifdef NS3_OPENFLOW

  // offeredload = 6.0;

  _mode = 0;
  _sim = 1;    // 与仿真中心对接模式

  CommandLine cmd;
  cmd.AddValue ("offeredload", "范围：0.5-6.0", offeredload);
  cmd.AddValue ("SDNRoute", "0:OSPF, 1:Hydra路由", _SDNRoute);
  cmd.AddValue ("isSate", "1:60颗, 2:108颗, 3:500颗,", _isSate);
  cmd.AddValue ("consType", "Some parameter", _consType);  
  cmd.AddValue ("linkBandwidth", "Some parameter", linkBandwidth);
  cmd.AddValue("tranProtocol", "0:UDP, 1:TCP", _tranProc);
  cmd.Parse (argc, argv);
  std::cout << "offeredLoad:" << offeredload 
            << "\tisSDNRoute:" << _SDNRoute
            << "\tlinkBandwidth:" << linkBandwidth
            << "\ttranProc:" << (_tranProc == 1 ? "TCP" : "UDP")
            << std::endl;

  // sates_num = _isSate == 1 ? 60 : (_isSate == 2 ? 108 : 500);   // N: LEO卫星总数
  // orbit_num = _isSate == 1 ? 6 : (_isSate == 2 ? 12 : 25);   // No: 轨道数
  // sate_num = sates_num / orbit_num;

    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // 创建 socket 文件描述符
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket 失败");
        exit(EXIT_FAILURE);
    }

    // 强制绑定端口
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        perror("setsockopt 失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 绑定地址和端口
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 监听所有接口
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address,
             sizeof(address)) < 0) {
        perror("bind 失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 设置服务器 socket 为非阻塞
    // if (!set_non_blocking()) {
    //     perror("无法将服务器 socket 设置为非阻塞");
    //     close(server_fd);
    //     exit(EXIT_FAILURE);
    // }

    // 开始监听
    if (listen(server_fd, 1) < 0) { // 仅允许一个连接
        perror("listen 失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // std::cout << "服务器正在监听端口 " << PORT << "...\n";

    // 等待客户端连接
    while (true) {
        client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (client_socket < 0) {
          continue;
        }

        std::string client_ip = inet_ntoa(address.sin_addr);
        std::cout << "接收到来自 " << client_ip << " 的连接。\n";

        // 设置客户端 socket 为非阻塞
        // if (!set_non_blocking(client_socket)) {
        //     perror("无法将客户端 socket 设置为非阻塞");
        //     close(client_socket);
        //     continue;
        // }

        break; // 只接受一个客户端
    }

    // 主循环，定期调用 receive_and_process
    bool running = true;
    std::string recv_buffer;
    while (running) {
      receive_and_process(client_socket, recv_buffer, running);
    }

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

  if(_mode == 3) clusterUpdateStep *= 3;   // 簇更新频率(s) 

  std::cout << "开始测试数据包发送！" << std::endl;
  
  // // 构建并启动应用
  // buildApp();

  // 各个节点开启监听
  for(uint32_t  i=0; i<satClusterNodes.size(); i++){
    for(uint32_t  j=0; j<satClusterNodes[i].GetN(); j++){
      Ptr<satelliteNode> temp = DynamicCast<satelliteNode>(satClusterNodes[i].Get(j));
      temp->StartRecvPacket_sate();
    }
  }

  for(uint32_t i=0; i<mcs.GetN(); i++){
    Ptr<satelliteNode> node = DynamicCast<satelliteNode>(mcs.Get(i));
    node->StartRecvPacket_master();
  }
  
  // 地面主控发送心跳包测试
  if(_mode == 0){
    Ptr<satelliteNode> master_node = DynamicCast<satelliteNode>(mcs.Get(0));
    master_node->SendIdentity();
    master_node->SendHeartbeat();
  }
  else if(_mode == 1){
    // 地面主控到地面备份主控的迁移测试
    tobackup_test();
    Simulator::Schedule(Seconds(40), &tobackup_test);
  }
  else if(_mode == 2){
    // 地面主控到星上临时备份主控的迁移以及恢复测试
    Time starttime = Seconds(40.0);
    Simulator::Schedule(starttime, &MasterRecoveryTest);
  }
  else if(_mode == 3){
    Ptr<satelliteNode> master_node = DynamicCast<satelliteNode>(mcs.Get(0));
    master_node->SendIdentity();
    master_node->SendHeartbeat();

    // 从控制器失效场景下的迁移以及恢复测试
    // 40s的时候，设置某从控制器失效
    Time starttime = Seconds(32.0);
    Simulator::Schedule(starttime, &SlaveRecoveryTest);
  }

  //
  // Now, do the actual simulation.
  //
  NS_LOG_INFO ("Run Simulation.");

  Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

  // Simulator::Schedule(Seconds(10), &calCtlPktPer);

  Simulator::Stop (Seconds (totalTimeStep)); // 设置仿真停止时间
  Simulator::Run ();

  double ctlPkt = calCtlPkt();
  double breakDetectDelay = 0;
  if(_BreakDetect){
    breakDetectDelay = calBreakDetectDelay(); // 返回值为s
    std::cout << "主控制器探测故障时间：" << breakDetectDelay << std::endl;
  }

  Simulator::Destroy ();

  // 在 Simulator::Run() 之后
  dealSimInfo(monitor, ctlPkt);

  if(_BreakDetect)
    std::cout << "  breakNode Detect time: " << breakDetectDelay * 1000 << " ms\n" ;

  NS_LOG_INFO ("Done.");

  // #else
  // NS_LOG_INFO ("NS-3 OpenFlow is not enabled. Cannot run simulation.");
  // #endif // NS3_OPENFLOW
}