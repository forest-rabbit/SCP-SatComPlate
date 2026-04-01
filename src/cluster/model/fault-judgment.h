#ifndef FAULT_JUDGMENT_H
#define FAULT_JUDGMENT_H

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"

#include <iostream>
#include <unordered_map>
using namespace std;
using namespace ns3;


/**
 * Get the device on the connected nodes(网卡)
 * \param
 * \return
*/
pair<Ptr<PointToPointNetDevice>, Ptr<PointToPointNetDevice>> GetDevicesFromNodes(Ptr<Node> node1, Ptr<Node> node2);

/**
 * Get node's adjacent node
 * \param
 * \return
*/
NodeContainer FindNeighbors(Ptr<Node> node);

/**
 * Judge the connection between two nodes 
 * \param
 * \return
*/
bool IsConnect(Ptr<Node> node1, Ptr<Node> node2);

/**
 * Set the link falut(rand probability)--node1 and node2 is connected
 * \param 
 * \return
*/
void SetLinkFault(NodeContainer node);

/**
 * Judge the fault type
 * \param
 * \return
*/
std::unordered_map<uint32_t,uint32_t> JudgeFault(NodeContainer node);





#endif
