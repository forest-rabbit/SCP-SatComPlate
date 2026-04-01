#ifndef ACCESS_H
#define ACCESS_H

#include "ns3/point-to-point-module.h"
#include "ns3/core-module.h"
#include "ns3/ipv4.h"
#include "para.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using namespace std;

extern int rows;
extern int cols;
extern int d_flag; //1表示发生链路断开
extern int c_flag; //1表示链路重新连接
double* GetData(const int col, const int row, std::string name);
std::pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> GetDevicesFromNodes(Ptr<Node> node1, Ptr<Node> node2);
void LinkDown(Ptr<Node> node1, Ptr<Node> node2);
void LinkUp(Ptr<Node> node1, Ptr<Node> node2);
void LinkChangeSim(NodeContainer& nodes, uint32_t holdTime, uint32_t id1, uint32_t id2);
void LinkChange(NodeContainer& nodes, std::string name);
void SetLinkAvaAvailability(NodeContainer& nodes);

bool IsConnect(Ptr<Node> node1, Ptr<Node> node2);
NodeContainer FindNeibors(Ptr<Node> node);

#endif
