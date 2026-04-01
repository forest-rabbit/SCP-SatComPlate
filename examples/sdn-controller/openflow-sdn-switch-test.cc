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

#include <numeric>
#include <random>

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

  smutex.lock();
  breakSID = node->GetId();
  smutex.unlock();

  Time curr = Simulator::Now();
  double currSec = curr.GetSeconds();
  cout << "time "<< currSec << "\t簇0\t从控制器节点" << node->GetId() << "故障" << endl;
  SlaveRecovery();
  
  // Simulator::Schedule(Seconds(1), &SlaveRecovery);
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
    if(_slaveMode){
      for(int i = 0; i < scsnum; i++){
        Ptr<satelliteNode> node = DynamicCast<satelliteNode>(slavescs.Get(i));
        totalCtlPkt += node->ctlPktRecv;
        totalCtlPktOver += node->ctlPktRecvOver;
        totalCtlPkt0 += node->ctlPktRecv0;
        totalCtlPktOver0 += node->ctlPktRecvOver0;
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
    if(_slaveMode){
    for(int i = 0; i < scsnum; i++){
      Ptr<satelliteNode> node = DynamicCast<satelliteNode>(slavescs.Get(i));
      totalCtlPkt += node->ctlPktRecv;
      totalCtlPktOver += node->ctlPktRecvOver;
      totalCtlPkt0 += node->ctlPktRecv0;
      totalCtlPktOver0 += node->ctlPktRecvOver0;
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
    if(iter->first < (uint32_t)sateBegID) continue;
    uint32_t nodeID = iter->first - sateBegID;
    Ptr<satelliteNode> node = DynamicCast<satelliteNode>(sates.Get(nodeID));
    if(node->m_state != ns3::breakNode) continue;
    maxT = max(maxT, iter->second - node->m_breakTime);
    if(_BreakDetect) std::cout << "breakID:" << node->GetId() << "\tbreakTime:" << node->m_breakTime << std::endl;
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

  if(_trafficMode == 0)
  {
    #ifdef _trafficDown
    int satePerOrbit;
    int networkR;
    if((_isSate == 3) && (_tranProc == 0))
    {
        satePerOrbit = (int)sates_num / orbit_num;
        networkR = (satePerOrbit + orbit_num) / 6;
    }
    if((_isSate == 4) && (_tranProc == 0))
    {   // TODO
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

                if((_isSate == 3) && (maxPathHop > networkR) && (_tranProc == 0))
                {
                    // std::cout << "!src: " << m << ", dst: " << k << ", PathHop: " << maxPathHop << ", networkR: " << networkR << std::endl;
                    continue;
                }
                else if((_isSate == 4) && (maxPathHop > networkR) && (_tranProc == 0))
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
  else if(_trafficMode == 1)
  {
    #ifdef _trafficDown
    int satePerOrbit;
    int networkR;
    satePerOrbit = (int)sates_num / orbit_num;
    if((_isSate == 2) && (_tranProc == 0))
        networkR = (satePerOrbit + orbit_num) / 8;
    else if((_isSate == 3) && (_tranProc == 0))
        networkR = (satePerOrbit + orbit_num) / 10;
    else if((_isSate == 4) && (_tranProc == 0))
        networkR = (satePerOrbit + orbit_num) / 10;
    #endif

    for(int m = 0; m < destNum; m++)
    {
        #ifdef _trafficDown
        int srcSatOrbit = m / satePerOrbit;
        int srcSatNumber = m % satePerOrbit;
        #endif
        for(int n = 0; n < (int)(totalTimeStep - 10); n++)
        {
            int index = m + n*destNum;
            if(index < destNum*(int)(totalTimeStep - 10))
            {
                for(int k = 0;k < destNum; k++)
                {
                #ifdef _trafficDown
                int dstSatOrbit = k / satePerOrbit;
                int dstSatNumber = k % satePerOrbit;

                int maxPathHop = (std::min(std::abs(dstSatOrbit - srcSatOrbit), (int)orbit_num - std::abs(dstSatOrbit - srcSatOrbit)) 
                                    + std::min(std::abs(dstSatNumber - srcSatNumber), satePerOrbit - std::abs(dstSatNumber - srcSatNumber))) / 2;

                if(((_isSate == 2) || (_isSate == 3) || (_isSate == 4)) && (_tranProc == 0))
                {
                    if(maxPathHop > networkR)
                    {
                        // std::cout << "!src: " << m << ", dst: " << k << ", PathHop: " << maxPathHop << ", networkR: " << networkR << std::endl;
                        continue;
                    }
                }
                #endif
                data[m][k] += temp_data[index][k];
                // cout<<"data[" <<m<<"]["<<k << "]\t"<< data[m][k] <<endl;
                }
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
        int maxPacketCount;
        if ((_isSate == 1) && (_trafficMode == 0))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
        }
        else if ((_isSate == 1) && (_trafficMode == 1))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 15.0;
        }
        else if ((_isSate == 2) && (_trafficMode == 0))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 2.0;
        }
        else if ((_isSate == 2) && (_trafficMode == 1))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount = maxPacketCount / 30.0;
        }        
        else if((_isSate == 3) && (_trafficMode == 0))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 20.0;
        }
        else if((_isSate == 3) && (_trafficMode == 1))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 200.0;
        }
        else if((_isSate == 4) && (_trafficMode == 0))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 200.0;
        }
        else if((_isSate == 4) && (_trafficMode == 1))
        {
          maxPacketCount = int(data[i][j] * 1024.0 * 1024.0 / (packetSize * 8.0));   // 数据包数量
          maxPacketCount /= 200.0;
        }
        // cout<<maxPacketCount<<endl;
        double interPacketInterval = (double)(totalTimeStep - 10)/maxPacketCount;   // 数据包间隔 100s的流量
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
        apps.Stop(Seconds(totalTimeStep - 10));
      }
    }
  }

  // 周期性调度，根据动态分簇结果更新管控架构
  //Simulator::Schedule(Seconds(1), &installClient, data, numNodes, portStart, nodes);
}

// 为每个节点创建应用 client加server
void buildApp(){
  // if(_BreakDetect) return ;
  vector<vector<double>> data(sates.GetN()*totalTimeStep, vector<double>(sates.GetN(), 0));
  // 获取excel数据
  // GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Iridium).csv");
  if(_trafficMode == 0)
  {// 区域热点流量
    if(_isSate == 1){
        if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1).csv");
        else if(linkBandwidth == 100000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1).csv");
        else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1)_100Mbps.csv");
        else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat1)_500Mbps.csv");
    }
    else if(_isSate == 2)
    {
        if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat2).csv");
        else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat2)_100Mbps.csv");
        else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat2)_500Mbps.csv");
    }
    else if(_isSate == 3)
    {
        if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3).csv");
        else if(linkBandwidth == 100000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3).csv");
        else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3)_100Mbps.csv");
        else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat3)_500Mbps.csv");    
    }
    else if(_isSate == 4)
    {   // TODO
        if(linkBandwidth == 10000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat4).csv");
        else if(linkBandwidth == 100000000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat4).csv");
        else if(linkBandwidth == 100000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat4)_100Mbps.csv");
        else if(linkBandwidth == 500000000) GetData(data, sates.GetN(), "examples/sdn-controller/traffic_matrix(Sat4)_500Mbps.csv");    
    }

  }
  else if(_trafficMode == 1)
  {// 均匀流量
    if(_isSate == 1)
    {
        GetData(data, sates.GetN(), "examples/sdn-controller/uniform_traffic_matrix(Sat1).csv");
    }
    else if(_isSate == 2)
    {
        GetData(data, sates.GetN(), "examples/sdn-controller/uniform_traffic_matrix(Sat2).csv");
    }
    else if(_isSate == 3)
    {
        GetData(data, sates.GetN(), "examples/sdn-controller/uniform_traffic_matrix(Sat3)_30s.csv");
    }
    else if(_isSate == 4)
    {   // TODO
        GetData(data, sates.GetN(), "examples/sdn-controller/uniform_traffic_matrix(Sat4)_30s.csv");
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
  uint32_t TxBytes = 0;         //!< 发送字节数.
  uint32_t TxBytesCtl = 0;         //!< 发送字节数.

  uint32_t sumChangeClusterNum = 0; //!< 总簇ID更新次数.
  // Ipv4FlowClassifier::FiveTuple t;

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
                 "  Tx Bytes: " << TxBytes << " bytes\n"
                 "  Rx Packets: " << RxPackets << " p\n"
                 "  lostPackets: " << lostPackets << " p\n"
                 "  Latency: " << Latency*1000 << " ms\n"
                 "  Throughput: "<< Throughput/1000 << " Gbps\n"
                 "  Jitter: " << Jitter*1000 << " ms\n"
                 "  Loss Packet Ratio: " << (double)lostPackets * 100 / TxPackets << " %\n"
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

    std::cout << "--------------Performance--------------" << std::endl;
    std::cout << "  Average Link Utilization: " << AvgLinkUtil * 100 << " %\n"
                 "  Maximum Link Utilization: " << MaxLinkUtil * 100 << " %\n"
                 "  Control Overhead: " << ctlPkt << " \n"
                 "  Cluster Stability: " << 100 - clusterStablity * 100  << " %\n"
                 ;
}

int
main (int argc, char *argv[])
{
  // #ifdef NS3_OPENFLOW

  // offeredload = 6.0;

  CommandLine cmd;
  cmd.AddValue ("offeredload", "范围：0.5-6.0", offeredload);
  cmd.AddValue ("SDNRoute", "0:OSPF, 1:Hydra路由", _SDNRoute);
  cmd.AddValue ("isSate", "1:60颗, 2:108颗, 3:500颗, 4:432颗", _isSate);
  cmd.AddValue ("consType", "Some parameter", _consType);  
  cmd.AddValue ("linkBandwidth", "Some parameter", linkBandwidth);
  cmd.AddValue("tranProtocol", "0:UDP, 1:TCP", _tranProc);
  cmd.Parse (argc, argv);
  std::cout << "offeredLoad:" << offeredload 
            << "\tisSDNRoute:" << _SDNRoute
            << "\tlinkBandwidth:" << linkBandwidth
            << "\ttranProc:" << (_tranProc == 1 ? "TCP" : "UDP")
            << std::endl;

  sates_num = _isSate == 1 ? 60 : (_isSate == 2 ? 108 : (_isSate == 3 ? 500 : 432));    // N: LEO卫星总数
  orbit_num = _isSate == 1 ? 6 : (_isSate == 2 ? 12 : (_isSate == 3 ? 25 : 24));        // No: 轨道数
  sate_num = sates_num / orbit_num;


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
  //
  // Explicitly create the nodes required by the topology (shown above).
  //
  NS_LOG_INFO ("Create nodes.");

  initTopo();

  // updateTopo();    // 由于该行易导致每次更新间隔出现两次路由计算，故注释（luxueyu）,后续可讨论

  std::cout << "开始测试数据包发送！" << std::endl;
  
  // 构建并启动应用
  buildApp();

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

  if(_slaveMode){
    for(uint32_t i=0; i<scs.GetN(); i++){
      Ptr<satelliteNode> node = DynamicCast<satelliteNode>(scs.Get(i));
      node->StartRecvPacket_sate();
    }
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
    Time starttime = Seconds(15);
    Simulator::Schedule(starttime, &MasterRecoveryTest);
  }
  else if(_mode == 3){
    Ptr<satelliteNode> master_node = DynamicCast<satelliteNode>(mcs.Get(0));
    master_node->SendIdentity();
    master_node->SendHeartbeat();

    // 从控制器失效场景下的迁移以及恢复测试
    // 10s的时候，设置某从控制器失效
    Time starttime = Seconds(10);
    Simulator::Schedule(starttime, &SlaveRecoveryTest);
  }

  //
  // Now, do the actual simulation.
  //
  NS_LOG_INFO ("Run Simulation.");

  Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

  //控制开销随时间变化
  Simulator::Schedule(Seconds(10), &calCtlPktPer);

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