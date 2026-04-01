#ifndef LINK_UTILIZATION_H
#define LINK_UTILIZATION_H

#define CLUSTERNUM 12

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/timer.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4.h"

using namespace ns3;
using namespace std;
//each node is a class
class LinkUtilizationMonitor:  public Object
{
public:
    double max_utilization;   // max link utilization
    std::map<ns3::Ipv4Address, double> eachD_utilization; 
    Ptr<Node> m_node;         // monitor the node
    map<uint32_t, uint32_t> m_totalDevice;

  /**
   * \brief Get the TypeId
   *
   * \return The TypeId for this class
   */
  static TypeId GetTypeId (void);

/**
 * \brief Set the link bandwidth
 * \param l link bandwidth
*/
  void SetLinkCapacity(uint32_t l);

/**
 * \brief Set the simulation end time
 * \param t end time (second(s))
*/
  void SetStopTime(double t);

  /**
   * \brief Monitor the node traffic
   * \param node a node 
  */
  void StartMonitoring(Ptr<Node> node);

  /**
   * \brief Callback function--send the number of packet
   * \param p a packet 
   * \param p2p the p2p network device
  */
  void TrackTxBytes(Ptr<const Packet> p, Ptr<const PointToPointNetDevice> p2p);

  /**
   * \brief Get the max link utilization
  */
  void CheckAndUpdateMaxUtilization();
  
  /**
   * \brief Get each link utilization
  */
  void CheckAndUpdateEachLinkUtilization();

private:
    double CalculateLinkUtilization(uint32_t bytes, uint32_t capacity);

    uint32_t link_capacity; // link bandwidth
    double m_time; //end time
    map<uint32_t, uint32_t> m_txBytes; // each device sends packet size
    map<Address,uint32_t> m_device;                     //key--device address value--total packet sizes
    // Address  mac_address;
    map<Ipv4Address,uint32_t> m_device1;                //key--device IP address value--total packet sizes

    EventId m_linkUtilization;

};

#endif